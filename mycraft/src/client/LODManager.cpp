#include "game/client/LODManager.h"
#include "game/client/TextureAtlas.h"
#include <algorithm>
#include <cmath>
#include <climits>

namespace game::client {

void LodManager::Start(uint32_t seed) {
    if (!m_registry) return; // 防御性检查

    m_terrain = std::make_unique<game::server::TerrainGenerator>(seed, *m_registry);
    m_stop = false;
    unsigned hw = std::thread::hardware_concurrency();
    unsigned n  = std::clamp(hw > 2 ? hw - 2 : 2u, 2u, 6u);
    for (unsigned i = 0; i < n; ++i) {
        m_workers.emplace_back([this, seed] { WorkerLoop(seed); });
    }
}

void LodManager::Stop() {
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

void LodManager::WorkerLoop(uint32_t seed) {
    game::server::TerrainGenerator terrain(seed, *m_registry);
    while (true) {
        LodTask task;
        {
            std::unique_lock lk(m_taskMutex);
            m_taskCv.wait(lk, [this] { return m_stop.load() || !m_taskQueue.empty(); });
            if (m_stop.load()) return;
            task = m_taskQueue.front();
            m_taskQueue.pop();
        }

        LodResult result;
        result.level = task.level;
        result.unitX = task.unitX;
        result.unitZ = task.unitZ;
        
        BuildLodUnit(terrain, task.level, task.unitX, task.unitZ, result.vertices, result.indices, result.aabbMin, result.aabbMax);

        {
            std::lock_guard lk(m_resultMutex);
            m_resultQueue.push(std::move(result));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void LodManager::Update(const glm::vec3& cameraPos, [[maybe_unused]] const ClientWorld& world, int renderDistance, Eng::client::IRenderAPI* renderer) {
    if (!m_terrain) return;

    int playerChunkX = static_cast<int>(std::floor(cameraPos.x)) >> 4;
    int playerChunkZ = static_cast<int>(std::floor(cameraPos.z)) >> 4;

    if (playerChunkX != m_lastPlayerChunkX || playerChunkZ != m_lastPlayerChunkZ || renderDistance != m_lastRenderDistance) {
        m_lastPlayerChunkX = playerChunkX;
        m_lastPlayerChunkZ = playerChunkZ;
        m_lastRenderDistance = renderDistance;

        // 原生渲染的绝对区块范围 [min, max] (闭区间)
        int nativeMinChunkX = playerChunkX - renderDistance;
        int nativeMaxChunkX = playerChunkX + renderDistance;
        int nativeMinChunkZ = playerChunkZ - renderDistance;
        int nativeMaxChunkZ = playerChunkZ + renderDistance;

        m_wanted.clear();
        struct Need { LodKey k; int d2; };
        std::vector<Need> need;

        for (int level = 1; level <= kLodLevels; ++level) {
            int sampleStep = 1 << level;
            // 当前 LOD 块覆盖了多少个区块
            int chunkWidth = (kLodUnitSize * sampleStep) / 16;

            // 算出原生边缘对应的 LOD 网格索引
            int minUnitX = nativeMinChunkX / chunkWidth;
            int maxUnitX = nativeMaxChunkX / chunkWidth;
            int minUnitZ = nativeMinChunkZ / chunkWidth;
            int maxUnitZ = nativeMaxChunkZ / chunkWidth;

            int padding = 4; 

            for (int unitX = minUnitX - padding; unitX <= maxUnitX + padding; ++unitX) {
                for (int unitZ = minUnitZ - padding; unitZ <= maxUnitZ + padding; ++unitZ) {
                    
                    // 算出当前 LOD 块的绝对区块范围 [min, max) (左闭右开)
                    int lodMinChunkX = unitX * chunkWidth;
                    int lodMaxChunkX = lodMinChunkX + chunkWidth;
                    int lodMinChunkZ = unitZ * chunkWidth;
                    int lodMaxChunkZ = lodMinChunkZ + chunkWidth;

                    // 严格 AABB 碰撞：如果 LOD 块和原生渲染范围有一丝一毫的重叠，直接跳过
                    if (lodMaxChunkX > nativeMinChunkX && lodMinChunkX <= nativeMaxChunkX &&
                        lodMaxChunkZ > nativeMinChunkZ && lodMinChunkZ <= nativeMaxChunkZ) {
                        continue; 
                    }

                    LodKey key{ (int8_t)level, unitX, unitZ };
                    m_wanted.insert(key);
                    if (m_units.count(key)) continue;
                    if (m_pending.count(key)) continue;

                    int centerUnitX = playerChunkX / chunkWidth;
                    int centerUnitZ = playerChunkZ / chunkWidth;
                    int dx = unitX - centerUnitX;
                    int dz = unitZ - centerUnitZ;
                    need.push_back({ key, dx*dx + dz*dz });
                }
            }
        }

        // 清理废弃的 LOD
        for (auto it = m_units.begin(); it != m_units.end(); ) {
            if (!m_wanted.count(it->first)) {
                it = m_units.erase(it);
            } else ++it;
        }

        std::sort(need.begin(), need.end(), [](const Need& a, const Need& b){ return a.d2 < b.d2; });
        m_needQueue.clear();
        m_needQueue.reserve(need.size());
        for (auto& n : need) m_needQueue.push_back(n.k);
        m_needCursor = 0;
    }

    // 提交任务（一帧只发 2 个，防止卡顿）
    constexpr int K_PER_FRAME = 2;
    int sent = 0;
    while (m_needCursor < m_needQueue.size() && sent < K_PER_FRAME) {
        LodKey key = m_needQueue[m_needCursor];
        if (m_units.count(key) || m_pending.count(key)) { ++m_needCursor; continue; }

        m_pending.insert(key);
        {
            std::lock_guard lk(m_taskMutex);
            m_taskQueue.push({ key.level, key.unitX, key.unitZ });
        }
        m_taskCv.notify_one();
        ++m_needCursor;
        ++sent;
    }

    DrainResults(renderer);
}

void LodManager::DrainResults(Eng::client::IRenderAPI* renderer) {
    constexpr int K = 4; // 防止主线程卡顿
    int done = 0;
    for (;;) {
        LodResult result;
        {
            std::lock_guard lk(m_resultMutex);
            if (m_resultQueue.empty() || done >= K) break;
            result = std::move(m_resultQueue.front());
            m_resultQueue.pop();
        }

        LodKey key{ static_cast<int8_t>(result.level), result.unitX, result.unitZ };
        m_pending.erase(key);

        // 如果这个 LOD 不在当前 wanted 列表里，说明玩家已经飞走了，直接丢弃！
        if (!m_wanted.count(key)) continue;

        if (result.vertices.empty()) continue;

        Eng::client::MeshData data;
        data.vertices = std::move(result.vertices);
        data.indices  = std::move(result.indices);
        auto mesh = renderer->CreateMesh(data);

        LodUnit unit;
        unit.level   = result.level;
        unit.unitX   = result.unitX;
        unit.unitZ   = result.unitZ;
        unit.mesh    = std::move(mesh);
        unit.aabbMin = result.aabbMin;
        unit.aabbMax = result.aabbMax;
        m_units[key] = std::move(unit);
        ++done;
    }
}

void LodManager::CollectVisibleMeshes(const Frustum& frustum, std::vector<Eng::client::MeshPtr>& outMeshes) const {
    outMeshes.clear();
    outMeshes.reserve(m_units.size());
    for (const auto& [key, unit] : m_units) {
        if (!unit.mesh) continue;

        bool visible = true;
        for (const auto& p : frustum.planes) {
            glm::vec3 n = glm::vec3(p);
            float d = p.w;
            glm::vec3 v(n.x >= 0 ? unit.aabbMax.x : unit.aabbMin.x,
                        n.y >= 0 ? unit.aabbMax.y : unit.aabbMin.y,
                        n.z >= 0 ? unit.aabbMax.z : unit.aabbMin.z);
            if (glm::dot(n, v) + d < 0.0f) { visible = false; break; }
        }
        if (visible) outMeshes.push_back(unit.mesh);
    }
}

void LodManager::Clear() {
    m_units.clear();
    m_pending.clear();
    {
        std::lock_guard lk(m_resultMutex);
        while (!m_resultQueue.empty()) m_resultQueue.pop();
    }
    m_needQueue.clear();
    m_needCursor = 0;
    m_lastPlayerChunkX = INT_MIN;
    m_lastPlayerChunkZ = INT_MIN;
    m_terrain.reset();
}

void LodManager::BuildLodUnit(const game::server::TerrainGenerator& terrain,
                          int level, int unitX, int unitZ,
                          std::vector<Eng::client::Vertex>& outV,
                          std::vector<uint32_t>& outI,
                          glm::vec3& outMin, glm::vec3& outMax) const {
    int sampleStep = 1 << level;
    // ★ 必须和 Update 里的 chunkWidth 公式一模一样
    int chunkWidth = (kLodUnitSize * sampleStep) / 16;
    
    // 算出绝对方块起始坐标
    int baseChunkX = unitX * chunkWidth;
    int baseChunkZ = unitZ * chunkWidth;
    int baseBlockX = baseChunkX * 16;
    int baseBlockZ = baseChunkZ * 16;

    constexpr int N  = kLodUnitSize;
    constexpr int NP = N + 1;

    outV.clear(); outI.clear();
    outV.reserve(static_cast<size_t>(N) * N * 32);
    outI.reserve(static_cast<size_t>(N) * N * 48);
    outMin = glm::vec3(1e30f);
    outMax = glm::vec3(-1e30f);

    auto tileForBiome = [&](game::server::Biome biome, int y) -> int {
        using B = game::server::Biome;
        std::string tex = "grass_block_top";

        // ★ 根据海拔高度强制修正群系（和服务端对齐）
        if (y > 175) biome = B::Snow;
        else if (y > 155) biome = B::Mountain;

        switch (biome) {
            case B::Ocean:    tex = "water_still"; break;
            case B::Beach:
            case B::Desert:   tex = "sand"; break;
            case B::Snow:     tex = "snow"; break;
            case B::Mountain: tex = "stone"; break;
            default:          tex = "grass_block_top"; break;
        }
        int t = ResolveTile(tex);
        return t < 0 ? 0 : t;
    };

    static thread_local int     heightMap[NP][NP];
    static thread_local uint8_t biomeMap[NP][NP];
    for (int j = 0; j < NP; ++j)
        for (int i = 0; i < NP; ++i) {
            int worldBlockX = baseBlockX + i * sampleStep;
            int worldBlockZ = baseBlockZ + j * sampleStep;
            heightMap[j][i] = terrain.GetSurfaceY(worldBlockX, worldBlockZ);
            biomeMap[j][i] = (uint8_t)terrain.GetBiome(worldBlockX, worldBlockZ);
        }

    auto cellY = [&](int i, int j) -> float {
        return (float)std::max(heightMap[j][i] + 1, SEA_LEVEL);
    };
    auto cellTile = [&](int i, int j) -> int {
        int h = heightMap[j][i];
        auto b = (game::server::Biome)biomeMap[j][i];
        if (h < SEA_LEVEL) b = game::server::Biome::Ocean;
        return tileForBiome(b, h);
    };
    
    auto pushVertex = [&](const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv, const glm::vec4& uvInfo) {
        Eng::client::Vertex v;
        v.position = p; v.normal = n; v.uv = uv; v.ao = 1.0f;
        v.light = uvInfo; 
        outV.push_back(v);
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    };

    const float atlasCols = static_cast<float>(AtlasCols());
    
    auto getTileUV = [&](int tile, float& u0, float& v0, float& u1, float& v1) {
        const float pad = 0.005f; 
        int col = tile % static_cast<int>(atlasCols);
        int row = tile / static_cast<int>(atlasCols);
        u0 = (col / atlasCols) + pad;
        v0 = (row / atlasCols) + pad;
        u1 = ((col + 1) / atlasCols) - pad;
        v1 = ((row + 1) / atlasCols) - pad;
    };

    float stepF = static_cast<float>(sampleStep);

    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            float ySelf = cellY(i, j);
            int   tile  = cellTile(i, j);
            
            float u0, v0, u1, v1;
            getTileUV(tile, u0, v0, u1, v1);
            
            glm::vec4 uvInfo(1.0f, u0, v0, u1 - u0);

            float x0 = (float)(baseBlockX + i * sampleStep);
            float x1 = (float)(baseBlockX + (i + 1) * sampleStep);
            float z0 = (float)(baseBlockZ + j * sampleStep);
            float z1 = (float)(baseBlockZ + (j + 1) * sampleStep);

            // 1. 顶面
            {
                uint32_t base = (uint32_t)outV.size();
                pushVertex({x0, ySelf, z0}, {0,1,0}, {0.0f, stepF}, uvInfo);
                pushVertex({x1, ySelf, z0}, {0,1,0}, {stepF, stepF}, uvInfo);
                pushVertex({x1, ySelf, z1}, {0,1,0}, {stepF, 0.0f}, uvInfo);
                pushVertex({x0, ySelf, z1}, {0,1,0}, {0.0f, 0.0f}, uvInfo);
                outI.push_back(base+0); outI.push_back(base+3); outI.push_back(base+2);
                outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+1);
            }

            // 2. +X 侧面 (去掉 i + 1 < N 判断，因为 heightMap 已经存了边缘)
            {
                float yNb = cellY(i + 1, j);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i + 1, j);
                    float nu0, nv0, nu1, nv1; getTileUV(nt, nu0, nv0, nu1, nv1);
                    glm::vec4 nUvInfo(1.0f, nu0, nv0, nu1 - nu0);
                    uint32_t base = (uint32_t)outV.size();
                    pushVertex({x1, yNb,   z0}, {1,0,0}, {0.0f, stepF}, nUvInfo);
                    pushVertex({x1, ySelf, z0}, {1,0,0}, {0.0f, 0.0f}, nUvInfo);
                    pushVertex({x1, ySelf, z1}, {1,0,0}, {stepF, 0.0f}, nUvInfo);
                    pushVertex({x1, yNb,   z1}, {1,0,0}, {stepF, stepF}, nUvInfo);
                    outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                }
            }

            // 3. -X 侧面 (去掉 i - 1 >= 0，交给高度图处理)
            {
                float yNb = cellY(i - 1, j);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i - 1, j);
                    float nu0, nv0, nu1, nv1; getTileUV(nt, nu0, nv0, nu1, nv1);
                    glm::vec4 nUvInfo(1.0f, nu0, nv0, nu1 - nu0);
                    uint32_t base = (uint32_t)outV.size();
                    pushVertex({x0, yNb,   z1}, {-1,0,0}, {0.0f, stepF}, nUvInfo);
                    pushVertex({x0, ySelf, z1}, {-1,0,0}, {0.0f, 0.0f}, nUvInfo);
                    pushVertex({x0, ySelf, z0}, {-1,0,0}, {stepF, 0.0f}, nUvInfo);
                    pushVertex({x0, yNb,   z0}, {-1,0,0}, {stepF, stepF}, nUvInfo);
                    outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                }
            }

