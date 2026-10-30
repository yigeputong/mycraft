#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <atomic>
#include <glm/glm.hpp>
#include "game/core/BlockRegistry.h"
#include "game/server/WorldGenerator.h"
#include "client/Render/RenderAPI.h"
#include "game/client/ClientWorld.h"

namespace game::client {

struct LodKey {
    int8_t level;
    int32_t unitX; // ★ 语义明确：这是 LOD 网格的 X 索引，而不是区块坐标
    int32_t unitZ; // ★ 语义明确：这是 LOD 网格的 Z 索引
    bool operator==(const LodKey& o) const { 
        return level == o.level && unitX == o.unitX && unitZ == o.unitZ; 
    }
};

struct LodKeyHash {
    size_t operator()(const LodKey& k) const {
        return std::hash<int>()(k.level) ^ (std::hash<int>()(k.unitX) << 1) ^ (std::hash<int>()(k.unitZ) << 2);
    }
};

struct LodUnit {
    int level;
    int unitX, unitZ;
    Eng::client::MeshPtr mesh;
    glm::vec3 aabbMin, aabbMax;
};

class LodManager {
public:
    void Start(uint32_t seed);
    void Stop();
    void Update(const glm::vec3& cameraPos, const ClientWorld& world, int renderDistance, Eng::client::IRenderAPI* renderer);
    void CollectVisibleMeshes(const Frustum& frustum, std::vector<Eng::client::MeshPtr>& outMeshes) const;
    void Clear();
    void SetRegistry(const BlockRegistry* reg) { m_registry = reg; }

private:
    struct LodTask { 
        int level, unitX, unitZ; 
    };
    struct LodResult {
        int level, unitX, unitZ;
        std::vector<Eng::client::Vertex> vertices;
        std::vector<uint32_t> indices;
        glm::vec3 aabbMin, aabbMax;
    };

    void WorkerLoop(uint32_t seed);
    void DrainResults(Eng::client::IRenderAPI* renderer);
    void BuildLodUnit(const game::server::TerrainGenerator& terrain, int level, int unitX, int unitZ,
                      std::vector<Eng::client::Vertex>& outV, std::vector<uint32_t>& outI,
                      glm::vec3& outMin, glm::vec3& outMax) const;
    void BuildLod1FromChunks(const LodKey& key, const ClientWorld& world,
                             std::vector<Eng::client::Vertex>& outV, std::vector<uint32_t>& outI,
                             glm::vec3& outMin, glm::vec3& outMax);

    const BlockRegistry* m_registry = nullptr;

    std::unique_ptr<game::server::TerrainGenerator> m_terrain;
    std::vector<std::thread> m_workers;
    std::mutex m_taskMutex, m_resultMutex;
    std::condition_variable m_taskCv;
    std::queue<LodTask> m_taskQueue;
    std::queue<LodResult> m_resultQueue;
    std::atomic<bool> m_stop{false};

    std::unordered_map<LodKey, LodUnit, LodKeyHash> m_units;
    std::unordered_set<LodKey, LodKeyHash> m_pending;
    std::unordered_set<LodKey, LodKeyHash> m_wanted;
    std::vector<LodKey> m_needQueue;
    size_t m_needCursor = 0;

    int m_lastRenderDistance = -1;
    int m_lastPlayerChunkX = INT_MIN, m_lastPlayerChunkZ = INT_MIN;

    static constexpr int kLodLevels = 3;
    static constexpr int kLodUnitSize = 8; // 这里的 8 代表每个 LOD 块在 X/Z 轴上采样 8 个点
};

} // namespace game::client