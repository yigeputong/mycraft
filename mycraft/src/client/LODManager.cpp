#include "game/client/LODManager.h"
#include "game/client/TextureAtlas.h"
#include "Engine/tool/Platform.h"
#include <algorithm>
#include <cmath>
#include <climits>

namespace game::client {

void LodManager::Start(uint32_t seed) {
    if (!m_registry) return;
    m_terrain = std::make_unique<game::server::TerrainGenerator>(seed, *m_registry);
    
    // 根据平台动态分配线程，最多 4 个
    uint32_t hw = Eng::tool::GetRecommendedWorkerThreads();
    unsigned n = std::clamp(hw / 2, 1u, 4u); 

    for (unsigned i = 0; i < n; ++i) {
        m_workers.push_back(std::make_unique<Eng::tool::RAIIThread>(
            [this, seed](const std::stop_token& st) { WorkerLoop(seed, st); }
        ));
    }
}

void LodManager::Stop() {
    if (m_workers.empty()) return;
    for (auto& w : m_workers) w->RequestStop();
    
    { std::lock_guard lk(m_taskMutex); while (!m_taskQueue.empty()) m_taskQueue.pop(); }
    m_taskCv.notify_all();
    
    { std::lock_guard lk(m_resultMutex); while (!m_resultQueue.empty()) m_resultQueue.pop(); }
    m_resultCv.notify_all();
    
    m_workers.clear();
}

void LodManager::WorkerLoop(uint32_t seed, const std::stop_token& st) {
    game::server::TerrainGenerator terrain(seed, *m_registry);
    
    while (!st.stop_requested()) {
        LodTask task;
        {
            std::unique_lock lk(m_taskMutex);
            m_taskCv.wait(lk, [this, &st] { return st.stop_requested() || !m_taskQueue.empty(); });
            if (st.stop_requested() && m_taskQueue.empty()) return;
            if (m_taskQueue.empty()) continue;
            task = m_taskQueue.front();
            m_taskQueue.pop();
        }

        LodResult result;
        result.level = task.level;
        result.unitX = task.unitX;
        result.unitZ = task.unitZ;
        BuildLodUnit(terrain, task.level, task.unitX, task.unitZ, result.vertices, result.indices, result.aabbMin, result.aabbMax);

        {
            std::unique_lock lk(m_resultMutex);
            // 背压：结果队列超过 128 个，线程暂停等待消费
            m_resultCv.wait(lk, [this, &st] { return st.stop_requested() || m_resultQueue.size() < 128; });
            if (st.stop_requested()) return;
            m_resultQueue.push(std::move(result));
        }
    }
}

void LodManager::Update(const glm::vec3& cameraPos, [[maybe_unused]] const ClientWorld& world, 
                        int renderDistance, int lodDistance, Eng::service::IRenderService* renderer) {
    if (!m_terrain) return;

    int playerChunkX = static_cast<int>(std::floor(cameraPos.x)) >> 4;
    int playerChunkZ = static_cast<int>(std::floor(cameraPos.z)) >> 4;

    if (playerChunkX != m_lastPlayerChunkX || playerChunkZ != m_lastPlayerChunkZ || 
        renderDistance != m_lastRenderDistance || lodDistance != m_lastLodDistance ||
        !m_dirtyLods.empty()) {
        
        m_lastPlayerChunkX = playerChunkX;
        m_lastPlayerChunkZ = playerChunkZ;
        m_lastRenderDistance = renderDistance;
        m_lastLodDistance = lodDistance;

        m_wanted.clear();
        struct Need { LodKey k; int d2; };
        std::vector<Need> need;

        // 原生渲染区域的绝对边界
        int nativeMinX = playerChunkX - renderDistance;
        int nativeMaxX = playerChunkX + renderDistance;
        int nativeMinZ = playerChunkZ - renderDistance;
        int nativeMaxZ = playerChunkZ + renderDistance;

        int maxN = 0;
        if (lodDistance > 0) {
            maxN = 1;
            while ((renderDistance * (1 << maxN)) < lodDistance) maxN++;
        }

        int currentRadius = renderDistance; // 上一级的半径，初始为原生视距
        for (int level = 1; level <= maxN; ++level) {
            int sampleStep = 1 << level;
            int chunkWidth = (kLodUnitSize * sampleStep) / 16; 

            int nextRadius = renderDistance * (1 << level);
            if (nextRadius > lodDistance) nextRadius = lodDistance;

            int minUnitX = (playerChunkX - nextRadius) / chunkWidth - 1;
            int maxUnitX = (playerChunkX + nextRadius) / chunkWidth + 1;
            int minUnitZ = (playerChunkZ - nextRadius) / chunkWidth - 1;
            int maxUnitZ = (playerChunkZ + nextRadius) / chunkWidth + 1;

            int centerUnitX = playerChunkX / chunkWidth;
            int centerUnitZ = playerChunkZ / chunkWidth;

            for (int unitX = minUnitX; unitX <= maxUnitX; ++unitX) {
                for (int unitZ = minUnitZ; unitZ <= maxUnitZ; ++unitZ) {
                    
                    int minCx = unitX * chunkWidth;
                    int maxCx = minCx + chunkWidth;
                    int minCz = unitZ * chunkWidth;
                    int maxCz = minCz + chunkWidth;

                    // 1. 跳过完全在原生区块内部的 LOD
                    if (minCx >= nativeMinX && maxCx <= nativeMaxX &&
                        minCz >= nativeMinZ && maxCz <= nativeMaxZ) {
                        continue;
                    }

                    // 2. 计算当前 LOD 块的中心点距离
                    int cx = unitX * chunkWidth + chunkWidth / 2;
                    int cz = unitZ * chunkWidth + chunkWidth / 2;
                    int distToCenter = std::max(std::abs(cx - playerChunkX), std::abs(cz - playerChunkZ));
                    
                    // 3. 核心环形裁剪：如果这个块完全被上一级包住了，跳过
                    if (level > 1 && distToCenter + (chunkWidth / 2) <= currentRadius) {
                        continue;
                    }

                    LodKey key{ (int8_t)level, unitX, unitZ };
                    m_wanted.insert(key);
                    
                    if (m_unitLookup.count(key) || m_pending.count(key)) continue;

                    int dx = unitX - centerUnitX;
                    int dz = unitZ - centerUnitZ;
                    need.push_back({ key, dx*dx + dz*dz });
                }
            }
            currentRadius = nextRadius; // 更新上一级半径
            if (currentRadius >= lodDistance) break;
        }

        // 处理脏块
        for (const auto& dirtyKey : m_dirtyLods) {
            m_pending.erase(dirtyKey);
            auto it = m_unitLookup.find(dirtyKey);
            if (it != m_unitLookup.end()) {
                m_unitsList[it->second].active = false;
                m_unitLookup.erase(it);
                m_garbageUnits++;
            }
        }
        m_dirtyLods.clear();

        // 标记废弃块
        for (auto& unit : m_unitsList) {
            if (!unit.active) continue;
            LodKey key{ (int8_t)unit.level, unit.unitX, unit.unitZ };
            if (!m_wanted.count(key)) {
                unit.active = false;
                m_unitLookup.erase(key);
                m_garbageUnits++;
            }
        }

        // 内存安全回收
        if (m_garbageUnits > 3000 || m_forceFullRebuild) {
            m_cpuVerts.clear();
            m_cpuIndices.clear();
            m_unitsList.clear();
            m_unitLookup.clear();
            m_uploadedVertCount = 0;
            m_uploadedIndexCount = 0;
            m_garbageUnits = 0;
            m_forceFullRebuild = false;
            
            need.clear();
            for (const auto& key : m_wanted) {
                int sampleStep = 1 << key.level;
                int chunkWidth = (kLodUnitSize * sampleStep) / 16;
                int centerUnitX = playerChunkX / chunkWidth;
                int centerUnitZ = playerChunkZ / chunkWidth;
                int dx = key.unitX - centerUnitX;
                int dz = key.unitZ - centerUnitZ;
                need.push_back({ key, dx*dx + dz*dz });
            }
        }

        std::sort(need.begin(), need.end(), [](const Need& a, const Need& b){ return a.d2 < b.d2; });
        
        for (const auto& n : need) {
            if (m_unitLookup.count(n.k) || m_pending.count(n.k)) continue;
            m_pending.insert(n.k);
            {
                std::lock_guard lk(m_taskMutex);
                m_taskQueue.push({ n.k.level, n.k.unitX, n.k.unitZ });
            }
            m_taskCv.notify_one();
        }
    }

    DrainResults(renderer);
}

void LodManager::StoreRealChunk(int cx, int cz, const ClientChunk& chunk) {
    std::lock_guard lock(m_realDataMutex);
    uint64_t key = ChunkKey(cx, cz);
    
    // 如果满了，淘汰最旧的一个
    if (m_realChunks.size() > 2048) {
        if (!m_realChunkLRU.empty()) {
            m_realChunks.erase(m_realChunkLRU.front());
            m_realChunkLRU.erase(m_realChunkLRU.begin());
        }
    }

    RealChunkData data;
    std::copy(chunk.heights.begin(), chunk.heights.end(), data.heights.begin());
    std::copy(chunk.topBlock.begin(), chunk.topBlock.end(), data.topBlocks.begin());
    
    m_realChunks[key] = data;
    m_realChunkLRU.push_back(key);

    // ★ 核心修复：把这个区块覆盖的所有 LOD 块标记为脏，强制下一帧重新生成
    int maxN = 0;
    if (m_lastLodDistance > 0) {
        maxN = 1;
        while ((m_lastRenderDistance * (1 << maxN)) < m_lastLodDistance) maxN++;
    }
    for (int level = 1; level <= maxN; ++level) {
        int sampleStep = 1 << level;
        int chunkWidth = (kLodUnitSize * sampleStep) / 16;
        int unitX = cx / chunkWidth;
        int unitZ = cz / chunkWidth;
        m_dirtyLods.insert({ (int8_t)level, unitX, unitZ });
    }
}

void LodManager::DrainResults(Eng::service::IRenderService* renderer) {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    if (m_resultQueue.empty()) return;

    bool newData = false;
    while (!m_resultQueue.empty()) {
        auto result = std::move(m_resultQueue.front());
        m_resultQueue.pop();

        LodKey key{ static_cast<int8_t>(result.level), result.unitX, result.unitZ };
        
        uint32_t startIndex = static_cast<uint32_t>(m_cpuIndices.size());
        uint32_t vOffset = static_cast<uint32_t>(m_cpuVerts.size());

        for (auto& v : result.vertices) m_cpuVerts.push_back(v);
        for (auto i : result.indices) m_cpuIndices.push_back(i + vOffset);

        LodUnit newUnit{
            .level = result.level,
            .unitX = result.unitX,
            .unitZ = result.unitZ,
            .firstIndex = startIndex,
            .indexCount = static_cast<uint32_t>(result.indices.size()),
            .aabbMin = result.aabbMin,
            .aabbMax = result.aabbMax,
            .active = true
        };

        auto it = m_unitLookup.find(key);
        if (it != m_unitLookup.end()) {
            m_unitsList[it->second] = newUnit; 
        } else {
            m_unitLookup[key] = m_unitsList.size();
            m_unitsList.push_back(newUnit); 
        }
        m_pending.erase(key);
        newData = true;
    }
    m_resultCv.notify_all();

    if (!newData) return;

    size_t needVboBytes = m_cpuVerts.size() * sizeof(Eng::service::Vertex);
    size_t needIboBytes = m_cpuIndices.size() * sizeof(uint32_t);

    bool needFullUpload = false;
    if (!m_lodVbo || m_lodVboCapacity < needVboBytes) {
        m_lodVbo = renderer->CreateBuffer({ .size = std::max<size_t>(needVboBytes * 2, 64ull * 1024 * 1024), .usage = Eng::service::rhi::BufferUsage::Vertex });
        m_lodVboCapacity = std::max<size_t>(needVboBytes * 2, 64ull * 1024 * 1024);
        needFullUpload = true;
    }
    if (!m_lodIbo || m_lodIboCapacity < needIboBytes) {
        m_lodIbo = renderer->CreateBuffer({ .size = std::max<size_t>(needIboBytes * 2, 32ull * 1024 * 1024), .usage = Eng::service::rhi::BufferUsage::Index });
        m_lodIboCapacity = std::max<size_t>(needIboBytes * 2, 32ull * 1024 * 1024);
        needFullUpload = true;
    }

    bool needsFullUpload = needFullUpload || 
                           (m_cpuVerts.size() < m_uploadedVertCount) || 
                           (m_cpuIndices.size() < m_uploadedIndexCount);

    if (needsFullUpload) {
        renderer->UpdateBuffer(m_lodVbo, 0, std::as_bytes(std::span(m_cpuVerts)));
        renderer->UpdateBuffer(m_lodIbo, 0, std::as_bytes(std::span(m_cpuIndices)));
        m_uploadedVertCount = m_cpuVerts.size();
        m_uploadedIndexCount = m_cpuIndices.size();
    } 
    else {
        // 增量上传
        if (m_cpuVerts.size() > m_uploadedVertCount) {
            size_t newVerts = m_cpuVerts.size() - m_uploadedVertCount;
            renderer->UpdateBuffer(m_lodVbo, m_uploadedVertCount * sizeof(Eng::service::Vertex), 
                std::as_bytes(std::span(m_cpuVerts.data() + m_uploadedVertCount, newVerts)));
            m_uploadedVertCount = m_cpuVerts.size();
        }

        if (m_cpuIndices.size() > m_uploadedIndexCount) {
            size_t newIndices = m_cpuIndices.size() - m_uploadedIndexCount;
            renderer->UpdateBuffer(m_lodIbo, m_uploadedIndexCount * sizeof(uint32_t), 
                std::as_bytes(std::span(m_cpuIndices.data() + m_uploadedIndexCount, newIndices)));
            m_uploadedIndexCount = m_cpuIndices.size();
        }
    }
}

void LodManager::PrepareDraw(Eng::service::IRenderService* renderer, const Eng::client::Frustum& frustum) {
    if (!m_lodVbo || !m_lodIbo || m_unitsList.empty()) return;

    m_indirectCommands.clear();
    m_indirectCommands.reserve(m_unitsList.size());
    for (const auto& unit : m_unitsList) {
        if (!unit.active || unit.indexCount == 0) continue;
        if (!frustum.TestAABB(unit.aabbMin, unit.aabbMax)) continue;

        m_indirectCommands.push_back({
            .indexCount    = unit.indexCount,
            .instanceCount = 1,
            .firstIndex    = unit.firstIndex,
            .vertexOffset  = 0,
            .firstInstance = 0
        });
    }
    if (!m_indirectCommands.empty()) {
        size_t needBytes = m_indirectCommands.size() * sizeof(Eng::service::rhi::DrawIndexedIndirectCommand);
        if (!m_indirectBuffer || m_indirectCapacity < needBytes) {
            m_indirectBuffer = renderer->CreateBuffer({ .size = needBytes, .usage = Eng::service::rhi::BufferUsage::Indirect });
            m_indirectCapacity = needBytes;
        }
        renderer->UpdateBuffer(
            m_indirectBuffer, 0,
            std::as_bytes(std::span<const Eng::service::rhi::DrawIndexedIndirectCommand>(m_indirectCommands)));
    }
}

void LodManager::Draw(Eng::service::IRenderService* renderer, const Eng::service::ShaderPtr& shader, const Eng::service::TexturePtr& tex) {
    if (!m_lodVbo || !m_lodIbo || !m_indirectBuffer || m_indirectCommands.empty()) return;

    renderer->BindShader(shader);
    renderer->BindVertexBuffer(m_lodVbo);
    renderer->BindIndexBuffer(m_lodIbo);
    renderer->BindTexture(tex, 0);
    
    renderer->DrawIndexedIndirect(
        m_indirectBuffer,
        static_cast<uint32_t>(m_indirectCommands.size()),
        sizeof(Eng::service::rhi::DrawIndexedIndirectCommand)
    );
}

void LodManager::Clear() {
    Stop();
    { std::lock_guard<std::mutex> lock1(m_taskMutex); std::queue<LodTask> emptyTasks; std::swap(m_taskQueue, emptyTasks); }
    { std::lock_guard<std::mutex> lock2(m_resultMutex); std::queue<LodResult> emptyResults; std::swap(m_resultQueue, emptyResults); }

    m_unitsList.clear();
    m_unitLookup.clear();
    m_pending.clear();
    m_wanted.clear();
    m_dirtyLods.clear(); // 清理脏块
    m_cpuVerts.clear();
    m_cpuIndices.clear();
    m_indirectCommands.clear();
    
    {
        std::lock_guard<std::mutex> lock3(m_realDataMutex);
        m_realChunks.clear();
        m_realChunkLRU.clear();
    }

    m_lodVbo.reset();
    m_lodIbo.reset();
    m_indirectBuffer.reset();
    m_terrain.reset();
    
    m_lodVboCapacity = 0;
    m_lodIboCapacity = 0;
    m_indirectCapacity = 0;
    m_uploadedVertCount = 0;
    m_uploadedIndexCount = 0;
    m_garbageUnits = 0;
    m_forceFullRebuild = false;
    
    m_lastRenderDistance = -1;
    m_lastLodDistance = -1;
    m_lastPlayerChunkX = INT_MIN;
    m_lastPlayerChunkZ = INT_MIN;
}

void LodManager::BuildLodUnit(const game::server::TerrainGenerator& terrain,
                          int level, int unitX, int unitZ,
                          std::vector<Eng::service::Vertex>& outV,
                          std::vector<uint32_t>& outI,
                          glm::vec3& outMin,
                          glm::vec3& outMax) const {
    int sampleStep = 1 << level;
    int chunkWidth = (kLodUnitSize * sampleStep) / 16;
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

    // ★ 核心修复：提前查好 BlockID，不用 static，避免多线程重入或多实例污染
    const BlockId waterId = m_registry->Get("mycraft:water").id;
    const BlockId sandId = m_registry->Get("mycraft:sand").id;
    const BlockId snowId = m_registry->Get("mycraft:snow").id;
    const BlockId stoneId = m_registry->Get("mycraft:stone").id;

    // ★ 核心修复 2：批量提取真实区块数据到局部 Map，避免在循环里高频加锁
    std::unordered_map<uint64_t, RealChunkData> localRealData;
    {
        // 这里因为加了 mutable，在 const 函数里也能正常加锁了
        std::lock_guard lock(m_realDataMutex);
        int minCx = baseBlockX >> 4;
        int maxCx = (baseBlockX + N * sampleStep) >> 4;
        int minCz = baseBlockZ >> 4;
        int maxCz = (baseBlockZ + N * sampleStep) >> 4;

        for (int cxi = minCx; cxi <= maxCx; ++cxi) {
            for (int czi = minCz; czi <= maxCz; ++czi) {
                uint64_t key = ChunkKey(cxi, czi);
                auto it = m_realChunks.find(key);
                if (it != m_realChunks.end()) {
                    localRealData[key] = it->second;
                }
            }
        }
    }

    static thread_local int     heightMap[NP][NP];
    static thread_local uint8_t biomeMap[NP][NP];
    static thread_local bool    waterMap[NP][NP];
    
    // 1. 采样
    for (int j = 0; j < NP; ++j) {
        for (int i = 0; i < NP; ++i) {
            int worldBlockX = baseBlockX + i * sampleStep;
            int worldBlockZ = baseBlockZ + j * sampleStep;
            
            int cx = worldBlockX >> 4;
            int cz = worldBlockZ >> 4;
            int lx = worldBlockX & 15;
            int lz = worldBlockZ & 15;
            
            int h = -1;
            BlockId topBlock = AIR_ID;
            auto b = game::server::Biome::Plains;

            // 从局部 Map 读取（无锁！）
            auto it = localRealData.find(ChunkKey(cx, cz));
            if (it != localRealData.end()) {
                h = it->second.heights[lx * 16 + lz];
                topBlock = it->second.topBlocks[lx * 16 + lz];
            }

            // 如果没有真实数据，用数学种子生成
            if (h < 0) {
                h = terrain.GetSurfaceY(worldBlockX, worldBlockZ);
                b = terrain.GetBiome(worldBlockX, worldBlockZ);
            } else {
                // 如果读到了真实数据，根据顶部方块反推地形材质
                if (topBlock == waterId) b = game::server::Biome::Ocean;
                else if (topBlock == sandId) b = game::server::Biome::Beach;
                else if (topBlock == snowId) b = game::server::Biome::Snow;
                else if (topBlock == stoneId) b = game::server::Biome::Mountain;
            }

            if (h < SEA_LEVEL) {
                h = SEA_LEVEL;
                b = game::server::Biome::Ocean;
                waterMap[j][i] = true;
            } else {
                waterMap[j][i] = false;
            }
            heightMap[j][i] = h;
            biomeMap[j][i] = (uint8_t)b;
        }
    }

    auto tileForBiome = [&](game::server::Biome biome, int y, bool& isWaterOut) -> int {
        using B = game::server::Biome;
        std::string tex = "grass_block_top";
        isWaterOut = false;
        if (y > 180) biome = B::Snow;
        switch (biome) {
            case B::Ocean:    tex = "water_still"; isWaterOut = true; break;
            case B::Beach:
            case B::Desert:   tex = "sand"; break;
            case B::Snow:     tex = "snow"; break;
            case B::Mountain: tex = "stone"; break;
            default:          tex = "grass_block_top"; break;
        }
        int t = ResolveTile(tex);
        return t < 0 ? 0 : t;
    };

    auto cellY = [&](int i, int j) -> float { 
        if (waterMap[j][i]) return (float)SEA_LEVEL + 0.8f; 
        return (float)heightMap[j][i] + 1.0f; 
    };

    auto cellTile = [&](int i, int j, bool& isWater) -> int {
        auto b = (game::server::Biome)biomeMap[j][i];
        isWater = waterMap[j][i];
        return tileForBiome(b, heightMap[j][i], isWater);
    };
    
    auto pushVertex = [&](const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv, float ao) {
        Eng::service::Vertex v;
        v.position = p; v.normal = n; v.uv = uv; 
        v.ao = ao; 
        v.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        outV.push_back(v);
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    };

    const float atlasCols = static_cast<float>(AtlasCols());
    auto getTileUV = [&](int tile, float& u0, float& v0, float& u1, float& v1) {
        const float pad = 0.005f; 
        int col = tile % static_cast<int>(atlasCols);
        int row = tile / static_cast<int>(atlasCols);
        u0 = (col / atlasCols) + pad; v0 = (row / atlasCols) + pad;
        u1 = ((col + 1) / atlasCols) - pad; v1 = ((row + 1) / atlasCols) - pad;
    };

    float uvStep = std::min(static_cast<float>(sampleStep), 2.0f);

    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            float ySelf = cellY(i, j);
            bool selfIsWater;
            int   tile  = cellTile(i, j, selfIsWater);
            
            float u0, v0, u1, v1;
            getTileUV(tile, u0, v0, u1, v1);
            float uR = u0 + (u1 - u0) * uvStep;
            float vB = v0 + (v1 - v0) * uvStep;

            float x0 = static_cast<float>(baseBlockX + i * sampleStep);
            float x1 = static_cast<float>(baseBlockX + (i + 1) * sampleStep);
            float z0 = static_cast<float>(baseBlockZ + j * sampleStep);
            float z1 = static_cast<float>(baseBlockZ + (j + 1) * sampleStep);

            // 2. 顶面
            {
                float ao = 1.0f;
                if (j > 0  && heightMap[j-1][i] > heightMap[j][i]) ao -= 0.15f;
                if (j < N-1&& heightMap[j+1][i] > heightMap[j][i]) ao -= 0.15f;
                if (i > 0  && heightMap[j][i-1] > heightMap[j][i]) ao -= 0.15f;
                if (i < N-1&& heightMap[j][i+1] > heightMap[j][i]) ao -= 0.15f;
                ao = std::max(ao, 0.6f);

                uint32_t base = static_cast<uint32_t>(outV.size());
                pushVertex({x0, ySelf, z0}, {0,1,0}, {u0, vB}, ao);
                pushVertex({x1, ySelf, z0}, {0,1,0}, {uR, vB}, ao);
                pushVertex({x1, ySelf, z1}, {0,1,0}, {uR, v0}, ao);
                pushVertex({x0, ySelf, z1}, {0,1,0}, {u0, v0}, ao);
                outI.push_back(base+0); outI.push_back(base+3); outI.push_back(base+2);
                outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+1);
            }

