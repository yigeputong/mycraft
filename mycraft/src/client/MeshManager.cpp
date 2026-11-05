#include "game/client/MeshManager.h"
#include "game/client/MeshBuilderUtils.h"
#include "game/client/TextureArray.h"
#include "Engine/tool/Platform.h"
#include "Engine/tool/Log.h"
namespace game::client {

void MeshManager::Start() {
    if (!m_workers.empty()) return;
    m_stop = false;

    unsigned n = Eng::tool::GetRecommendedWorkerThreads();
    auto logger = std::make_unique<Eng::tool::Log>();
    logInfo(logger, "[MeshManager] Starting " << n << " mesh worker threads");

    for (unsigned int i = 0; i < n; ++i) {
        m_workers.push_back(std::make_unique<Eng::tool::RAIIThread>(
            [this](const std::stop_token& st) { WorkerLoop(st); }
        ));
    }
}

void MeshManager::Stop() {
    if (m_workers.empty()) return;

    // 1. 请求所有线程停止
    for (auto& w : m_workers) {
        w->RequestStop();
    }

    // 2. 清空任务队列
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_stop = true;
        while (!m_taskQueue.empty()) m_taskQueue.pop();
    }
    m_taskCv.notify_all(); 
    
    // 3. 唤醒阻塞在结果队列的线程
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
    }
    m_resultCv.notify_all(); 

    // 4. 触发 RAIIThread 析构 join
    m_workers.clear();
}

void MeshManager::Submit(uint64_t key, uint32_t version, 
                         std::shared_ptr<Chunk> chunk, 
                         std::shared_ptr<Chunk> nXm, std::shared_ptr<Chunk> nXp, 
                         std::shared_ptr<Chunk> nZm, std::shared_ptr<Chunk> nZp) 
{
    auto task = std::make_shared<MeshTask>();
    task->chunkKey = key;
    task->version = version;
    task->chunk = std::move(chunk);
    task->nXm = std::move(nXm);
    task->nXp = std::move(nXp);
    task->nZm = std::move(nZm);
    task->nZp = std::move(nZp);
    task->reg = m_registry;

    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_taskQueue.push(std::move(task));
    }
    m_taskCv.notify_one();
}

void MeshManager::DrainResults(size_t maxCount, std::function<void(MeshResult&&)>&& onComplete) {
    size_t processed = 0;
    
    while (processed < maxCount) {
        MeshResult result;
        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            if (m_resultQueue.empty()) break;
            
            // ★ 一次只取 maxCount 个，剩下的安全留在队列里
            result = std::move(m_resultQueue.front());
            m_resultQueue.pop();
        }
        
        onComplete(std::move(result));
        processed++;
    }
    
    // 唤醒因为结果队列满（>=64）而阻塞的 Worker
    m_resultCv.notify_all();
}

void MeshManager::WorkerLoop(const std::stop_token& st) {
    while (!st.stop_requested()) {
        std::shared_ptr<MeshTask> task;
        {
            std::unique_lock<std::mutex> lock(m_taskMutex);
            m_taskCv.wait(lock, [this, &st] { 
                return st.stop_requested() || m_stop || !m_taskQueue.empty(); 
            });
            
            if ((st.stop_requested() || m_stop) && m_taskQueue.empty()) return;
            if (m_taskQueue.empty()) continue;
            
            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }

        MeshResult result;
        result.chunkKey = task->chunkKey;
        result.version = task->version; // ★ 传递版本号
        
        BuildChunkMeshData(*task, result);

        {
            std::unique_lock<std::mutex> lock(m_resultMutex);
            // 背压：结果队列攒了 64 个还没消费，Worker 停止干活
            m_resultCv.wait(lock, [this, &st] { 
                return st.stop_requested() || m_stop || m_resultQueue.size() < 64; 
            });
            if (st.stop_requested() || m_stop) return;
            
            m_resultQueue.push(std::move(result));
        }
    }
}

