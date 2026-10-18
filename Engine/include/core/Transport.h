#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <string>
#include <functional>
#include <memory>

namespace Eng {

class IServerTransport {
public:
    virtual ~IServerTransport() = default;
    virtual bool Listen(uint16_t port) = 0;
    virtual void Update() = 0;
    virtual void Broadcast(std::span<const uint8_t> data) = 0;
    virtual bool SendTo(int clientId, std::span<const uint8_t> data) = 0;
    virtual void Shutdown() = 0;

    virtual void SetMessageCallback(std::function<void(int, const std::vector<uint8_t>&)> cb) = 0;
    virtual void SetConnectCallback(std::function<void(int)> cb) = 0;
    virtual void SetDisconnectCallback(std::function<void(int)> cb) = 0;
};

class IClientTransport {
public:
    virtual ~IClientTransport() = default;
    virtual bool Connect(const std::string& host, uint16_t port) = 0;
    virtual void Disconnect() = 0;
    virtual bool Send(std::span<const uint8_t> data) = 0;
    virtual bool Receive(std::vector<uint8_t>& outData) = 0;
};

} // namespace Eng