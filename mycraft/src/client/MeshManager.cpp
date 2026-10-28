#include "game/client/MeshManager.h"
#include "game/client/MeshBuilderUtils.h"
#include "game/core/LightEngine.h"
#include <algorithm>

namespace game::client {

// ==========================================
// 内部辅助工具（消灭重复的 push_back）
// ==========================================
namespace {
    void PushQuad(std::vector<Eng::client::Vertex>& vertices, std::vector<uint32_t>& indices,
                  const Eng::client::Vertex& v0, const Eng::client::Vertex& v1,
                  const Eng::client::Vertex& v2, const Eng::client::Vertex& v3,
                  bool doubleSided = false) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.insert(vertices.end(), { v0, v1, v2, v3 });

        indices.insert(indices.end(), { base+0, base+1, base+2, base+2, base+3, base+0 });
        if (doubleSided) {
            indices.insert(indices.end(), { base+0, base+2, base+1, base+3, base+2, base+0 });
        }
    }
} // namespace

// ==========================================
// 多线程管理
// ==========================================
void MeshManager::Start() {
    unsigned hw = std::thread::hardware_concurrency();
    unsigned n  = std::clamp(hw > 2 ? hw - 2 : 2u, 2u, 8u);
    m_stop = false;
    for (unsigned i = 0; i < n; ++i) {
        m_workers.emplace_back([this] { WorkerLoop(); });
    }
}

void MeshManager::Stop() {
    if (m_workers.empty()) return;
    {
        std::lock_guard lk(m_taskMutex);
        m_stop = true;
        while (!m_taskQueue.empty()) m_taskQueue.pop();
    }
    m_taskCv.notify_all();
    for (auto& t : m_workers) {
        if (t.joinable()) t.join();
    }
    m_workers.clear();
}

void MeshManager::Submit(uint64_t key, std::shared_ptr<Chunk> chunk, std::shared_ptr<Chunk> nXm, std::shared_ptr<Chunk> nXp, std::shared_ptr<Chunk> nZm, std::shared_ptr<Chunk> nZp) {
    {
        std::lock_guard lk(m_taskMutex);
        if (m_inflightTasks.count(key)) return; 
        m_inflightTasks.insert(key);
    }
    
    auto task = std::make_shared<MeshTask>();
    task->chunkKey = key;
    task->chunk = std::move(chunk);
    task->nXm   = std::move(nXm);
    task->nXp   = std::move(nXp);
    task->nZm   = std::move(nZm);
    task->nZp   = std::move(nZp);

    {
        std::lock_guard lk(m_taskMutex);
        m_taskQueue.push(task);
    }
    m_taskCv.notify_one();
}

void MeshManager::DrainResults(std::function<void(MeshResult&&)>&& onComplete) {
    constexpr int K = 4; 
    int done = 0;
    for (;;) {
        MeshResult r;
        {
            std::lock_guard lk(m_resultMutex);
            if (m_resultQueue.empty() || done >= K) break;
            r = std::move(m_resultQueue.front());
            m_resultQueue.pop();
        }
        onComplete(std::move(r));
        ++done;
    }
}

void MeshManager::WorkerLoop() {
    while (true) {
        std::shared_ptr<MeshTask> task;
        {
            std::unique_lock lk(m_taskMutex);
            m_taskCv.wait(lk, [this] { return m_stop.load() || !m_taskQueue.empty(); });
            if (m_stop.load()) return;
            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }

        MeshResult result;
        result.chunkKey = task->chunkKey;
        BuildChunkMeshData(*task, result);

        {
            std::lock_guard lk(m_resultMutex);
            m_resultQueue.push(std::move(result));
        }
        
        {
            std::lock_guard lk(m_taskMutex);
            m_inflightTasks.erase(task->chunkKey);
        }
    }
}