// ============================================================
// 核心 Mesh 构建逻辑 (保留原版的高效逻辑)
// ============================================================
void MeshManager::BuildChunkMeshData(const MeshTask& task, MeshResult& result) {
    result.vertices.clear();
    result.indices.clear();
    result.waterVertices.clear();
    result.waterIndices.clear();
    
    result.vertices.reserve(8192); 
    result.indices.reserve(12288);

    if (!task.reg || !task.chunk) return; 
    const game::BlockRegistry& reg = *task.reg;
    const Chunk& chunk = *task.chunk;
    const BlockId* pC  = chunk.blocks.data();
    const Chunk* nXm = task.nXm.get();
    const Chunk* nXp = task.nXp.get();
    const Chunk* nZm = task.nZm.get();
    const Chunk* nZp = task.nZp.get();

    const int cx = chunk.chunk_x;
    const int cz = chunk.chunk_z;
    const float baseX = (float)(cx * CHUNK_SIZE_X);
    const float baseZ = (float)(cz * CHUNK_SIZE_Z);

    const int Y_STRIDE = 256;
    const int Z_STRIDE = 16;

    auto getBlock = [&](int lx, int ly, int lz) -> BlockId {
        if (ly < 0 || ly >= 256) return AIR_ID;
        if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16) return pC[ly * Y_STRIDE + lz * Z_STRIDE + lx];
        const BlockId* ptr = nullptr; 
        int nlx = lx, nlz = lz;
        if      (lx < 0)  { ptr = nXm ? nXm->blocks.data() : nullptr; nlx = lx + 16; }
        else if (lx >= 16){ ptr = nXp ? nXp->blocks.data() : nullptr; nlx = lx - 16; }
        else if (lz < 0)  { ptr = nZm ? nZm->blocks.data() : nullptr; nlz = lz + 16; }
        else if (lz >= 16){ ptr = nZp ? nZp->blocks.data() : nullptr; nlz = lz - 16; }
        if (!ptr || nlx < 0 || nlx >= 16 || nlz < 0 || nlz >= 16) return AIR_ID;
        return ptr[ly * Y_STRIDE + nlz * Z_STRIDE + nlx];
    };

    auto isSolidAt = [&](int lx, int ly, int lz) -> bool {
        return reg.IsSolid(getBlock(lx, ly, lz));
    };

    auto getLight = [&](int lx, int ly, int lz) -> glm::vec4 {
        if (ly < 0 || ly >= 256) return {1.0f, 0.0f, 0.0f, 0.0f};
        const Chunk* c = &chunk;
        int nlx = lx, nlz = lz;
        if (lx < 0) { c = nXm; nlx = lx + 16; }
        else if (lx >= 16) { c = nXp; nlx = lx - 16; }
        else if (lz < 0) { c = nZm; nlz = lz + 16; }
        else if (lz >= 16) { c = nZp; nlz = lz - 16; }
        if (!c || nlx < 0 || nlx >= 16 || nlz < 0 || nlz >= 16) return glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        
        int idx = ly * Y_STRIDE + nlz * Z_STRIDE + nlx;
        return { c->sky[idx] / 15.0f, c->block[idx].r / 15.0f, c->block[idx].g / 15.0f, c->block[idx].b / 15.0f };
    };

    const glm::ivec3 normals[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    const float faceOffsets[6][4][3] = {
        {{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f}},
        {{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},{-0.5f,-0.5f,-0.5f}},
        {{-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f}},
        {{-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}},
        {{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}},
        {{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f}},
    };
    const float faceUVs[4][2] = {{0,0},{0,1},{1,1},{1,0}};
    const int faceAxes[6][2] = { {2, 1}, {2, 1}, {0, 2}, {0, 2}, {0, 1}, {0, 1} };

    for (int ly = 0; ly < 256; ++ly) {
        for (int lz = 0; lz < 16; ++lz) {
            for (int lx = 0; lx < 16; ++lx) {
                BlockId type = pC[ly * Y_STRIDE + lz * Z_STRIDE + lx];
                if (type == AIR_ID) continue;

                const auto& def = reg.Get(type);

                if (def.render.isCross) {
                    std::vector<Eng::service::Vertex> tmpV;
                    std::vector<uint32_t> tmpI;
                    glm::vec4 light = (def.render.luminance > 0) ? glm::vec4(0.0f, 1.0f, 0.6f, 0.2f) : getLight(lx, ly, lz);
                    
                    if (def.render.luminance > 0) {
                        MeshBuilderUtils::AppendTorch(reg, type, (int)baseX + lx, ly, (int)baseZ + lz, light, tmpV, tmpI);
                    } else {
                        glm::vec3 center = { baseX + lx + 0.5f, (float)ly + 0.5f, baseZ + lz + 0.5f };
                        MeshBuilderUtils::AppendSprite(reg, type, center, light, tmpV, tmpI);
                    }
                    
                    uint32_t offset = static_cast<uint32_t>(result.vertices.size());
                    for (auto& v : tmpV) result.vertices.push_back(v);
                    for (auto& i : tmpI) result.indices.push_back(i + offset);
                    continue;
                }

                bool visible[6] = {false};
                for (int f = 0; f < 6; ++f) {
                    BlockId nId = getBlock(lx + normals[f].x, ly + normals[f].y, lz + normals[f].z);
                    const auto& ndef = reg.Get(nId);
                    if (ndef.render.isOpaque) continue;
                    if (def.render.isLiquid && ndef.render.isLiquid) continue;
                    visible[f] = true;
                }

                for (int f = 0; f < 6; ++f) {
                    if (!visible[f]) continue;
                    
                    // 直接获取层索引，不需要复杂的 UV 计算！
                    int layer = 0;
                    if (f == 2) { layer = ResolveLayer(def.render.ForTop().tile); }
                    else if (f == 3) { layer = ResolveLayer(def.render.ForBottom().tile); }
                    else { layer = ResolveLayer(def.render.ForSide().tile); }
                    if (layer < 0) layer = 0;

                    bool isLiquid = def.render.isLiquid;
                    auto& outV = isLiquid ? result.waterVertices : result.vertices;
                    auto& outI = isLiquid ? result.waterIndices  : result.indices;

                    auto baseV = static_cast<uint32_t>(outV.size());
                    int Nx = normals[f].x, Ny = normals[f].y, Nz = normals[f].z;

                    for (int v = 0; v < 4; ++v) {
                        Eng::service::Vertex vert;
                        
                        float vx = faceOffsets[f][v][0];
                        float vy = faceOffsets[f][v][1];
                        float vz = faceOffsets[f][v][2];

                        vert.position = glm::vec3(baseX + lx + vx + 0.5f, ly + vy + 0.5f, baseZ + lz + vz + 0.5f);
                        vert.normal = glm::vec3(Nx, Ny, Nz);
                        
                        if (isLiquid && vert.position.y > ly + 0.01f) vert.position.y = (float)ly + 0.9f;
                        
                        // UV 直接传 0~1，layer 传层号
                        vert.uv = { faceUVs[v][0], 1.0f - faceUVs[v][1] };
                        vert.layer = static_cast<float>(layer);
                        
                        int uA = faceAxes[f][0];
                        int vA = faceAxes[f][1];
                        int Ux = 0, Uy = 0, Uz = 0, Vx = 0, Vy = 0, Vz = 0;
                        
                        if (uA == 0) { Ux = (vx < 0.0f) ? -1 : 1; }
                        else if (uA == 1) { Uy = (vy < 0.0f) ? -1 : 1; }
                        else { Uz = (vz < 0.0f) ? -1 : 1; }

                        if (vA == 0) { Vx = (vx < 0.0f) ? -1 : 1; }
                        else if (vA == 1) { Vy = (vy < 0.0f) ? -1 : 1; }
                        else { Vz = (vz < 0.0f) ? -1 : 1; }

                        int s1 = isSolidAt(lx + Nx + Ux, ly + Ny + Uy, lz + Nz + Uz) ? 1 : 0;
                        int s2 = isSolidAt(lx + Nx + Vx, ly + Ny + Vy, lz + Nz + Vz) ? 1 : 0;
                        int sc = isSolidAt(lx + Nx + Ux + Vx, ly + Ny + Uy + Vy, lz + Nz + Uz + Vz) ? 1 : 0;
                        
                        float ao = (s1 && s2) ? 0.4f : 1.0f - 0.2f * static_cast<float>(s1 + s2 + sc);
                        if (isLiquid) ao = 1.0f;
                        vert.ao = ao;

                        vert.light = getLight(lx + normals[f].x, ly + normals[f].y, lz + normals[f].z); 
                        outV.push_back(vert);
                    }

                    outI.push_back(baseV + 0);
                    outI.push_back(baseV + 1);
                    outI.push_back(baseV + 2);
                    outI.push_back(baseV + 2);
                    outI.push_back(baseV + 3);
                    outI.push_back(baseV + 0);
                }
            }
        }
    }
}

} // namespace game::client