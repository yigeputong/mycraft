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

namespace game::server {

class GameServer {
public:
    bool Start(uint16_t port);
    void Stop();
    bool IsRunning() const { return m_running.load(); }

private:
    static constexpr float TPS = 20.0f;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    Eng::NetworkServer m_server;
    std::unique_ptr<Eng::Log> m_logger = std::make_unique<Eng::Log>();

    struct ServerPlayer {
        uint32_t  id = 0;
        glm::vec3 position{0.0f, 10.0f, 0.0f};
        float     yaw = 0.0f;
        float     pitch = 0.0f;
        glm::vec3 moveDir{0.0f};
    };
    static constexpr float REACH_DISTANCE = 5.0f;

    std::vector<ServerPlayer> m_players;
    const float m_moveSpeed = 5.0f;

    // ---- 世界 ----
    TerrainGenerator m_terrain;
    std::unordered_map<uint64_t, Chunk> m_chunks;

    // 查得到直接返回引用；查不到就生成一份放进去
    Chunk& GetOrCreateChunk(int cx, int cz);

    void Run();
    void HandleMessage(int clientId, const std::vector<uint8_t>& data);
    void ApplyPlayerInput(int clientId, const net::PlayerInput& in);
    void TickWorld(float dt);

    BlockType GetBlockAt(int wx, int wy, int wz) const;
    void SetBlockAt(int wx, int wy, int wz, BlockType bt);
    bool IsInsidePlayer(const ServerPlayer& p, int bx, int by, int bz);

    void HandleDig(ServerPlayer& p);
    void HandlePlace(ServerPlayer& p);

    void BroadcastWorldState();
    void BroadcastBlockChange(int bx, int by, int bz, BlockType type);
};

} // namespace game