#pragma once
#include <atomic>
#include <condition_variable>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Engine/tool/Log.h"
#include "Engine/tool/MemoryTransport.h"
#include "Engine/tool/MessageReader.h"
#include "Engine/tool/RAIIThread.h"
#include "Engine/tool/Transport.h"
#include "Engine/tool/UniqueFunction.h"

#include "game/Protocol.h"
#include "game/core/BlockRegistry.h"
#include "game/core/Physics.h"
#include "game/core/World.h"
#include "game/core/ecs/Components.h"
#include "game/core/ecs/EntityManager.h"
#include "game/core/ecs/System.h"
#include "game/server/WorldGenerator.h"
#include "game/server/WorldSave.h"

namespace game::server {

class GameServer {
public:
    bool Start(uint16_t port, const std::string& worldId, bool useMemory = false);
    void Stop();
    bool IsRunning() const { return m_running.load(); }
    std::shared_ptr<Eng::tool::MemoryPipe> GetMemoryPipe() const;
    size_t GetChunkCount() const { return m_chunks.size(); }

    GenPhase GetChunkProgress(int cx, int cz) const;

private:
    // ==================== 生命周期 ====================
    static constexpr float TPS = 20.0f;
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    // ==================== 网络与注册表 ====================
    std::unique_ptr<Eng::tool::IServerTransport> m_server;
    std::unique_ptr<Eng::tool::Log> m_logger = std::make_unique<Eng::tool::Log>();
    game::BlockRegistry m_registry;

    // ==================== ECS 世界 ====================
    game::core::ecs::EntityManager m_ecsWorld;
    mutable std::mutex m_worldMutex;

    // 网络 ID <-> ECS EntityId 映射
    std::unordered_map<uint32_t, game::core::ecs::EntityId> m_netToEcsMap;
    std::unordered_map<game::core::ecs::EntityId, uint32_t> m_ecsToNetMap;
    uint32_t m_nextNetEntityId = 1;

    static constexpr float REACH_DISTANCE = 5.0f;

    // ==================== 世界数据 ====================
    std::unique_ptr<TerrainGenerator> m_terrain;
    std::unordered_map<uint64_t, Chunk> m_chunks;
    uint32_t m_worldSeed = 0;
    glm::vec3 m_spawnPos{0.0f, 256.0f, 0.0f};

    struct PendingChunkRequest {
        int clientId;
        int cx, cz;
    };
    std::queue<PendingChunkRequest> m_chunkQueue;

    // ==================== 存档 ====================
    std::unique_ptr<WorldSave> m_save;
    std::unordered_map<uint64_t, std::vector<BlockChange>> m_pendingChanges;
    bool m_dirty = false;
    float m_saveAccum = 0.0f;
    static constexpr float kSaveIntervalSec = 10.0f;

    // ==================== 跨平台流水线 ====================
    struct InFlightChunk {
        uint64_t key = 0;
        std::unique_ptr<ChunkGenContext> ctx;
        std::atomic<GenPhase> phase{GenPhase::Empty};
    };
    static constexpr size_t MAX_CONCURRENT_GENERATING = 64;

    mutable std::shared_mutex m_genMutex;
    std::unordered_map<uint64_t, std::shared_ptr<InFlightChunk>> m_generatingChunks;

    std::vector<std::unique_ptr<Eng::tool::RAIIThread>> m_workers;
    std::mutex m_taskMutex;
    std::condition_variable m_taskCv;
    std::queue<Eng::tool::UniqueFunction> m_taskQueue;
    std::atomic<bool> m_workersStop{false};

    struct ChunkResult {
        uint64_t key;
        std::unique_ptr<Chunk> chunk;
    };
    std::mutex m_resultMutex;
    std::queue<ChunkResult> m_resultQueue;
    std::unordered_map<uint64_t, std::vector<int>> m_waitingClients;

    void StartWorkers(uint32_t seed);
    void StopWorkers();
    void WorkerLoop(uint32_t seed, const std::stop_token& st);
    void DrainChunkResults();
    void SendChunkTo(int clientId, const Chunk& chunk);

    // ==================== ECS 辅助 ====================
    game::core::ecs::EntityId CreateNetworkEntity(uint32_t& outNetId);
    game::core::ecs::Entity* GetEntityByNetId(uint32_t netId);

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
    void SetBlockAt(int wx, int wy, int wz, BlockId id);
    bool IsInsidePlayer(game::core::ecs::Entity& playerEntity, int bx, int by, int bz);
    void SendInventoryUpdate(int clientId);
    void HandleClickContainer(int clientId, Eng::tool::MessageReader& r);

    // ==================== 交互 ====================
    void HandleDig(game::core::ecs::Entity& playerEntity);
    void HandlePlace(game::core::ecs::Entity& playerEntity, BlockId id);
    void AddToInventory(game::core::ecs::Entity& playerEntity, BlockId id, int count);

    // ==================== 广播 ====================
    void BroadcastWorldState();
    void BroadcastBlockChange(int bx, int by, int bz, BlockId id);

    // ==================== 碰撞 ====================
    bool IsSolidAt(int wx, int wy, int wz) const;
    bool AABBCollides(const glm::vec3& pos) const;
};

}  // namespace game::server