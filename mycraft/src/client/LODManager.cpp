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

        LodResult r;
        r.level = task.level;
        r.cx = task.cx;
        r.cz = task.cz;
        
        BuildLodUnit(terrain, task.level, task.cx, task.cz, r.vertices, r.indices, r.aabbMin, r.aabbMax);

        {
            std::lock_guard lk(m_resultMutex);
            m_resultQueue.push(std::move(r));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void LodManager::Update(const glm::vec3& cameraPos, [[maybe_unused]] const ClientWorld& world, int renderDistance, Eng::client::IRenderAPI* renderer) {
    if (!m_terrain) return;

    int pcx = static_cast<int>(std::floor(cameraPos.x)) >> 4;
    int pcz = static_cast<int>(std::floor(cameraPos.z)) >> 4;

    if (pcx != m_lastPcx || pcz != m_lastPcz || renderDistance != m_lastRenderDistance) {
        m_lastPcx = pcx;
        m_lastPcz = pcz;
        m_lastRenderDistance = renderDistance;

        m_wanted.clear(); // ★ 清空当前需要的列表
        struct Need { LodKey k; int d2; };
        std::vector<Need> need;

        for (int level = 1; level <= kLodLevels; ++level) {
            int unitChunks = 1 << level;
            int pu = pcx / unitChunks;
            int pv = pcz / unitChunks;
            int outerUnits = (renderDistance / unitChunks) + 6; // 稍微缩短外围范围，提高性能

            for (int dv = -outerUnits; dv <= outerUnits; ++dv) {
                for (int du = -outerUnits; du <= outerUnits; ++du) {
                    
                    int lodCxMin = (pu + du) * unitChunks;
                    int lodCxMax = lodCxMin + unitChunks;
                    int lodCzMin = (pv + dv) * unitChunks;
                    int lodCzMax = lodCzMin + unitChunks;

                    int rd = renderDistance;
                    // ★ 如果 LOD 的 AABB 碰到了真实地形范围，直接跳过
                    if (lodCxMin < pcx + rd && lodCxMax > pcx - rd &&
                        lodCzMin < pcz + rd && lodCzMax > pcz - rd) {
                        continue;
                    }

                    LodKey k{ (int8_t)level, pu + du, pv + dv };
                    m_wanted.insert(k); // ★ 记录下来
                    if (m_units.count(k)) continue;
                    if (m_pending.count(k)) continue;
                    
                    need.push_back({ k, du*du + dv*dv });
                }
            }
        }

        // 清理已经废弃的 LOD
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

    // 提交任务 (限制生成速度)
    constexpr int K_PER_FRAME = 2;
    int sent = 0;
    while (m_needCursor < m_needQueue.size() && sent < K_PER_FRAME) {
        LodKey k = m_needQueue[m_needCursor];
        if (m_units.count(k) || m_pending.count(k)) { ++m_needCursor; continue; }

        m_pending.insert(k);
        {
            std::lock_guard lk(m_taskMutex);
            m_taskQueue.push({ k.level, k.cx, k.cz });
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
        LodResult r;
        {
            std::lock_guard lk(m_resultMutex);
            if (m_resultQueue.empty() || done >= K) break;
            r = std::move(m_resultQueue.front());
            m_resultQueue.pop();
        }

        LodKey k{ static_cast<int8_t>(r.level), r.cx, r.cz };
        m_pending.erase(k);

        // 如果这个 LOD 不在当前 wanted 列表里，说明玩家已经飞走了，直接丢弃！
        if (!m_wanted.count(k)) continue;

        if (r.vertices.empty()) continue;

        Eng::client::MeshData data;
        data.vertices = std::move(r.vertices);
        data.indices  = std::move(r.indices);
        auto mesh = renderer->CreateMesh(data);

        LodUnit u;
        u.level   = r.level;
        u.cx      = r.cx;
        u.cz      = r.cz;
        u.mesh    = std::move(mesh);
        u.aabbMin = r.aabbMin;
        u.aabbMax = r.aabbMax;
        m_units[k] = std::move(u);
        ++done;
    }
}

void LodManager::CollectVisibleMeshes(const Frustum& frustum, std::vector<Eng::client::MeshPtr>& outMeshes) const {
    outMeshes.clear();
    outMeshes.reserve(m_units.size());
    for (const auto& [key, u] : m_units) {
        if (!u.mesh) continue;

        bool visible = true;
        for (const auto& p : frustum.planes) {
            glm::vec3 n = glm::vec3(p);
            float d = p.w;
            glm::vec3 v(n.x >= 0 ? u.aabbMax.x : u.aabbMin.x,
                        n.y >= 0 ? u.aabbMax.y : u.aabbMin.y,
                        n.z >= 0 ? u.aabbMax.z : u.aabbMin.z);
            if (glm::dot(n, v) + d < 0.0f) { visible = false; break; }
        }
        if (visible) outMeshes.push_back(u.mesh);
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
    m_lastPcx = INT_MIN;
    m_lastPcz = INT_MIN;
    m_terrain.reset();
}

void LodManager::BuildLodUnit(const game::server::TerrainGenerator& terrain,
                          int level, int ucx, int ucz,
                          std::vector<Eng::client::Vertex>& outV,
                          std::vector<uint32_t>& outI,
                          glm::vec3& outMin, glm::vec3& outMax) const {
    const int sampleStep = 1 << level;
    const int unitSize   = kLodUnitSize * sampleStep;
    constexpr int N  = kLodUnitSize;
    constexpr int NP = N + 1;

    const int baseX = ucx * unitSize;
    const int baseZ = ucz * unitSize;

    outV.clear(); outI.clear();
    outV.reserve(static_cast<size_t>(N) * N * 32);
    outI.reserve(static_cast<size_t>(N) * N * 48);
    outMin = glm::vec3(1e30f);
    outMax = glm::vec3(-1e30f);

    auto tileForBiome = [&](game::server::Biome bb, int y) -> int {
        using B = game::server::Biome;
        std::string tex = "grass_block_top";

        // ★ 修复 1：根据海拔高度强制修正群系（和服务端对齐）
        if (y > 175) bb = B::Snow;
        else if (y > 155) bb = B::Mountain;

        switch (bb) {
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

    static thread_local int     hP[NP][NP];
    static thread_local uint8_t bP[NP][NP];
    for (int j = 0; j < NP; ++j)
        for (int i = 0; i < NP; ++i) {
            int wx = baseX + i * sampleStep;
            int wz = baseZ + j * sampleStep;
            hP[j][i] = terrain.GetSurfaceY(wx, wz);
            bP[j][i] = (uint8_t)terrain.GetBiome(wx, wz);
        }

    auto cellY = [&](int i, int j) -> float {
        return (float)std::max(hP[j][i] + 1, SEA_LEVEL);
    };
    auto cellTile = [&](int i, int j) -> int {
        int h = hP[j][i];
        auto b = (game::server::Biome)bP[j][i];
        if (h < SEA_LEVEL) b = game::server::Biome::Ocean;
        return tileForBiome(b, h); // ★ 传入高度
    };
    
    auto pushV = [&](const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv, const glm::vec4& uvInfo) {
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

            float x0 = (float)(baseX + i * sampleStep);
            float x1 = (float)(baseX + (i + 1) * sampleStep);
            float z0 = (float)(baseZ + j * sampleStep);
            float z1 = (float)(baseZ + (j + 1) * sampleStep);

            // 1. 顶面
            {
                uint32_t base = (uint32_t)outV.size();
                pushV({x0, ySelf, z0}, {0,1,0}, {0.0f, stepF}, uvInfo);
                pushV({x1, ySelf, z0}, {0,1,0}, {stepF, stepF}, uvInfo);
                pushV({x1, ySelf, z1}, {0,1,0}, {stepF, 0.0f}, uvInfo);
                pushV({x0, ySelf, z1}, {0,1,0}, {0.0f, 0.0f}, uvInfo);
                outI.push_back(base+0); outI.push_back(base+3); outI.push_back(base+2);
                outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+1);
            }

            // 2. +X 侧面
            if (i + 1 < N) {
                float yNb = cellY(i + 1, j);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i + 1, j);
                    float nu0, nv0, nu1, nv1; getTileUV(nt, nu0, nv0, nu1, nv1);
                    glm::vec4 nUvInfo(1.0f, nu0, nv0, nu1 - nu0);
                    uint32_t base = (uint32_t)outV.size();
                    pushV({x1, yNb,   z0}, {1,0,0}, {0.0f, stepF}, nUvInfo);
                    pushV({x1, ySelf, z0}, {1,0,0}, {0.0f, 0.0f}, nUvInfo);
                    pushV({x1, ySelf, z1}, {1,0,0}, {stepF, 0.0f}, nUvInfo);
                    pushV({x1, yNb,   z1}, {1,0,0}, {stepF, stepF}, nUvInfo);
                    outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                }
            }

            // 3. -X 侧面
            if (i - 1 >= 0) {
                float yNb = cellY(i - 1, j);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i - 1, j);
                    float nu0, nv0, nu1, nv1; getTileUV(nt, nu0, nv0, nu1, nv1);
                    glm::vec4 nUvInfo(1.0f, nu0, nv0, nu1 - nu0);
                    uint32_t base = (uint32_t)outV.size();
                    pushV({x0, yNb,   z1}, {-1,0,0}, {0.0f, stepF}, nUvInfo);
                    pushV({x0, ySelf, z1}, {-1,0,0}, {0.0f, 0.0f}, nUvInfo);
                    pushV({x0, ySelf, z0}, {-1,0,0}, {stepF, 0.0f}, nUvInfo);
                    pushV({x0, yNb,   z0}, {-1,0,0}, {stepF, stepF}, nUvInfo);
                    outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                }
            }

            // 4. +Z 侧面 (前，法线朝 +Z)
            if (j + 1 < N) {
                float yNb = cellY(i, j + 1);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i, j + 1);
                    float nu0, nv0, nu1, nv1; getTileUV(nt, nu0, nv0, nu1, nv1);
                    glm::vec4 nUvInfo(1.0f, nu0, nv0, nu1 - nu0);
                    uint32_t base = (uint32_t)outV.size();
                    
                    // ★ 修复 2：修正了 +Z 面的绕序
                    pushV({x0, yNb,   z1}, {0,0,1}, {0.0f, stepF}, nUvInfo);
                    pushV({x0, ySelf, z1}, {0,0,1}, {0.0f, 0.0f}, nUvInfo);
                    pushV({x1, ySelf, z1}, {0,0,1}, {stepF, 0.0f}, nUvInfo);
                    pushV({x1, yNb,   z1}, {0,0,1}, {stepF, stepF}, nUvInfo);
                    
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+1);
                    outI.push_back(base+0); outI.push_back(base+3); outI.push_back(base+2);
                }
            }

            // 5. -Z 侧面 (后，法线朝 -Z)
            if (j - 1 >= 0) {
                float yNb = cellY(i, j - 1);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i, j - 1);
                    float nu0, nv0, nu1, nv1; getTileUV(nt, nu0, nv0, nu1, nv1);
                    glm::vec4 nUvInfo(1.0f, nu0, nv0, nu1 - nu0);
                    uint32_t base = (uint32_t)outV.size();
                    pushV({x1, yNb,   z0}, {0,0,-1}, {0.0f, stepF}, nUvInfo);
                    pushV({x1, ySelf, z0}, {0,0,-1}, {0.0f, 0.0f}, nUvInfo);
                    pushV({x0, ySelf, z0}, {0,0,-1}, {stepF, 0.0f}, nUvInfo);
                    pushV({x0, yNb,   z0}, {0,0,-1}, {stepF, stepF}, nUvInfo);
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
    const int baseX = key.cx * CELLS;
    const int baseZ = key.cz * CELLS;

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
    const game::client::ClientChunk* cs[2][2] = {
        { getChunk(key.cx*2+0, key.cz*2+0), getChunk(key.cx*2+0, key.cz*2+1) },
        { getChunk(key.cx*2+1, key.cz*2+0), getChunk(key.cx*2+1, key.cz*2+1) },
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