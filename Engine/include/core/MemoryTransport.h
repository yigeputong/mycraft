#pragma once
#include "core/Transport.h"
#include <mutex>
#include <queue>

namespace Eng {

struct MemoryPipe {
    std::mutex mtx;
    std::queue<std::vector<uint8_t>> c2s;
    std::queue<std::vector<uint8_t>> s2c;
    bool connected = false;
};

class MemoryServerTransport : public IServerTransport {
public:
    std::shared_ptr<MemoryPipe> m_pipe = std::make_shared<MemoryPipe>();
    
    std::function<void(int, const std::vector<uint8_t>&)> m_msgCb;
    std::function<void(int)> m_connectCb;
    std::function<void(int)> m_disconnectCb;

    bool Listen(uint16_t) override { return true; }
    void Update() override;
    void Broadcast(std::span<const uint8_t> data) override;
    bool SendTo(int clientId, std::span<const uint8_t> data) override;
    void Shutdown() override;

    void SetMessageCallback(std::function<void(int, const std::vector<uint8_t>&)> cb) override { m_msgCb = std::move(cb); }
    void SetConnectCallback(std::function<void(int)> cb) override { m_connectCb = std::move(cb); }
    void SetDisconnectCallback(std::function<void(int)> cb) override { m_disconnectCb = std::move(cb); }
};

class MemoryClientTransport : public IClientTransport {
public:
    std::shared_ptr<MemoryPipe> m_pipe;
    MemoryClientTransport(std::shared_ptr<MemoryPipe> pipe) : m_pipe(pipe) {}
    
    bool Connect(const std::string&, uint16_t) override;
    void Disconnect() override;
    bool Send(std::span<const uint8_t> data) override;
    bool Receive(std::vector<uint8_t>& outData) override;
};

} // namespace Eng