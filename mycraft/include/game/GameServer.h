#pragma once
#include "core/NetworkServer.h"
#include "core/Log.h"
#include <memory>
#include <thread>
#include <atomic>

namespace game {

class GameServer {
public:
    bool Start(uint16_t port);
    void Stop();
    bool IsRunning() const { return m_running.load(); }

private:
    void Run();
    void HandleMessage(int clientId, const std::vector<uint8_t>& data);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    Eng::NetworkServer m_server;
    std::unique_ptr<Eng::Log> m_logger = std::make_unique<Eng::Log>();
};

} // namespace game