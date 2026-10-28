#pragma once
#include "client/Render/RenderAPI.h"
#include "game/client/ClientWorld.h"
#include "game/server/WorldGenerator.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>

namespace game::client {

struct LodKey {
    int8_t  level;
    int32_t cx, cz;
    bool operator==(const LodKey&) const = default;
};

struct LodKeyHash {
    size_t operator()(const LodKey& k) const {
        size_t h = (size_t)(uint8_t)k.level;
        h = h * 31 + (size_t)(uint32_t)k.cx;
        h = h * 31 + (size_t)(uint32_t)k.cz;
        return h;
    }
};

struct LodUnit {
    int8_t  level = 1;
    int32_t cx = 0, cz = 0;
    Eng::client::MeshPtr mesh;                   // ← MeshPtr，默认空
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
};

class LodManager {
public:
    void Start(uint32_t seed);
    void Stop();

    // 每帧调用：处理流式加载、下发任务、收集结果
    void Update(const glm::vec3& cameraPos, const ClientWorld& world, Eng::client::IRenderAPI* renderer);

    // 收集可见的 LOD Mesh
    void CollectVisibleMeshes(const Frustum& frustum, std::vector<Eng::client::MeshPtr>& outMeshes) const;

    // 退出时清空所有 LOD 资源（MeshPtr 自动释放，无 renderer 参数）
    void Clear();

private:
    struct LodTask   { int8_t level; int32_t cx, cz; };
    struct LodResult {
        int8_t  level;
        int32_t cx, cz;
        glm::vec3 aabbMin, aabbMax;
        std::vector<Eng::client::Vertex> vertices;
        std::vector<uint32_t>            indices;
    };

    void WorkerLoop(uint32_t seed);
    void BuildLodUnit(const game::server::TerrainGenerator& terrain,
                      int level, int ucx, int ucz,
                      std::vector<Eng::client::Vertex>& outV,
                      std::vector<uint32_t>& outI,
                      glm::vec3& outMin, glm::vec3& outMax) const;

    void BuildLod1FromChunks(const LodKey& key, const ClientWorld& world,
                             std::vector<Eng::client::Vertex>& outV,
                             std::vector<uint32_t>& outI,
                             glm::vec3& outMin, glm::vec3& outMax);

    void DrainResults(Eng::client::IRenderAPI* renderer);

    std::unique_ptr<game::server::TerrainGenerator> m_terrain;

    std::unordered_map<LodKey, LodUnit, LodKeyHash> m_units;
    std::unordered_set<LodKey, LodKeyHash>          m_pending;
    std::vector<LodKey>                             m_needQueue;
    size_t                                          m_needCursor = 0;

    std::vector<std::thread>      m_workers;
    std::mutex                    m_taskMutex;
    std::condition_variable       m_taskCv;
    std::queue<LodTask>           m_taskQueue;
    std::atomic<bool>             m_stop{false};

    std::mutex                    m_resultMutex;
    std::queue<LodResult>         m_resultQueue;

    int m_lastPcx = INT_MIN;
    int m_lastPcz = INT_MIN;
    glm::vec3 m_lastCameraPos{0.0f};
    float     m_stationaryTime = 0.0f;

    static constexpr int kLodLevels   = 3;
    static constexpr int kLodUnitSize = 16;
};

} // namespace game::client