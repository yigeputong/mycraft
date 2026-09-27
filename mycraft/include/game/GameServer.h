#pragma once
#include "core/NetworkServer.h"
#include "core/Log.h"
#include "game/Protocol.h"
#include <memory>
#include <thread>
#include <atomic>
#include <glm/glm.hpp>

namespace game {

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

    std::vector<ServerPlayer> m_players;
    const float m_moveSpeed = 5.0f;

    void Run();
    void HandleMessage(int clientId, const std::vector<uint8_t>& data);

    void ApplyPlayerInput(int clientId, const net::PlayerInput& in);
    void TickWorld(float dt);
    void BroadcastWorldState();
};

} // namespace game