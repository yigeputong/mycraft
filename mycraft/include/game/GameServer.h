#pragma once
#include "core/NetworkServer.h"
#include "core/Log.h"
#include "core/World.h"
#include "game/server/WorldGenerator.h"
#include "game/Protocol.h"
#include <memory>
#include <thread>
#include <atomic>
#include <glm/glm.hpp>
#include <queue>

namespace game::server {

class GameServer {
public:
    bool Start(uint16_t port);
    void Stop();
    bool IsRunning() const { return m_running.load(); }

private:
    private:
    // ==================== 生命周期 ====================
    static constexpr float TPS = 20.0f;
    std::thread        m_thread;
    std::atomic<bool>  m_running{false};

    // ==================== 网络 ====================
    Eng::NetworkServer        m_server;
    std::unique_ptr<Eng::Log> m_logger = std::make_unique<Eng::Log>();

    // ==================== 玩家 ====================
    struct ServerPlayer {
        uint32_t  id = 0;
        glm::vec3 position{0.0f, 60.0f, 0.0f};   // 脚底
        glm::vec3 velocity{0.0f};
        bool      onGround = false;
        bool      jump     = false;
        float     yaw   = 0.0f;
        float     pitch = 0.0f;
        glm::vec3 moveDir{0.0f};                  // 水平意图
    };
    std::vector<ServerPlayer> m_players;
    static constexpr float REACH_DISTANCE = 5.0f;

    // ==================== 世界 ====================
    TerrainGenerator                    m_terrain;
    std::unordered_map<uint64_t, Chunk> m_chunks;

    struct PendingChunkRequest { int clientId; int cx, cz; };
    std::queue<PendingChunkRequest> m_chunkQueue;

    // ==================== 指标 ====================
    float m_lastTickMs = 0.0f;

    // ==================== 方法 ====================
    void Run();
    void HandleMessage(int clientId, const std::vector<uint8_t>& data);
    void ApplyPlayerInput(int clientId, const net::PlayerInput& in);
    void TickWorld(float dt);

    // 区块
    Chunk& GetOrCreateChunk(int cx, int cz);

    // 方块查询/修改
    BlockType GetBlockAt(int wx, int wy, int wz) const;
    void      SetBlockAt(int wx, int wy, int wz, BlockType bt);
    bool      IsInsidePlayer(const ServerPlayer& p, int bx, int by, int bz);

    // 交互
    void HandleDig(ServerPlayer& p);
    void HandlePlace(ServerPlayer& p, BlockType type);

    // 广播
    void BroadcastWorldState();
    void BroadcastBlockChange(int bx, int by, int bz, BlockType type);

    // 碰撞
    bool IsSolidAt(int wx, int wy, int wz) const;
    bool AABBCollides(const glm::vec3& pos) const;   // pos = 脚底
};

} // namespace game