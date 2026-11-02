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
#include "Engine/service/IRenderService.h"
#include "game/client/ClientWorld.h"
#include "Engine/client/SceneRenderer.h" // ★ 关键：引入 RenderMesh 定义

namespace game::client {

struct LodKey {
    int8_t level;
    int32_t unitX;
    int32_t unitZ;
    bool operator==(const LodKey& o) const { 
        return level == o.level && unitX == o.unitX && unitZ == o.unitZ; 
    }
};

struct LodKeyHash {
    size_t operator()(const LodKey& k) const {
        uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(k.unitX)) * 73856093ULL ^
                     static_cast<uint64_t>(static_cast<uint32_t>(k.unitZ)) * 19349663ULL ^
                     static_cast<uint64_t>(static_cast<uint8_t>(k.level)) * 83492791ULL;
        return static_cast<size_t>(h);
    }
};

struct LodUnit {
    int level;
    int unitX, unitZ;
    Eng::client::RenderMesh mesh; // ★ 从 MeshPtr 换成 RenderMesh
    glm::vec3 aabbMin, aabbMax;
};

class LodManager {
public:
    void Start(uint32_t seed);
    void Stop();
    void Update(const glm::vec3& cameraPos, const ClientWorld& world, int renderDistance, Eng::service::IRenderService* renderer);
    
    // ★ 收集接口换成 RenderMesh
    void CollectVisibleMeshes(const Eng::client::Frustum& frustum, std::vector<Eng::client::RenderMesh>& outMeshes) const;
    
    void Clear();
    void SetRegistry(const BlockRegistry* reg) { m_registry = reg; }

private:
    struct LodTask { 
        int level, unitX, unitZ; 
    };
    struct LodResult {
        int level, unitX, unitZ;
        std::vector<Eng::service::Vertex> vertices;
        std::vector<uint32_t> indices;
        glm::vec3 aabbMin, aabbMax;
    };

    void WorkerLoop(uint32_t seed);
    void DrainResults(Eng::service::IRenderService* renderer);
    void BuildLodUnit(const game::server::TerrainGenerator& terrain, int level, int unitX, int unitZ,
                      std::vector<Eng::service::Vertex>& outV, std::vector<uint32_t>& outI,
                      glm::vec3& outMin, glm::vec3& outMax) const;
    void BuildLod1FromChunks(const LodKey& key, const ClientWorld& world,
                             std::vector<Eng::service::Vertex>& outV, std::vector<uint32_t>& outI,
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
    static constexpr int kLodUnitSize = 8; 
};

} // namespace game::client