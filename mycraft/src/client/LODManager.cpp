#include "game/client/LODManager.h"
#include "game/client/TextureAtlas.h"

namespace game::client {

void LodManager::Start(uint32_t seed) {
    m_terrain = std::make_unique<game::server::TerrainGenerator>(seed);
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
    game::server::TerrainGenerator terrain(seed);
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
        
        // 目前 Worker 线程里只调用 BuildLodUnit，如果以后要用 BuildLod1FromChunks 再改
        BuildLodUnit(terrain, task.level, task.cx, task.cz, r.vertices, r.indices, r.aabbMin, r.aabbMax);

        {
            std::lock_guard lk(m_resultMutex);
            m_resultQueue.push(std::move(r));
        }
    }
}

void LodManager::Update(const glm::vec3& cameraPos, [[maybe_unused]] const ClientWorld& world, Eng::client::IRenderAPI* renderer) {
    if (!m_terrain) return;

    int pcx = static_cast<int>(std::floor(cameraPos.x)) >> 4;
    int pcz = static_cast<int>(std::floor(cameraPos.z)) >> 4;

    if (pcx != m_lastPcx || pcz != m_lastPcz) {
        m_lastPcx = pcx;
        m_lastPcz = pcz;

        std::unordered_set<LodKey, LodKeyHash> wanted;
        struct Need { LodKey k; int d2; };
        std::vector<Need> need;

        for (int level = 1; level <= kLodLevels; ++level) {
            int unitChunks = 1 << level;
            constexpr int kRingInnerUnits = 8;
            constexpr int kRingOuterUnits = 16;
            int pu = pcx / unitChunks;
            int pv = pcz / unitChunks;

            for (int dv = -kRingOuterUnits; dv <= kRingOuterUnits; ++dv) {
                for (int du = -kRingOuterUnits; du <= kRingOuterUnits; ++du) {
                    int adu = std::abs(du), adv = std::abs(dv);
                    if (adu < kRingInnerUnits && adv < kRingInnerUnits) continue;
                    LodKey k{ (int8_t)level, pu + du, pv + dv };
                    wanted.insert(k);
                    if (m_units.count(k))    continue;
                    if (m_pending.count(k))  continue;
                    need.push_back({ k, adu*adu + adv*adv });
                }
            }
        }

        // 卸载
        for (auto it = m_units.begin(); it != m_units.end(); ) {
            if (!wanted.count(it->first)) {
                if (it->second.mesh != 0) renderer->DestroyMesh(it->second.mesh);
                it = m_units.erase(it);
            } else ++it;
        }

        std::sort(need.begin(), need.end(), [](const Need& a, const Need& b){ return a.d2 < b.d2; });
        m_needQueue.clear();
        m_needQueue.reserve(need.size());
        for (auto& n : need) m_needQueue.push_back(n.k);
        m_needCursor = 0;
    }

    // 玩家静止判定
    const float moveDist = glm::length(cameraPos - m_lastCameraPos);
    if (moveDist < 0.02f) m_stationaryTime += 0.016f;
    else                  m_stationaryTime = 0.0f;
    m_lastCameraPos = cameraPos;
    const bool stationary = (m_stationaryTime > 1.0f);

    // 下发任务
    constexpr int K_PER_FRAME = 6;
    int sent = 0;
    while (m_needCursor < m_needQueue.size() && sent < K_PER_FRAME) {
        LodKey k = m_needQueue[m_needCursor];
        if (m_units.count(k) || m_pending.count(k)) { ++m_needCursor; continue; }
        if (k.level > 1 && !stationary) { ++m_needCursor; continue; }

        m_pending.insert(k);
        {
            std::lock_guard lk(m_taskMutex);
            m_taskQueue.push({ k.level, k.cx, k.cz });
        }
        m_taskCv.notify_one();
        ++m_needCursor;
        ++sent;
    }

    // 收集结果
    DrainResults(renderer);
}

