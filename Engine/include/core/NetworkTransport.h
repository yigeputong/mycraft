#pragma once
#include "core/Transport.h"
#include "core/NetworkServer.h"
#include "core/NetworkChannel.h"

namespace Eng {

class NetworkServerTransport : public IServerTransport {
public:
    bool Listen(uint16_t port) override { return m_server.Listen(port); }
    void Update() override { m_server.PollAccept(); m_server.Update(); }
    void Broadcast(std::span<const uint8_t> data) override { m_server.Broadcast(data); }
    bool SendTo(int clientId, std::span<const uint8_t> data) override { return m_server.SendTo(clientId, data); }
    void Shutdown() override { m_server.Shutdown(); }

    void SetMessageCallback(std::function<void(int, const std::vector<uint8_t>&)> cb) override { m_server.SetMessageCallback(std::move(cb)); }
    void SetConnectCallback(std::function<void(int)> cb) override { m_server.SetConnectCallback(std::move(cb)); }
    void SetDisconnectCallback(std::function<void(int)> cb) override { m_server.SetDisconnectCallback(std::move(cb)); }

private:
    NetworkServer m_server;
};

class NetworkClientTransport : public IClientTransport {
public:
    bool Connect(const std::string& host, uint16_t port) override { return m_channel.Connect(host, port); }
    void Disconnect() override { m_channel.Disconnect(); }
    bool Send(std::span<const uint8_t> data) override { return m_channel.Send(data); }
    bool Receive(std::vector<uint8_t>& outData) override { return m_channel.Receive(outData); }

private:
    NetworkChannel m_channel;
};

} // namespace Eng