#pragma once
#include "Engine/tool/Transport.h"
#include "Engine/tool/MemoryTransport.h"
#include "Engine/tool/Log.h"
#include "Engine/tool/RAIIThread.h"      // ★ 引入自动 join 的跨平台线程
#include "Engine/tool/UniqueFunction.h"  // ★ 引入手搓的零拷贝函数包装器
#include "game/core/World.h"
#include "game/server/WorldGenerator.h"
#include "game/Protocol.h"
#include "game/core/Physics.h"
#include "game/core/BlockRegistry.h"
#include "game/server/WorldSave.h"

#include <memory>
#include <thread>
#include <atomic>
#include <glm/glm.hpp>
#include <queue>
#include <unordered_map>
#include <condition_variable>
#include <shared_mutex>
#include <string>
#include <vector>

namespace game::server {

class GameServer {
public:
    bool Start(uint16_t port, const std::string& saveDir, bool useMemory = false);
    void Stop();
    bool IsRunning() const { return m_running.load(); }
    std::shared_ptr<Eng::tool::MemoryPipe> GetMemoryPipe() const;
    size_t GetChunkCount() const { return m_chunks.size(); } 

    // ★ 提供给 UI 绘制“小太阳”的进度查询接口
    GenPhase GetChunkProgress(int cx, int cz) const;

private:
    // ==================== 生命周期 ====================
    static constexpr float TPS = 20.0f;
    std::thread        m_thread; // 服务端主循环线程
    std::atomic<bool>  m_running{false};

    // ==================== 网络与注册表 ====================
    std::unique_ptr<Eng::tool::IServerTransport> m_server;
    std::unique_ptr<Eng::tool::Log>              m_logger = std::make_unique<Eng::tool::Log>();
    game::BlockRegistry                          m_registry; 

    // ==================== 玩家 ====================
    struct ServerPlayer {
        uint32_t    id = 0;
        std::string name;
        glm::vec3   position{0.0f, 256.0f, 0.0f};
        glm::vec3   velocity{0.0f};
        bool        onGround = false;
        bool        jump     = false;
        float       yaw   = 0.0f;
        float       pitch = 0.0f;
        glm::vec3   moveDir{0.0f};
        bool        flyMode = false;
    };
    std::vector<ServerPlayer> m_players;
    mutable std::mutex        m_playersMutex;
    static constexpr float REACH_DISTANCE = 5.0f;

    // ==================== 世界数据 ====================
    std::unique_ptr<TerrainGenerator>   m_terrain;
    std::unordered_map<uint64_t, Chunk> m_chunks; // 已完全就绪的区块（在 Tick 线程中读写，无需加锁）
    uint32_t m_worldSeed = 0;
    glm::vec3 m_spawnPos{0.0f, 256.0f, 0.0f};

    struct PendingChunkRequest { int clientId; int cx, cz; };
    std::queue<PendingChunkRequest> m_chunkQueue;

    // ==================== 存档 ====================
    std::unique_ptr<WorldSave> m_save;
    std::unordered_map<uint64_t, std::vector<BlockChange>> m_pendingChanges;
    bool  m_dirty      = false;
    float m_saveAccum  = 0.0f;
    static constexpr float kSaveIntervalSec = 10.0f;

    // ==================== 跨平台流水线 ====================
    struct InFlightChunk {
        uint64_t key = 0;
        std::unique_ptr<ChunkGenContext> ctx;
        std::atomic<GenPhase> phase{GenPhase::Empty};
    };

    // 最大允许同时在流水线中（排队+生成中）的区块数量
    // 建议设为 CPU 线程数的 4~8 倍，比如 64 或 128
    static constexpr size_t MAX_CONCURRENT_GENERATING = 64; 

    // 所有正在生成的区块（由读写锁保护）
    mutable std::shared_mutex m_genMutex;
    std::unordered_map<uint64_t, std::shared_ptr<InFlightChunk>> m_generatingChunks;

    // ★ 线程池使用 RAIIThread，自动处理 stop_token 与 join
    std::vector<std::unique_ptr<Eng::tool::RAIIThread>> m_workers;
    
    // ★ 任务队列使用 UniqueFunction，完美移动 unique_ptr，零拷贝开销
    std::mutex                                          m_taskMutex;
    std::condition_variable                             m_taskCv;
    std::queue<Eng::tool::UniqueFunction>               m_taskQueue;
    std::atomic<bool>                                   m_workersStop{false};

    // 生成完成的结果（由主线程消费）
    struct ChunkResult { 
        uint64_t key; 
        std::unique_ptr<Chunk> chunk; 
    };
    std::mutex                                          m_resultMutex;
    std::queue<ChunkResult>                             m_resultQueue;

    // 等待客户端发往对应区块的网络包
    std::unordered_map<uint64_t, std::vector<int>>      m_waitingClients;

    void StartWorkers(uint32_t seed);
    void StopWorkers();
    void WorkerLoop(uint32_t seed, const std::stop_token& st);
    void DrainChunkResults();
    void SendChunkTo(int clientId, const Chunk& chunk);

    // ==================== 实体 ====================
    struct ItemEntity : game::PointEntity {
        uint32_t  id          = 0;
        float     pickupDelay = 0.5f;
        float     age         = 0.0f;
        BlockId   itemType    = AIR_ID;
    };
    std::unordered_map<uint32_t, ItemEntity> m_entities;
    uint32_t m_nextEntityId = 1;
    std::vector<uint32_t> m_freeEntityIds;   // ID 回收池

    uint32_t AllocEntityId() {
        if (!m_freeEntityIds.empty()) {
            uint32_t id = m_freeEntityIds.back();
            m_freeEntityIds.pop_back();
            return id;
        }
        return m_nextEntityId++;
    }

    void SpawnItemDrop(const glm::vec3& pos, BlockId id);
    void TickEntities(float dt);

    // ==================== 性能指标 ====================
    float m_lastTickMs = 0.0f;

    // ==================== 核心方法 ====================
    void Run();
    void HandleMessage(int clientId, const std::vector<uint8_t>& data);
    void ApplyPlayerInput(int clientId, const net::PlayerInput& in);
    void TickWorld(float dt);

    // ==================== 世界查询/修改 ====================
    Chunk* GetCachedChunk(int cx, int cz);
    BlockId GetBlockAt(int wx, int wy, int wz) const;
    void    SetBlockAt(int wx, int wy, int wz, BlockId id);
    bool    IsInsidePlayer(const ServerPlayer& p, int bx, int by, int bz);

    // ==================== 交互 ====================
    void HandleDig(ServerPlayer& p);
    void HandlePlace(ServerPlayer& p, BlockId id);

    // ==================== 广播 ====================
    void BroadcastWorldState();
    void BroadcastBlockChange(int bx, int by, int bz, BlockId id);

    // ==================== 碰撞 ====================
    bool IsSolidAt(int wx, int wy, int wz) const;
    bool AABBCollides(const glm::vec3& pos) const;   // pos = 脚底
};

} // namespace game::server