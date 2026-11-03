#pragma once
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "game/core/BlockRegistry.h"
#include "game/server/WorldGenerator.h"
#include "Engine/service/IRenderService.h"
#include "game/client/ClientWorld.h"
#include "Engine/client/SceneRenderer.h"
#include "Engine/tool/RAIIThread.h"

namespace game::client {

struct RealChunkData {
    std::array<int16_t, 256> heights;
    std::array<BlockId, 256> topBlocks;
};

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
    uint32_t firstIndex;
    uint32_t indexCount;
    glm::vec3 aabbMin, aabbMax;
    bool active = true;
};

class LodManager {
public:
    void Start(uint32_t seed);
    void Stop();
    
    // lodDistance: 远景极限视距（单位：区块），传 0 则关闭 LOD
    void Update(const glm::vec3& cameraPos, [[maybe_unused]] const ClientWorld& world, int renderDistance, int lodDistance, Eng::service::IRenderService* renderer);
    
    void StoreRealChunk(int cx, int cz, const ClientChunk& chunk);

    // MDI 渲染：视锥剔除并组装指令
    void PrepareDraw(Eng::service::IRenderService* renderer, const Eng::client::Frustum& frustum);
    
    // 直接触发 MDI 绘制
    void Draw(Eng::service::IRenderService* renderer, const Eng::service::ShaderPtr& shader, const Eng::service::TexturePtr& tex);
    
    void Clear();
    void SetRegistry(const BlockRegistry* reg) { m_registry = reg; }
    size_t GetLodUnitCount() const { return m_unitsList.size(); }

private:
    struct LodTask { int level, unitX, unitZ; };
    struct LodResult {
        int level, unitX, unitZ;
        std::vector<Eng::service::Vertex> vertices;
        std::vector<uint32_t> indices;
        glm::vec3 aabbMin, aabbMax;
    };

    void WorkerLoop(uint32_t seed, const std::stop_token& st);
    void DrainResults(Eng::service::IRenderService* renderer);
    void BuildLodUnit(const game::server::TerrainGenerator& terrain, int level, int unitX, int unitZ,
                      std::vector<Eng::service::Vertex>& outV, std::vector<uint32_t>& outI,
                      glm::vec3& outMin, glm::vec3& outMax) const;

    const BlockRegistry* m_registry = nullptr;
    std::unique_ptr<game::server::TerrainGenerator> m_terrain;

    // 线程池与任务队列
    std::vector<std::unique_ptr<Eng::tool::RAIIThread>> m_workers;
    std::mutex m_taskMutex, m_resultMutex;
    std::condition_variable m_taskCv, m_resultCv;
    std::queue<LodTask> m_taskQueue;
    std::queue<LodResult> m_resultQueue;

    // 连续内存 + 索引查找
    std::vector<LodUnit> m_unitsList;
    std::unordered_map<LodKey, size_t, LodKeyHash> m_unitLookup; 
    std::unordered_set<LodKey, LodKeyHash> m_pending;
    std::unordered_set<LodKey, LodKeyHash> m_wanted;

    // 真实数据缓存
    mutable std::mutex m_realDataMutex;
    std::unordered_map<uint64_t, RealChunkData> m_realChunks;
    std::vector<uint64_t> m_realChunkLRU; // 用于简单淘汰
    std::unordered_set<LodKey, LodKeyHash> m_dirtyLods;
    
    // 内存回收（防长途飞行泄漏）
    size_t m_garbageUnits = 0;
    bool m_forceFullRebuild = false;

    // 全局超级合批 Buffer
    std::vector<Eng::service::Vertex> m_cpuVerts;
    std::vector<uint32_t> m_cpuIndices;
    
    Eng::service::BufferPtr m_lodVbo;
    Eng::service::BufferPtr m_lodIbo;
    Eng::service::BufferPtr m_indirectBuffer;
    
    size_t m_lodVboCapacity = 0;
    size_t m_lodIboCapacity = 0;
    size_t m_indirectCapacity = 0;
    
    // 增量上传记录
    size_t m_uploadedVertCount = 0;
    size_t m_uploadedIndexCount = 0;

    std::vector<Eng::service::rhi::DrawIndexedIndirectCommand> m_indirectCommands;

    int m_lastRenderDistance = -1;
    int m_lastLodDistance = -1;
    int m_lastPlayerChunkX = INT_MIN, m_lastPlayerChunkZ = INT_MIN;

    static constexpr int kLodUnitSize = 8; 
};

} // namespace game::client