// ==========================================
// Mesh 构建核心
// ==========================================
void MeshManager::BuildChunkMeshData(MeshTask& task, MeshResult& result) {
    result.vertices.clear();
    result.indices.clear();
    result.vertices.reserve(10000);
    result.indices.reserve(15000);

    // 使用 unique_ptr 在堆上分配局部副本，避免 1.6MB 对象撑爆线程栈
    auto pC  = std::make_unique<Chunk>(*task.chunk);
    auto pXm = task.nXm ? std::make_unique<Chunk>(*task.nXm) : nullptr;
    auto pXp = task.nXp ? std::make_unique<Chunk>(*task.nXp) : nullptr;
    auto pZm = task.nZm ? std::make_unique<Chunk>(*task.nZm) : nullptr;
    auto pZp = task.nZp ? std::make_unique<Chunk>(*task.nZp) : nullptr;

    // 传 get() 拿到底层裸指针
    LightEngine::InitRegion(*pC, pXm.get(), pXp.get(), pZm.get(), pZp.get());
    
    // 将算好的光照带回结果
    result.sky = pC->sky;
    result.block = pC->block;

    const Chunk& chunk = *pC;
    const int cx = chunk.chunk_x;
    const int cz = chunk.chunk_z;
    const int baseX = cx * CHUNK_SIZE_X;
    const int baseZ = cz * CHUNK_SIZE_Z;

    auto getBlock = [&](int lx, int ly, int lz) -> BlockType {
        if (ly < 0 || ly >= CHUNK_SIZE_Y) return BlockType::Air;
        if (lx >= 0 && lx < CHUNK_SIZE_X && lz >= 0 && lz < CHUNK_SIZE_Z) return ChunkGet(chunk, lx, ly, lz);
        
        const Chunk* n = nullptr; 
        int nlx = lx, nlz = lz;
        if      (lx < 0)             { n = pXm.get(); nlx = lx + CHUNK_SIZE_X; }
        else if (lx >= CHUNK_SIZE_X) { n = pXp.get(); nlx = lx - CHUNK_SIZE_X; }
        else if (lz < 0)             { n = pZm.get(); nlz = lz + CHUNK_SIZE_Z; }
        else if (lz >= CHUNK_SIZE_Z) { n = pZp.get(); nlz = lz - CHUNK_SIZE_Z; }
        
        if (!n || nlx < 0 || nlx >= CHUNK_SIZE_X || nlz < 0 || nlz >= CHUNK_SIZE_Z) return BlockType::Air;
        return ChunkGet(*n, nlx, ly, nlz);
    };

    auto getLightAt = [&](int wx, int wy, int wz) -> glm::vec4 {
        if (wy < 0 || wy >= CHUNK_SIZE_Y) return glm::vec4(0.0f);
        int idx;
        if (wx >= 0 && wx < CHUNK_SIZE_X && wz >= 0 && wz < CHUNK_SIZE_Z) {
            idx = ChunkIndex(wx, wy, wz);
            return { chunk.sky[idx] / 15.0f, chunk.block[idx].r / 15.0f, chunk.block[idx].g / 15.0f, chunk.block[idx].b / 15.0f };
        }
        
        const Chunk* n = nullptr; 
        int nlx = wx, nlz = wz;
        if (wx < 0) { n = pXm.get(); nlx = wx + CHUNK_SIZE_X; }
        else if (wx >= CHUNK_SIZE_X) { n = pXp.get(); nlx = wx - CHUNK_SIZE_X; }
        else if (wz < 0) { n = pZm.get(); nlz = wz + CHUNK_SIZE_Z; }
        else if (wz >= CHUNK_SIZE_Z) { n = pZp.get(); nlz = wz - CHUNK_SIZE_Z; }
        
        if (n && nlx >= 0 && nlx < CHUNK_SIZE_X && nlz >= 0 && nlz < CHUNK_SIZE_Z) {
            idx = ChunkIndex(nlx, wy, nlz);
            return { n->sky[idx] / 15.0f, n->block[idx].r / 15.0f, n->block[idx].g / 15.0f, n->block[idx].b / 15.0f };
        }
        return glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    };

    auto isSolidAt = [&](int lx, int ly, int lz) -> bool {
        BlockType b = getBlock(lx, ly, lz);
        return IsSolid(b);
    };

    for (int lx = 0; lx < CHUNK_SIZE_X; ++lx) {
        for (int ly = 0; ly < CHUNK_SIZE_Y; ++ly) {
            for (int lz = 0; lz < CHUNK_SIZE_Z; ++lz) {
                BlockType type = ChunkGet(chunk, lx, ly, lz);
                if (type == BlockType::Air || type == BlockType::Void) continue;

                // --- 1.火把和植物 ---
                if (type == BlockType::Torch || type == BlockType::TallGrass || type == BlockType::FlowerRed || type == BlockType::FlowerYellow) {
                    glm::vec4 light;
                    if (type == BlockType::Torch) {
                        light = glm::vec4(0.0f, 1.0f, 0.6f, 0.2f); // 火把自发光
                    } else {
                        light = getLightAt(lx, ly, lz);             // 植物吃环境光
                    }
                    glm::vec3 center = { baseX + lx + 0.5f, (float)ly + 0.5f, baseZ + lz + 0.5f };
                    
                    MeshBuilderUtils::AppendSprite(type, center, light, result.vertices, result.indices);
                    continue;
                }

                // --- 2. 普通方块 ---
                glm::vec3 center = { baseX + lx + 0.5f, ly + 0.5f, baseZ + lz + 0.5f };
                auto faces = MeshBuilderUtils::GetBlockFaces(type, center);

                for (int f = 0; f < 6; ++f) {
                    glm::ivec3 N = faces[f].normal;
                    int nx = lx + N.x;
                    int ny = ly + N.y;
                    int nz = lz + N.z;
                    BlockType neighbor = getBlock(nx, ny, nz);

                    if (IsSolid(neighbor)) continue;
                    if (type == BlockType::Water && neighbor == BlockType::Water) continue;

                    for (int v = 0; v < 4; ++v) {
                        // 处理水面高度
                        if (type == BlockType::Water && faces[f].verts[v].position.y > ly + 0.01f) {
                            faces[f].verts[v].position.y = (float)ly + 0.9f;
                        }

                        // 拿回 U、V 轴
                        glm::ivec3 U(0), V(0);
                        U[faces[f].u_axis] = faces[f].du[v]; 
                        V[faces[f].v_axis] = faces[f].dv[v];

                        // AO 计算
                        int s1 = isSolidAt(lx + N.x + U.x, ly + N.y + U.y, lz + N.z + U.z) ? 1 : 0;
                        int s2 = isSolidAt(lx + N.x + V.x, ly + N.y + V.y, lz + N.z + V.z) ? 1 : 0;
                        int sc = isSolidAt(lx + N.x + U.x + V.x, ly + N.y + U.y + V.y, lz + N.z + U.z + V.z) ? 1 : 0;
                        float ao = (s1 && s2) ? 0.4f : 1.0f - 0.2f * static_cast<float>(s1 + s2 + sc);
                        if (type == BlockType::Water) ao = 1.0f;
                        faces[f].verts[v].ao = ao;

                        // 平滑光照采样
                        glm::ivec3 pO(lx + N.x, ly + N.y, lz + N.z);
                        glm::vec4 sumL = getLightAt(pO.x, pO.y, pO.z) +
                                         getLightAt(pO.x + U.x, pO.y + U.y, pO.z + U.z) +
                                         getLightAt(pO.x + V.x, pO.y + V.y, pO.z + V.z) +
                                         getLightAt(pO.x + U.x + V.x, pO.y + U.y + V.y, pO.z + U.z + V.z);
                        glm::vec4 light = sumL / 4.0f;
                        if (type == BlockType::Water) light = glm::vec4(1.0f);
                        faces[f].verts[v].light = light;
                    }
                    
                    // 推送计算好后的顶点
                    PushQuad(result.vertices, result.indices, 
                             faces[f].verts[0], faces[f].verts[1], faces[f].verts[2], faces[f].verts[3]);
                }
            }
        }
    }
}

} // namespace game::client