#include "game/client/MeshManager.h"
#include "game/client/MeshBuilderUtils.h"
#include "game/core/BlockRegistry.h"
#include <algorithm>

namespace game::client {

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
}

// ============================================================
// 线程生命周期管理
// ============================================================
void MeshManager::Start() {
    if (!m_workers.empty()) return; // 已启动
    m_stop = false;

    auto threadCount = std::thread::hardware_concurrency();

    if (threadCount == 0) threadCount = 4; // 兜底

    for (unsigned int i = 0; i < threadCount; ++i) {
        m_workers.emplace_back(&MeshManager::WorkerLoop, this);
    }
}

void MeshManager::Stop() {
    if (m_workers.empty()) return;
    
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_stop = true;
        // 直接清空积压的任务，别让主线程傻等
        while (!m_taskQueue.empty()) {
            m_taskQueue.pop();
        }
    }
    
    m_taskCv.notify_all(); 

    for (auto& t : m_workers) {
        if (t.joinable()) t.join();
    }
    m_workers.clear();
}

// ============================================================
// 任务提交
// ============================================================
void MeshManager::Submit(uint64_t key, std::shared_ptr<Chunk> chunk, 
                         std::shared_ptr<Chunk> nXm, std::shared_ptr<Chunk> nXp, 
                         std::shared_ptr<Chunk> nZm, std::shared_ptr<Chunk> nZp) 
{
    auto task = std::make_shared<MeshTask>();
    task->chunkKey = key;
    task->chunk = std::move(chunk);
    task->nXm = std::move(nXm);
    task->nXp = std::move(nXp);
    task->nZm = std::move(nZm);
    task->nZp = std::move(nZp);
    task->reg = m_registry; // 注入注册表

    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_taskQueue.push(std::move(task));
    }
    m_taskCv.notify_one();
}

// ============================================================
// 结果收集（主线程调用）
// ============================================================
void MeshManager::DrainResults(std::function<void(MeshResult&&)>&& onComplete) {
    std::queue<MeshResult> localQueue;
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        std::swap(localQueue, m_resultQueue);
    }

    while (!localQueue.empty()) {
        onComplete(std::move(localQueue.front()));
        localQueue.pop();
    }
}

// ============================================================
// Worker 线程主循环
// ============================================================
void MeshManager::WorkerLoop() {
    while (true) {
        std::shared_ptr<MeshTask> task;
        {
            std::unique_lock<std::mutex> lock(m_taskMutex);
            m_taskCv.wait(lock, [this] { return m_stop || !m_taskQueue.empty(); });

            if (m_stop && m_taskQueue.empty()) {
                return; // 退出线程
            }

            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }

        // 构建 Mesh
        MeshResult result;
        result.chunkKey = task->chunkKey;
        BuildChunkMeshData(*task, result);

        // 将结果推入结果队列
        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_resultQueue.push(std::move(result));
        }
    }
}