            // 4. +Z 侧面 (去掉 j + 1 < N)
            {
                float yNb = cellY(i, j + 1);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i, j + 1);
                    float nu0, nv0, nu1, nv1; getTileUV(nt, nu0, nv0, nu1, nv1);
                    glm::vec4 nUvInfo(1.0f, nu0, nv0, nu1 - nu0);
                    uint32_t base = (uint32_t)outV.size();
                    pushVertex({x0, yNb,   z1}, {0,0,1}, {0.0f, stepF}, nUvInfo);
                    pushVertex({x0, ySelf, z1}, {0,0,1}, {0.0f, 0.0f}, nUvInfo);
                    pushVertex({x1, ySelf, z1}, {0,0,1}, {stepF, 0.0f}, nUvInfo);
                    pushVertex({x1, yNb,   z1}, {0,0,1}, {stepF, stepF}, nUvInfo);
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+1);
                    outI.push_back(base+0); outI.push_back(base+3); outI.push_back(base+2);
                }
            }

            // 5. -Z 侧面
            {
                float yNb = cellY(i, j - 1);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i, j - 1);
                    float nu0, nv0, nu1, nv1; getTileUV(nt, nu0, nv0, nu1, nv1);
                    glm::vec4 nUvInfo(1.0f, nu0, nv0, nu1 - nu0);
                    uint32_t base = (uint32_t)outV.size();
                    pushVertex({x1, yNb,   z0}, {0,0,-1}, {0.0f, stepF}, nUvInfo);
                    pushVertex({x1, ySelf, z0}, {0,0,-1}, {0.0f, 0.0f}, nUvInfo);
                    pushVertex({x0, ySelf, z0}, {0,0,-1}, {stepF, 0.0f}, nUvInfo);
                    pushVertex({x0, yNb,   z0}, {0,0,-1}, {stepF, stepF}, nUvInfo);
                    outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                }
            }
        }
    }
}

