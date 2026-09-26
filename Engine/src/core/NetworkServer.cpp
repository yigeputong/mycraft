#include "core/NetworkServer.h"
#include "core/Log.h"

namespace Eng {

    bool NetworkServer::Listen(uint16_t port) {
    m_addr = NET_ResolveHostname("localhost");
    if (!m_addr) return false;
    if (NET_WaitUntilResolved(m_addr, 5000) != NET_SUCCESS) return false;

    m_server = NET_CreateServer(m_addr, port, 0);
    return m_server != nullptr;
}

bool NetworkServer::PollAccept() {
    if (!m_server) return false;

    NET_StreamSocket* socket = nullptr;
    NET_AcceptClient(m_server, &socket);
    if (!socket) return false;

    Client client;
    client.id = m_nextClientId++;
    client.channel = NetworkChannel::FromAccepted(socket);
    m_clients.push_back(std::move(client));

    logInfo(m_logger, "[Server] Client " << client.id << " connected");
    return true;
}

void NetworkServer::Update() {
    for (auto it = m_clients.begin(); it != m_clients.end(); ) {
        std::vector<uint8_t> data;
        while (it->channel.Receive(data)) {
            if (m_messageCb) m_messageCb(it->id, data);
        }

        if (!it->channel.IsConnected()) {
            if (m_disconnectCb) m_disconnectCb(it->id);
            logInfo(m_logger, "[Server] Client " << it->id << " disconnected");
            it = m_clients.erase(it);
        } else {
            ++it;
        }
    }
}

void NetworkServer::Broadcast(std::span<const uint8_t> data) {
    for (auto& client : m_clients) {
        client.channel.Send(data);
    }
}

void NetworkServer::Shutdown() {
    m_clients.clear();
    if (m_server) { NET_DestroyServer(m_server); m_server = nullptr; }
    if (m_addr) { NET_UnrefAddress(m_addr); m_addr = nullptr; }
}

}