            // 3. 侧面
            int ni[4] = {i+1, i-1, i, i};
            int nj[4] = {j, j, j+1, j-1};
            const glm::vec3 normals[4] = {{1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}};

            for (int f = 0; f < 4; ++f) {
                int query_i = std::clamp(ni[f], 0, NP - 1);
                int query_j = std::clamp(nj[f], 0, NP - 1);

                float yNb = cellY(query_i, query_j);
                bool nbIsWater;
                int nt = cellTile(query_i, query_j, nbIsWater);

                bool isEdge = (i == 0 || i == N-1 || j == 0 || j == N-1);
                bool needSide = false;

                if (selfIsWater) {
                    if (!nbIsWater && ySelf > yNb + 0.01f) needSide = true;
                } else {
                    needSide = (ySelf > yNb + 0.01f) || (selfIsWater != nbIsWater) || isEdge;
                }

                if (needSide) {
                    float sideAO = (yNb > ySelf) ? 0.5f : 0.8f; 
                    
                    float yBottom = std::min(yNb, ySelf) - 20.0f;
                    if (yBottom < -10.0f) yBottom = -10.0f;
                    if (selfIsWater && !nbIsWater && yNb > SEA_LEVEL) yBottom = yNb; 
                    if (yBottom > ySelf) yBottom = ySelf - 1.0f;

                    float nu0, nv0, nu1, nv1; 
                    getTileUV(nt, nu0, nv0, nu1, nv1);
                    float nUR = nu0 + (nu1 - nu0) * uvStep;
                    float nVB = nv0 + (nv1 - nv0) * uvStep;
                    
                    uint32_t base = static_cast<uint32_t>(outV.size());
                    glm::vec3 n = normals[f];

                    if (f == 0) {
                        pushVertex({x1, yBottom, z0}, n, {nu0, nVB}, sideAO);
                        pushVertex({x1, ySelf,   z0}, n, {nu0, nv0}, sideAO);
                        pushVertex({x1, ySelf,   z1}, n, {nUR, nv0}, sideAO);
                        pushVertex({x1, yBottom, z1}, n, {nUR, nVB}, sideAO);
                        outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                        outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                    } else if (f == 1) {
                        pushVertex({x0, yBottom, z1}, n, {nu0, nVB}, sideAO);
                        pushVertex({x0, ySelf,   z1}, n, {nu0, nv0}, sideAO);
                        pushVertex({x0, ySelf,   z0}, n, {nUR, nv0}, sideAO);
                        pushVertex({x0, yBottom, z0}, n, {nUR, nVB}, sideAO);
                        outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                        outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                    } else if (f == 2) {
                        pushVertex({x0, yBottom, z1}, n, {nu0, nVB}, sideAO);
                        pushVertex({x0, ySelf,   z1}, n, {nu0, nv0}, sideAO);
                        pushVertex({x1, ySelf,   z1}, n, {nUR, nv0}, sideAO);
                        pushVertex({x1, yBottom, z1}, n, {nUR, nVB}, sideAO);
                        outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+1);
                        outI.push_back(base+0); outI.push_back(base+3); outI.push_back(base+2);
                    } else if (f == 3) {
                        pushVertex({x1, yBottom, z0}, n, {nu0, nVB}, sideAO);
                        pushVertex({x1, ySelf,   z0}, n, {nu0, nv0}, sideAO);
                        pushVertex({x0, ySelf,   z0}, n, {nUR, nv0}, sideAO);
                        pushVertex({x0, yBottom, z0}, n, {nUR, nVB}, sideAO);
                        outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                        outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                    }
                }
            }
        }
    }
}
} // namespace game::client