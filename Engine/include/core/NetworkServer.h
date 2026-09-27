#pragma once
#include "core/NetworkChannel.h"
#include "core/Log.h"
#include <functional>
#include <memory>

namespace Eng {
    
class NetworkServer {
public:
    NetworkServer() = default;
    ~NetworkServer() = default;

    NetworkServer(const NetworkServer&) = delete;
    NetworkServer& operator=(const NetworkServer&) = delete;

    bool Listen(uint16_t port);

    bool PollAccept();

    void Broadcast(std::span<const uint8_t> data);
    bool SendTo(int clientId, std::span<const uint8_t> data);

    using MessageCallback = std::function<void(int clientId, const std::vector<uint8_t>&)>;
    using ConnectCallback = std::function<void(int clientId)>;
    using DisconnectCallback = std::function<void(int clientId)>;

    void SetMessageCallback(MessageCallback cb) { m_messageCb = std::move(cb); }
    void SetConnectCallback(DisconnectCallback cb) { m_connectCb = std::move(cb); }
    void SetDisconnectCallback(DisconnectCallback cb) { m_disconnectCb = std::move(cb); }

    void Update();

    size_t GetClientCount() const { return m_clients.size(); }
    void Shutdown();

private:
    struct Client {
        int id;
        NetworkChannel channel;
        bool alive = true;
    };

    std::unique_ptr<Log> m_logger = std::make_unique<Log>();

    NET_Address* m_addr = nullptr;
    NET_Server*  m_server = nullptr;
    std::vector<Client> m_clients;
    int m_nextClientId = 1;

    MessageCallback m_messageCb;
    ConnectCallback m_connectCb;
    DisconnectCallback m_disconnectCb;
};

}
