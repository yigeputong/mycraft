#include "core/Log.h"
#include "game/GameServer.h"
#include "game/Protocol.h"
#include "core/MessageReader.h"
#include <SDL3/SDL.h>
#include "core/NetworkServer.h"

namespace game {

bool GameServer::Start(uint16_t port) {
    if (m_running.load()) return false;
    m_running.store(true);
    m_thread = std::thread([this, port]() {
        // 服务端线程里初始化监听
        if (!m_server.Listen(port)) {
            logError(m_logger, "[Server] Listen failed");
            m_running.store(false);
            return;
        }

        m_server.SetMessageCallback([this](int id, const std::vector<uint8_t>& d) {
            HandleMessage(id, d);
        });
        m_server.SetDisconnectCallback([this](int id) {
            logInfo(m_logger, "[Server] Client " << id << " disconnected");
        });

        Run();
    });
    return true;
}

void GameServer::Stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    m_server.Shutdown();
    if (m_thread.joinable()) m_thread.join();
}

void GameServer::Run() {
    const float tickInterval = 1.0f / 20.0f;
    auto last = std::chrono::steady_clock::now();

    logInfo(m_logger, "[Server] Server Run");

    while (m_running.load()) {
        m_server.PollAccept();
        m_server.Update();

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now - last).count() >= tickInterval) {
            last = now;
            // TODO: 世界模拟、广播状态
        }

        SDL_Delay(1);
    }
}

void GameServer::HandleMessage(int clientId, const std::vector<uint8_t>& data) {
    if (data.empty()) return;
    auto type = static_cast<net::MessageType>(data[0]);
    Eng::MessageReader reader(std::span<const uint8_t>(data).subspan(1));

    switch (type) {
        case net::MessageType::PlayerInput: {
            net::PlayerInput input;
            input.moveX = reader.Read<float>();
            input.moveZ = reader.Read<float>();
            input.yaw   = reader.Read<float>();
            input.pitch = reader.Read<float>();
            input.jump  = reader.Read<uint8_t>() != 0;
            // TODO: 应用到玩家
            break;
        }
        default:
            logWarning(m_logger, "[Server] Unknown message type");
    }
}

} // namespace game