void LodManager::DrainResults(Eng::client::IRenderAPI* renderer) {
    constexpr int K = 4;
    int done = 0;
    for (;;) {
        LodResult r;
        {
            std::lock_guard lk(m_resultMutex);
            if (m_resultQueue.empty() || done >= K) break;
            r = std::move(m_resultQueue.front());
            m_resultQueue.pop();
        }

        LodKey k{ r.level, r.cx, r.cz };
        m_pending.erase(k);

        if (r.vertices.empty()) continue;

        Eng::client::MeshData data;
        data.vertices = std::move(r.vertices);
        data.indices  = std::move(r.indices);
        auto mesh = renderer->CreateMesh(data);

        LodUnit u;
        u.level = r.level;
        u.cx = r.cx;
        u.cz = r.cz;
        u.mesh = mesh;
        u.aabbMin = r.aabbMin;
        u.aabbMax = r.aabbMax;
        m_units[k] = u;
        ++done;
    }
}

void LodManager::CollectVisibleMeshes(const Frustum& frustum, std::vector<Eng::client::MeshHandle>& outMeshes) const {
    outMeshes.clear();
    outMeshes.reserve(m_units.size());
    for (const auto& [key, u] : m_units) {
        if (u.mesh == 0) continue;
        
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

void LodManager::Clear(Eng::client::IRenderAPI* renderer) {
    if (renderer) {
        for (auto& [key, u] : m_units) {
            if (u.mesh != 0) renderer->DestroyMesh(u.mesh);
        }
    }
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

// ------------------ BuildLodUnit 原样搬运 ------------------
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
    outV.reserve(static_cast<size_t>(N) * N * 16);
    outI.reserve(static_cast<size_t>(N) * N * 24);
    outMin = glm::vec3(1e30f);
    outMax = glm::vec3(-1e30f);

    auto tileForBiome = [](game::server::Biome bb) -> int {
        using B = game::server::Biome;
        switch (bb) {
            case B::Ocean:    return 5;
            case B::Beach:
            case B::Desert:   return 4;
            case B::Snow:     return 6;
            case B::Mountain: return 0;
            case B::Forest:
            case B::Plains:
            default:          return 1;
        }
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
        return tileForBiome(b);
    };
    auto pushV = [&](const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv) {
        Eng::client::Vertex v;
        v.position = p; v.normal = n; v.uv = uv; v.ao = 1.0f;
        v.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        outV.push_back(v);
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    };

    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            float ySelf = cellY(i, j);
            int   tile  = cellTile(i, j);
            float u = ((tile % client::ATLAS_COLS) + 0.5f) * client::TILE_UV;
            float v = ((tile / static_cast<float>(client::ATLAS_COLS)) + 0.5f) * client::TILE_UV;

            float x0 = (float)(baseX + i * sampleStep);
            float x1 = (float)(baseX + (i + 1) * sampleStep);
            float z0 = (float)(baseZ + j * sampleStep);
            float z1 = (float)(baseZ + (j + 1) * sampleStep);

            {
                uint32_t base = (uint32_t)outV.size();
                pushV({x0, ySelf, z0}, {0,1,0}, {u,v});
                pushV({x1, ySelf, z0}, {0,1,0}, {u,v});
                pushV({x1, ySelf, z1}, {0,1,0}, {u,v});
                pushV({x0, ySelf, z1}, {0,1,0}, {u,v});
                outI.push_back(base+0); outI.push_back(base+3); outI.push_back(base+2);
                outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+1);
            }

            if (i + 1 < N) {
                float yNb = cellY(i + 1, j);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i + 1, j);
                    float nu = ((nt % client::ATLAS_COLS) + 0.5f) * client::TILE_UV;
                    float nv = ((nt / static_cast<float>(client::ATLAS_COLS)) + 0.5f) * client::TILE_UV;
                    uint32_t base = (uint32_t)outV.size();
                    pushV({x1, yNb,   z0}, {1,0,0}, {nu,nv});
                    pushV({x1, ySelf, z0}, {1,0,0}, {nu,nv});
                    pushV({x1, ySelf, z1}, {1,0,0}, {nu,nv});
                    pushV({x1, yNb,   z1}, {1,0,0}, {nu,nv});
                    outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                }
            }

            if (j + 1 < N) {
                float yNb = cellY(i, j + 1);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i, j + 1);
                    float nu = ((nt % client::ATLAS_COLS) + 0.5f) * client::TILE_UV;
                    float nv = ((nt / static_cast<float>(client::ATLAS_COLS)) + 0.5f) * client::TILE_UV;
                    uint32_t base = (uint32_t)outV.size();
                    pushV({x0, yNb,   z1}, {0,0,1}, {nu,nv});
                    pushV({x1, yNb,   z1}, {0,0,1}, {nu,nv});
                    pushV({x1, ySelf, z1}, {0,0,1}, {nu,nv});
                    pushV({x0, ySelf, z1}, {0,0,1}, {nu,nv});
                    outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                }
            }
        }
    }
}

