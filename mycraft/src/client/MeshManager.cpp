#include "game/client/MeshManager.h"
#include "game/client/TextureAtlas.h"
#include "game/core/LightEngine.h"
#include <algorithm>

namespace game::client {

// ==========================================
// 内部辅助工具（消灭重复的 push_back）
// ==========================================
namespace {
    Eng::client::Vertex MakeVertex(glm::vec3 pos, glm::vec3 normal, glm::vec2 uv, float ao, glm::vec4 light) {
        return { pos, normal, uv, ao, light };
    }

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

    constexpr int faceNormals[6][3] = {
        { 1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
    };
    constexpr int faceOffsets[6][4][3] = {
        {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},
        {{0,0,1},{0,1,1},{0,1,0},{0,0,0}},
        {{0,1,1},{1,1,1},{1,1,0},{0,1,0}},
        {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
        {{1,0,1},{1,1,1},{0,1,1},{0,0,1}},
        {{0,0,0},{0,1,0},{1,1,0},{1,0,0}},
    };

    constexpr float faceUVs[4][2] = {
        {0,0},{0,1},{1,1},{1,0}
    };
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

void MeshManager::Submit(uint64_t key, const Chunk& chunk,
                         const Chunk* nXm, const Chunk* nXp, const Chunk* nZm, const Chunk* nZp) {
    auto task = std::make_shared<MeshTask>();
    task->chunkKey = key;
    task->chunk = chunk;
    if (nXm) { task->nXm = *nXm; task->hasN[0] = true; }
    if (nXp) { task->nXp = *nXp; task->hasN[1] = true; }
    if (nZm) { task->nZm = *nZm; task->hasN[2] = true; }
    if (nZp) { task->nZp = *nZp; task->hasN[3] = true; }

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

    Chunk* pC  = &task.chunk;
    Chunk* pXm = task.hasN[0] ? &task.nXm : nullptr;
    Chunk* pXp = task.hasN[1] ? &task.nXp : nullptr;
    Chunk* pZm = task.hasN[2] ? &task.nZm : nullptr;
    Chunk* pZp = task.hasN[3] ? &task.nZp : nullptr;

    LightEngine::InitRegion(*pC, pXm, pXp, pZm, pZp);
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
        const Chunk* n = nullptr; int nlx = lx, nlz = lz;
        if      (lx < 0)             { n = pXm; nlx = lx + CHUNK_SIZE_X; }
        else if (lx >= CHUNK_SIZE_X) { n = pXp; nlx = lx - CHUNK_SIZE_X; }
        else if (lz < 0)             { n = pZm; nlz = lz + CHUNK_SIZE_Z; }
        else if (lz >= CHUNK_SIZE_Z) { n = pZp; nlz = lz - CHUNK_SIZE_Z; }
        if (!n || nlx < 0 || nlx >= CHUNK_SIZE_X || nlz < 0 || nlz >= CHUNK_SIZE_Z) return BlockType::Air;
        return ChunkGet(*n, nlx, ly, nlz);
    };

    auto isSolidAt = [&](int lx, int ly, int lz) -> bool {
        BlockType b = getBlock(lx, ly, lz);
        return b != BlockType::Air && b != BlockType::Water && b != BlockType::Void &&
               b != BlockType::TallGrass && b != BlockType::FlowerRed && b != BlockType::FlowerYellow;
    };

    auto getLightAt = [&](int wx, int wy, int wz) -> glm::vec4 {
        if (wy < 0 || wy >= CHUNK_SIZE_Y) return glm::vec4(0.0f);
        int idx;
        if (wx >= 0 && wx < CHUNK_SIZE_X && wz >= 0 && wz < CHUNK_SIZE_Z) {
            idx = ChunkIndex(wx, wy, wz);
            return { chunk.sky[idx] / 15.0f, chunk.block[idx].r / 15.0f, chunk.block[idx].g / 15.0f, chunk.block[idx].b / 15.0f };
        }
        const Chunk* n = nullptr; int nlx = wx, nlz = wz;
        if (wx < 0) { n = pXm; nlx = wx + CHUNK_SIZE_X; }
        else if (wx >= CHUNK_SIZE_X) { n = pXp; nlx = wx - CHUNK_SIZE_X; }
        else if (wz < 0) { n = pZm; nlz = wz + CHUNK_SIZE_Z; }
        else if (wz >= CHUNK_SIZE_Z) { n = pZp; nlz = wz - CHUNK_SIZE_Z; }
        if (n && nlx >= 0 && nlx < CHUNK_SIZE_X && nlz >= 0 && nlz < CHUNK_SIZE_Z) {
            idx = ChunkIndex(nlx, wy, nlz);
            return { n->sky[idx] / 15.0f, n->block[idx].r / 15.0f, n->block[idx].g / 15.0f, n->block[idx].b / 15.0f };
        }
        return glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    };

    for (int lx = 0; lx < CHUNK_SIZE_X; ++lx) {
        for (int ly = 0; ly < CHUNK_SIZE_Y; ++ly) {
            for (int lz = 0; lz < CHUNK_SIZE_Z; ++lz) {
                BlockType type = ChunkGet(chunk, lx, ly, lz);
                if (type == BlockType::Air || type == BlockType::Void) continue;

                // --- 1. 十字植物 ---
                if (type == BlockType::TallGrass || type == BlockType::FlowerRed || type == BlockType::FlowerYellow) {
                    int tile = TileForBlock(type, 0);
                    float u0 = (tile % ATLAS_COLS) * TILE_UV;
                    float v0 = (tile / static_cast<float>(ATLAS_COLS)) * TILE_UV;
                    float u1 = u0 + TILE_UV;
                    float v1 = v0 + TILE_UV;
                    
                    float x0 = baseX + lx + 0.15f, x1 = baseX + lx + 0.85f;
                    float z0 = baseZ + lz + 0.15f, z1 = baseZ + lz + 0.85f;
                    float y0 = (float)ly, y1 = (float)ly + 1.0f;
                    glm::vec4 light(1.0f, 0.0f, 0.0f, 0.0f);

                    // 原版 UV 直接对应坐标（左下、右下、右上、左上）
                    PushQuad(result.vertices, result.indices,
                        MakeVertex({x0, y0, z0}, {0,1,0}, {u0, v1}, 1.0f, light),
                        MakeVertex({x1, y0, z1}, {0,1,0}, {u1, v1}, 1.0f, light),
                        MakeVertex({x1, y1, z1}, {0,1,0}, {u1, v0}, 1.0f, light),
                        MakeVertex({x0, y1, z0}, {0,1,0}, {u0, v0}, 1.0f, light), true);
                    
                    PushQuad(result.vertices, result.indices,
                        MakeVertex({x0, y0, z1}, {0,1,0}, {u0, v1}, 1.0f, light),
                        MakeVertex({x1, y0, z0}, {0,1,0}, {u1, v1}, 1.0f, light),
                        MakeVertex({x1, y1, z0}, {0,1,0}, {u1, v0}, 1.0f, light),
                        MakeVertex({x0, y1, z1}, {0,1,0}, {u0, v0}, 1.0f, light), true);
                    continue;
                }

                // --- 2. 普通方块 ---
                for (int f = 0; f < 6; ++f) {
                    int nx = lx + faceNormals[f][0];
                    int ny = ly + faceNormals[f][1];
                    int nz = lz + faceNormals[f][2];
                    BlockType neighbor = getBlock(nx, ny, nz);

                    if (IsSolid(neighbor)) continue;
                    if (type == BlockType::Water && neighbor == BlockType::Water) continue;

                    int N_axis = (faceNormals[f][0] != 0) ? 0 : (faceNormals[f][1] != 0) ? 1 : 2;
                    int U_axis = (N_axis + 1) % 3;
                    int V_axis = (N_axis + 2) % 3;
                    glm::ivec3 N(faceNormals[f][0], faceNormals[f][1], faceNormals[f][2]);
                    
                    int tile = TileForBlock(type, f);
                    int tileRow = tile / ATLAS_COLS;
                    int tileCol = tile % ATLAS_COLS;
                    float u0 = static_cast<float>(tileCol) * TILE_UV;
                    float v0 = static_cast<float>(tileRow) * TILE_UV;

                    Eng::client::Vertex verts[4];

                    for (int v = 0; v < 4; ++v) {
                        float yPos = (float)ly + faceOffsets[f][v][1];
                        if (type == BlockType::Water && faceOffsets[f][v][1] == 1) yPos = (float)ly + 0.9f;

                        // AO 计算
                        int du = (faceOffsets[f][v][U_axis] == 0) ? -1 : +1;
                        int dv = (faceOffsets[f][v][V_axis] == 0) ? -1 : +1;
                        glm::ivec3 U(0), V(0);
                        U[U_axis] = du; V[V_axis] = dv;
                        int s1 = isSolidAt(lx + N.x + U.x, ly + N.y + U.y, lz + N.z + U.z) ? 1 : 0;
                        int s2 = isSolidAt(lx + N.x + V.x, ly + N.y + V.y, lz + N.z + V.z) ? 1 : 0;
                        int sc = isSolidAt(lx + N.x + U.x + V.x, ly + N.y + U.y + V.y, lz + N.z + U.z + V.z) ? 1 : 0;
                        float ao = (s1 && s2) ? 0.4f : 1.0f - 0.2f * static_cast<float>(s1 + s2 + sc);
                        if (type == BlockType::Water) ao = 1.0f;

                        // 平滑光照采样
                        glm::ivec3 pO(lx + N.x, ly + N.y, lz + N.z);
                        glm::vec4 sumL = getLightAt(pO.x, pO.y, pO.z) +
                                         getLightAt(pO.x + U.x, pO.y + U.y, pO.z + U.z) +
                                         getLightAt(pO.x + V.x, pO.y + V.y, pO.z + V.z) +
                                         getLightAt(pO.x + U.x + V.x, pO.y + U.y + V.y, pO.z + U.z + V.z);
                        glm::vec4 light = sumL / 4.0f;
                        if (type == BlockType::Water) light = glm::vec4(1.0f);

                        // ★ 完全照抄原版的 UV 计算
                        glm::vec2 uv = {
                            u0 + faceUVs[v][0] * TILE_UV,
                            v0 + (1.0f - faceUVs[v][1]) * TILE_UV
                        };

                        verts[v] = MakeVertex(
                            { baseX + lx + faceOffsets[f][v][0], yPos, baseZ + lz + faceOffsets[f][v][2] },
                            { (float)faceNormals[f][0], (float)faceNormals[f][1], (float)faceNormals[f][2] },
                            uv, ao, light
                        );
                    }
                    PushQuad(result.vertices, result.indices, verts[0], verts[1], verts[2], verts[3]);
                }
            }
        }
    }
}

} // namespace game::client