#include "core/Log.h"
#include "game/GameServer.h"
#include "game/Protocol.h"
#include "core/MessageReader.h"
#include "core/MessageWriter.h"
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

        m_server.SetConnectCallback([this](int id) {
            ServerPlayer p;
            p.id = (uint32_t)id;
            p.position = glm::vec3(0.0f, 10.0f, 0.0f);
            m_players.push_back(p);

            // 只发一条"你的 ID"给这个客户端
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(net::MessageType::PlayerId));   // YourId
            w.WriteU32BE((uint32_t)id);
            m_server.SendTo(id, w.GetBuffer());

            logInfo(m_logger, "[Server] Player " << id << " joined");
        });

        m_server.SetMessageCallback([this](int id, const std::vector<uint8_t>& data) {
            HandleMessage(id, data);
        });

        m_server.SetDisconnectCallback([this](int id) {
            m_players.erase(std::remove_if(m_players.begin(), m_players.end(),
                [id](const ServerPlayer& p) { return p.id == (uint32_t)id; }), m_players.end());
            logInfo(m_logger, "[Server] Player " << id << " left");
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
    constexpr float TICK_INTERVAL = 1.0f / TPS;   // 20 TPS
    auto last = std::chrono::steady_clock::now();
    float accumulator = 0.0f;

    logInfo(m_logger, "[Server] Server Run");

    while (m_running.load()) {
        // 1. 网络收发
        m_server.PollAccept();
        m_server.Update();

        // 2. 累积时间
        auto now = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(now - last).count();
        last = now; // 每帧都更新
        accumulator += frameTime;

        // 3. 固定步长 tick
        while (accumulator >= TICK_INTERVAL) {
            TickWorld(TICK_INTERVAL);
            BroadcastWorldState();
            accumulator -= TICK_INTERVAL;
        }

        SDL_Delay(1);
    }
}

void GameServer::HandleMessage(int clientId, const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    auto type = static_cast<game::net::MessageType>(data[0]);
    Eng::MessageReader r(std::span<const uint8_t>(data).subspan(1));

    switch (type) {
        case game::net::MessageType::PlayerInput: {
            game::net::PlayerInput in;
            in.moveDir = r.ReadVec3();      // ★ vec3
            in.look    = r.ReadVec2();
            in.jump    = r.Read<uint8_t>() != 0;

            for (auto& p : m_players) {
                if (p.id == (uint32_t)clientId) {
                    p.moveDir = in.moveDir;     // ★ 存世界方向
                    p.yaw     = in.look.x;
                    p.pitch   = in.look.y;
                    break;
                }
            }
            break;
        }
    }
}

void GameServer::ApplyPlayerInput(int clientId, const net::PlayerInput& in) {
    for (auto& p : m_players) {
        if (p.id == (uint32_t)clientId) {
            p.moveDir = in.moveDir;
            p.yaw     = in.look.x;
            p.pitch   = in.look.y;
            return;
        }
    }
}

void GameServer::TickWorld(float dt) {
    for (auto& p : m_players) {
        if (glm::length(p.moveDir) > 0.001f) {
            glm::vec3 delta = glm::normalize(p.moveDir);
            p.position += delta * m_moveSpeed * dt;
        }
    }
}

void GameServer::BroadcastWorldState() {
    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::WorldState));
    w.WriteU32BE((uint32_t)m_players.size());

    for (auto& p : m_players) {
        w.WriteU32BE(p.id);
        w.WriteVec3(p.position);
        w.Write(p.yaw);
        w.Write(p.pitch);
    }
    m_server.Broadcast(w.GetBuffer());
}

} // namespace game