// ------------------ BuildLod1FromChunks 重构版 ------------------
void LodManager::BuildLod1FromChunks(const LodKey& key, const ClientWorld& world,
                                     std::vector<Eng::client::Vertex>& outV,
                                     std::vector<uint32_t>& outI,
                                     glm::vec3& outMin, glm::vec3& outMax) {
    constexpr int CELLS = 32;
    const int baseX = key.cx * CELLS;
    const int baseZ = key.cz * CELLS;

    // 1. 清理传入的容器（不要重新定义 outV 和 outI）
    outV.clear();
    outI.clear();
    outV.reserve(static_cast<size_t>(CELLS) * CELLS * 12);
    outI.reserve(static_cast<size_t>(CELLS) * CELLS * 18);
    outMin = glm::vec3(1e30f);
    outMax = glm::vec3(-1e30f);

    // 2. m_world 变成了外部传入的 world
    auto getChunk = [&world](int cx, int cz) -> const game::client::ClientChunk* {
        auto it = world.GetChunks().find(ChunkKey(cx, cz));
        return it == world.GetChunks().end() ? nullptr : &it->second;
    };
    const game::client::ClientChunk* cs[2][2] = {
        { getChunk(key.cx*2+0, key.cz*2+0), getChunk(key.cx*2+0, key.cz*2+1) },
        { getChunk(key.cx*2+1, key.cz*2+0), getChunk(key.cx*2+1, key.cz*2+1) },
    };

    auto blockToTile = [](uint8_t b) -> int {
        using BT = BlockType;
        switch ((BT)b) {
            case BT::Stone:      return 0;
            case BT::GrassBlock: return 1;
            case BT::Dirt:       return 3;
            case BT::Sand:       return 4;
            case BT::Water:      return 5;
            case BT::Snow:       return 6;
            case BT::Sandstone:  return 8;
            case BT::CoalOre:    return 9;
            case BT::IronOre:    return 10;
            case BT::Wood:       return 11;
            case BT::Leaves:     return 12;
            default:             return 1;
        }
    };

    auto sample = [&](int i, int j, float& y, int& tile) {
        int cxi = i >> 4, czi = j >> 4;
        int lx  = i & 15, lz  = j & 15;
        const game::client::ClientChunk* cc = cs[cxi][czi];

        int h;
        uint8_t tb;
        if (cc) {
            h  = cc->heights[lx * 16 + lz];
            tb = cc->topBlock[lx * 16 + lz];
        } else {
            int wx = baseX + i, wz = baseZ + j;
            // 3. m_lodTerrain 变成了 m_terrain
            h = m_terrain ? m_terrain->GetSurfaceY(wx, wz) : SEA_LEVEL;
            auto b = m_terrain ? m_terrain->GetBiome(wx, wz)
                               : game::server::Biome::Plains;
            using B = game::server::Biome;
            switch (b) {
                case B::Ocean:    tb = (uint8_t)BlockType::Water;      break;
                case B::Beach:
                case B::Desert:   tb = (uint8_t)BlockType::Sand;       break;
                case B::Snow:     tb = (uint8_t)BlockType::Snow;       break;
                case B::Mountain: tb = (uint8_t)BlockType::Stone;      break;
                default:          tb = (uint8_t)BlockType::GrassBlock; break;
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
        // 4. aabbMin / aabbMax 变成了 outMin / outMax
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    };

    auto tileUV = [&](int t, float& u, float& v) {
        u = ((t % client::ATLAS_COLS) + 0.5f) * client::TILE_UV;
        v = ((t / static_cast<float>(client::ATLAS_COLS)) + 0.5f) * client::TILE_UV;
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

    // 5. ★ 极其重要：删掉原版代码末尾的这段！
    // 因为这是 Worker 线程函数，不能直接调 renderer->CreateMesh，也不能直接 m_lodUnits[key] = u;
    // 这里只需要把 outV, outI, outMin, outMax 填好，上层代码会自动拿去创建 Mesh 的！
}

} // namespace game::client