// ------------------ BuildLod1FromChunks ------------------
void LodManager::BuildLod1FromChunks(const LodKey& key, const ClientWorld& world,
                                     std::vector<Eng::client::Vertex>& outV,
                                     std::vector<uint32_t>& outI,
                                     glm::vec3& outMin, glm::vec3& outMax) {
    constexpr int CELLS = 32;
    // 这里改用 unitX 和 unitZ
    const int baseX = key.unitX * CELLS;
    const int baseZ = key.unitZ * CELLS;

    outV.clear();
    outI.clear();
    outV.reserve(static_cast<size_t>(CELLS) * CELLS * 12);
    outI.reserve(static_cast<size_t>(CELLS) * CELLS * 18);
    outMin = glm::vec3(1e30f);
    outMax = glm::vec3(-1e30f);

    auto getChunk = [&world](int cx, int cz) -> const game::client::ClientChunk* {
        auto it = world.GetChunks().find(ChunkKey(cx, cz));
        return it == world.GetChunks().end() ? nullptr : &it->second;
    };
    // 注意这里也换成了 unitX / unitZ
    const game::client::ClientChunk* cs[2][2] = {
        { getChunk(key.unitX*2+0, key.unitZ*2+0), getChunk(key.unitX*2+0, key.unitZ*2+1) },
        { getChunk(key.unitX*2+1, key.unitZ*2+0), getChunk(key.unitX*2+1, key.unitZ*2+1) },
    };

    auto blockToTile = [&](BlockId b) -> int {
        if (b == AIR_ID) return 0;
        const auto& def = m_registry->Get(b);
        std::string tile = def.render.isCross ? def.render.ForSide().tile : def.render.ForTop().tile;
        if (tile.empty()) return 0;
        int t = ResolveTile(tile);
        return t < 0 ? 0 : t;
    };

    auto sample = [&](int i, int j, float& y, int& tile) {
        int cxi = i >> 4, czi = j >> 4;
        int lx  = i & 15, lz  = j & 15;
        const game::client::ClientChunk* cc = cs[cxi][czi];

        int h;
        BlockId tb;
        if (cc) {
            h  = cc->heights[lx * 16 + lz];
            tb = cc->topBlock[lx * 16 + lz];
        } else {
            int wx = baseX + i, wz = baseZ + j;
            h = m_terrain ? m_terrain->GetSurfaceY(wx, wz) : SEA_LEVEL;
            auto b = m_terrain ? m_terrain->GetBiome(wx, wz)
                               : game::server::Biome::Plains;
            using B = game::server::Biome;
            switch (b) {
                case B::Ocean:    tb = m_registry->Get("mycraft:water").id;       break;
                case B::Beach:
                case B::Desert:   tb = m_registry->Get("mycraft:sand").id;        break;
                case B::Snow:     tb = m_registry->Get("mycraft:snow").id;        break;
                case B::Mountain: tb = m_registry->Get("mycraft:stone").id;       break;
                default:          tb = m_registry->Get("mycraft:grass_block").id; break;
            }
        }

        y = std::max<float>((float)(h + 1), (float)SEA_LEVEL);
        tile = blockToTile(tb);
    };

    static thread_local float hM[CELLS][CELLS];
    static thread_local int   tM[CELLS][CELLS];
    for (int j = 0; j < CELLS; ++j)
        for (int i = 0; i < CELLS; ++i)
            sample(i, j, hM[j][i], tM[j][i]);

    auto pushV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) {
        Eng::client::Vertex v;
        v.position = p; v.normal = n; v.uv = uv; v.ao = 1.0f;
        v.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        outV.push_back(v);
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    };

    const float atlasTileUV = AtlasTileUV();
    const int   atlasCols   = AtlasCols();

    auto tileUV = [&](int t, float& u, float& v) {
        u = ((t % atlasCols) + 0.5f) * atlasTileUV;
        v = ((t / static_cast<float>(atlasCols)) + 0.5f) * atlasTileUV;
    };

    for (int j = 0; j < CELLS; ++j) {
        for (int i = 0; i < CELLS; ++i) {
            float ySelf = hM[j][i];
            float u, v; tileUV(tM[j][i], u, v);

            float x0 = (float)(baseX + i);
            float x1 = x0 + 1.0f;
            float z0 = (float)(baseZ + j);
            float z1 = z0 + 1.0f;

            {
                uint32_t b = (uint32_t)outV.size();
                pushV({x0, ySelf, z0}, {0,1,0}, {u,v});
                pushV({x1, ySelf, z0}, {0,1,0}, {u,v});
                pushV({x1, ySelf, z1}, {0,1,0}, {u,v});
                pushV({x0, ySelf, z1}, {0,1,0}, {u,v});
                outI.push_back(b+0); outI.push_back(b+3); outI.push_back(b+2);
                outI.push_back(b+0); outI.push_back(b+2); outI.push_back(b+1);
            }

            if (i + 1 < CELLS) {
                float yNb = hM[j][i+1];
                if (ySelf > yNb + 0.01f) {
                    float nu, nv; tileUV(tM[j][i+1], nu, nv);
                    uint32_t b = (uint32_t)outV.size();
                    pushV({x1, yNb,   z0}, {1,0,0}, {nu,nv});
                    pushV({x1, ySelf, z0}, {1,0,0}, {nu,nv});
                    pushV({x1, ySelf, z1}, {1,0,0}, {nu,nv});
                    pushV({x1, yNb,   z1}, {1,0,0}, {nu,nv});
                    outI.push_back(b+0); outI.push_back(b+1); outI.push_back(b+2);
                    outI.push_back(b+0); outI.push_back(b+2); outI.push_back(b+3);
                }
            }
            if (j + 1 < CELLS) {
                float yNb = hM[j+1][i];
                if (ySelf > yNb + 0.01f) {
                    float nu, nv; tileUV(tM[j+1][i], nu, nv);
                    uint32_t b = (uint32_t)outV.size();
                    pushV({x0, yNb,   z1}, {0,0,1}, {nu,nv});
                    pushV({x1, yNb,   z1}, {0,0,1}, {nu,nv});
                    pushV({x1, ySelf, z1}, {0,0,1}, {nu,nv});
                    pushV({x0, ySelf, z1}, {0,0,1}, {nu,nv});
                    outI.push_back(b+0); outI.push_back(b+1); outI.push_back(b+2);
                    outI.push_back(b+0); outI.push_back(b+2); outI.push_back(b+3);
                }
            }
        }
    }
}

} // namespace game::client