// ============================================================
// 核心 Mesh 构建逻辑
// ============================================================
void MeshManager::BuildChunkMeshData(MeshTask& task, MeshResult& result) {
    result.vertices.clear();
    result.indices.clear();
    // 稍微调整预留大小，减少扩容
    result.vertices.reserve(12000);
    result.indices.reserve(24000);

    if (!task.reg) return; 
    const game::BlockRegistry& reg = *task.reg;

    const Chunk& chunk = *task.chunk;
    const Chunk* pXm = task.nXm.get();
    const Chunk* pXp = task.nXp.get();
    const Chunk* pZm = task.nZm.get();
    const Chunk* pZp = task.nZp.get();

    const int cx = chunk.chunk_x;
    const int cz = chunk.chunk_z;
    const int baseX = cx * CHUNK_SIZE_X;
    const int baseZ = cz * CHUNK_SIZE_Z;

    const int Y_STRIDE = CHUNK_SIZE_X * CHUNK_SIZE_Z;
    const int Z_STRIDE = CHUNK_SIZE_X;

    auto getBlock = [&](int lx, int ly, int lz) -> BlockId {
        if (ly < 0 || ly >= CHUNK_SIZE_Y) return AIR_ID;
        if (lx >= 0 && lx < CHUNK_SIZE_X && lz >= 0 && lz < CHUNK_SIZE_Z) {
            return chunk.blocks[ly * Y_STRIDE + lz * Z_STRIDE + lx];
        }
        const Chunk* n = nullptr; 
        int nlx = lx, nlz = lz;
        if      (lx < 0)             { n = pXm; nlx = lx + CHUNK_SIZE_X; }
        else if (lx >= CHUNK_SIZE_X) { n = pXp; nlx = lx - CHUNK_SIZE_X; }
        else if (lz < 0)             { n = pZm; nlz = lz + CHUNK_SIZE_Z; }
        else if (lz >= CHUNK_SIZE_Z) { n = pZp; nlz = lz - CHUNK_SIZE_Z; }
        
        if (!n || nlx < 0 || nlx >= CHUNK_SIZE_X || nlz < 0 || nlz >= CHUNK_SIZE_Z) return AIR_ID;
        return n->blocks[ly * Y_STRIDE + nlz * Z_STRIDE + nlx];
    };

    auto getLightAt = [&](int lx, int ly, int lz) -> glm::vec4 {
        if (ly < 0 || ly >= CHUNK_SIZE_Y) return glm::vec4(0.0f);
        if (lx >= 0 && lx < CHUNK_SIZE_X && lz >= 0 && lz < CHUNK_SIZE_Z) {
            int idx = ly * Y_STRIDE + lz * Z_STRIDE + lx;
            return { chunk.sky[idx] / 15.0f, chunk.block[idx].r / 15.0f, chunk.block[idx].g / 15.0f, chunk.block[idx].b / 15.0f };
        }
        // 邻居区块如果没加载，直接给满光，防止边缘黑线
        return glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    };

    auto isSolidAt = [&](int lx, int ly, int lz) -> bool {
        return reg.IsSolid(getBlock(lx, ly, lz));
    };

    // 预计算法线，避免循环内构造
    const glm::ivec3 normals[6] = {
        {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
    };

    // 正确的循环顺序：y -> z -> x (匹配底层数组内存连续)
    for (int ly = 0; ly < CHUNK_SIZE_Y; ++ly) {
        for (int lz = 0; lz < CHUNK_SIZE_Z; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE_X; ++lx) {
                BlockId type = chunk.blocks[ly * Y_STRIDE + lz * Z_STRIDE + lx];
                if (type == AIR_ID) continue;

                const auto& def = reg.Get(type);

                // --- 1. 十字模型（草、花、火把） ---
                if (def.render.isCross) {
                    glm::vec4 light = (def.render.luminance > 0) 
                        ? glm::vec4(0.0f, 1.0f, 0.6f, 0.2f)
                        : getLightAt(lx, ly, lz);

                    if (def.render.luminance > 0) {
                        MeshBuilderUtils::AppendTorch(reg, type, baseX + lx, ly, baseZ + lz, light, result.vertices, result.indices);
                    } else {
                        glm::vec3 center = { baseX + lx + 0.5f, (float)ly + 0.5f, baseZ + lz + 0.5f };
                        MeshBuilderUtils::AppendSprite(reg, type, center, light, result.vertices, result.indices);
                    }
                    continue;
                }

                // --- 2. 普通方块 ---
                glm::vec3 center = { baseX + lx + 0.5f, (float)ly + 0.5f, baseZ + lz + 0.5f };
                
                // 先做面剔除，如果 6 个面都被挡住，直接跳过，省去 GetBlockFaces 的几何计算！
                bool visible[6] = {false};
                int visibleCount = 0;
                for (int f = 0; f < 6; ++f) {
                    BlockId neighborId = getBlock(lx + normals[f].x, ly + normals[f].y, lz + normals[f].z);
                    const auto& ndef = reg.Get(neighborId);

                    if (ndef.render.isOpaque) continue;
                    if (def.render.isLiquid && ndef.render.isLiquid && type == neighborId) continue;

                    visible[f] = true;
                    visibleCount++;
                }

                if (visibleCount == 0) continue; // 完全被挡住，跳过！

                // 只有确认有可见面，才去构建几何体
                auto faces = MeshBuilderUtils::GetBlockFaces(reg, type, center);

                for (int f = 0; f < 6; ++f) {
                    if (!visible[f]) continue;

                    glm::ivec3 N = normals[f];

                    for (int v = 0; v < 4; ++v) {
                        if (def.render.isLiquid && faces[f].verts[v].position.y > ly + 0.01f) {
                            faces[f].verts[v].position.y = (float)ly + 0.9f;
                        }

                        glm::ivec3 U(0), V(0);
                        U[faces[f].u_axis] = faces[f].du[v]; 
                        V[faces[f].v_axis] = faces[f].dv[v];

                        int s1 = isSolidAt(lx + N.x + U.x, ly + N.y + U.y, lz + N.z + U.z) ? 1 : 0;
                        int s2 = isSolidAt(lx + N.x + V.x, ly + N.y + V.y, lz + N.z + V.z) ? 1 : 0;
                        int sc = isSolidAt(lx + N.x + U.x + V.x, ly + N.y + U.y + V.y, lz + N.z + U.z + V.z) ? 1 : 0;
                        float ao = (s1 && s2) ? 0.4f : 1.0f - 0.2f * static_cast<float>(s1 + s2 + sc);
                        if (def.render.isLiquid) ao = 1.0f;
                        faces[f].verts[v].ao = ao;

                        glm::ivec3 pO(lx + N.x, ly + N.y, lz + N.z);
                        glm::vec4 sumL = getLightAt(pO.x, pO.y, pO.z) +
                                         getLightAt(pO.x + U.x, pO.y + U.y, pO.z + U.z) +
                                         getLightAt(pO.x + V.x, pO.y + V.y, pO.z + V.z) +
                                         getLightAt(pO.x + U.x + V.x, pO.y + U.y + V.y, pO.z + U.z + V.z);
                        glm::vec4 light = sumL / 4.0f;
                        
                        faces[f].verts[v].light = light;
                    }
                    
                    PushQuad(result.vertices, result.indices, 
                             faces[f].verts[0], faces[f].verts[1], faces[f].verts[2], faces[f].verts[3]);
                }
            }
        }
    }
}

} // namespace game::client