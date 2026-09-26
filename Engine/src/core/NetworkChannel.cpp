#include "core/NetworkChannel.h"
#include <SDL3/SDL.h>

namespace Eng {

NetworkChannel::~NetworkChannel() {
    Disconnect();
}

NetworkChannel::NetworkChannel(NetworkChannel&& other) noexcept
    : m_addr(other.m_addr)
    , m_server(other.m_server)
    , m_socket(other.m_socket)
    , m_connected(other.m_connected) {
    other.m_addr = nullptr;
    other.m_server = nullptr;
    other.m_socket = nullptr;
    other.m_connected = false;
}

NetworkChannel& NetworkChannel::operator=(NetworkChannel&& other) noexcept {
    if (this != &other) {
        Disconnect();

        m_addr = other.m_addr;
        m_server = other.m_server;
        m_socket = other.m_socket;
        m_connected = other.m_connected;

        other.m_addr = nullptr;
        other.m_server = nullptr;
        other.m_socket = nullptr;
        other.m_connected = false;
    }
    return *this;
}

void NetworkChannel::Disconnect() {
    if (m_socket) {
        NET_DestroyStreamSocket(m_socket);
        m_socket = nullptr;
    }
    if (m_server) {
        NET_DestroyServer(m_server);
        m_server = nullptr;
    }
    if (m_addr) {
        NET_UnrefAddress(m_addr);
        m_addr = nullptr;
    }
    m_connected = false;
}

// ===== 服务器 =====
bool NetworkChannel::Listen(const std::string& host, uint16_t port, int timeout) {
    m_addr = NET_ResolveHostname(host.c_str());
    if (!m_addr) return false;
    if (NET_WaitUntilResolved(m_addr, timeout) != NET_SUCCESS) return false;

    m_server = NET_CreateServer(m_addr, port, 0);
    return m_server != nullptr;
}

bool NetworkChannel::AcceptClient() {
    if (!m_server) return false;

    NET_StreamSocket* client = nullptr;
    while (!client) {
        NET_AcceptClient(m_server, &client);
        SDL_Delay(10);
    }
    m_socket = client;
    m_connected = true;
    return true;
}

bool NetworkChannel::TryAcceptClient() {
    if (!m_server) return false;

    NET_StreamSocket* client = nullptr;
    NET_AcceptClient(m_server, &client);
    if (!client) return false;

    m_socket = client;
    m_connected = true;
    return true;
}

// ===== 客户端 =====
bool NetworkChannel::Connect(const std::string& host, uint16_t port, int timeout) {
    m_addr = NET_ResolveHostname(host.c_str());
    if (!m_addr) return false;
    if (NET_WaitUntilResolved(m_addr, timeout) != NET_SUCCESS) return false;

    m_socket = NET_CreateClient(m_addr, port, 0);
    if (!m_socket) return false;

    if (!NET_WaitUntilConnected(m_socket, timeout)) {
        NET_DestroyStreamSocket(m_socket);
        m_socket = nullptr;
        return false;
    }

    m_connected = true;
    return true;
}

// ===== 收发 =====
bool NetworkChannel::SendMessage(const std::string& msg) {
    if (!m_socket || !m_connected) return false;

    uint32_t len = SDL_Swap32BE(static_cast<uint32_t>(msg.size()));
    if (!NET_WriteToStreamSocket(m_socket, &len, 4)) {
        m_connected = false;
        return false;
    }
    if (!msg.empty()) {
        if (!NET_WriteToStreamSocket(m_socket, msg.data(), static_cast<int>(msg.size()))) {
            m_connected = false;
            return false;
        }
    }
    return true;
}

bool NetworkChannel::Send(std::span<const uint8_t> data) {
    if (!m_socket || !m_connected) return false;

    uint32_t len = SDL_Swap32BE(static_cast<uint32_t>(data.size()));
    if (!NET_WriteToStreamSocket(m_socket, &len, 4)) {
        m_connected = false;
        return false;
    }

    if (!data.empty()) {
        if (!NET_WriteToStreamSocket(m_socket,
                data.data(), static_cast<int>(data.size()))) {
            m_connected = false;
            return false;
        }
    }
    return true;
}

bool NetworkChannel::ReceiveMessage(std::string& outMsg) {
    if (!m_socket || !m_connected) return false;

    // 等待数据（非阻塞，0ms 超时）
    void* sockets[] = { m_socket };
    int result = NET_WaitUntilInputAvailable(sockets, 1, 0);
    if (result <= 0) return false;  // 没数据或出错

    // 读 4 字节长度
    uint32_t len = 0;
    if (!ReadExact(&len, 4)) {
        m_connected = false;
        return false;
    }
    len = SDL_Swap32BE(len);

    // 读内容
    outMsg.resize(len);
    if (len > 0 && !ReadExact(&outMsg[0], len)) {
        m_connected = false;
        return false;
    }
    return true;
}

bool NetworkChannel::Receive(std::vector<uint8_t>& outData) {
    if (!m_socket || !m_connected) return false;

    // 非阻塞检查（0ms 超时）
    void* sockets[] = { m_socket };
    if (NET_WaitUntilInputAvailable(sockets, 1, 0) <= 0) {
        return false;  // 没数据，正常返回 false
    }

    // 读长度
    uint32_t len = 0;
    if (!ReadExact(&len, 4)) {
        m_connected = false;
        return false;
    }
    len = SDL_Swap32BE(len);

    // 读数据
    outData.resize(len);
    if (len > 0 && !ReadExact(outData.data(), len)) {
        m_connected = false;
        return false;
    }
    return true;
}

bool NetworkChannel::ReadExact(void* buffer, size_t size) {
    size_t totalRead = 0;
    while (totalRead < size) {
        void* sockets[] = { m_socket };
        if (NET_WaitUntilInputAvailable(sockets, 1, 100) <= 0) {
            return false;  // 超时或断开
        }
        int n = NET_ReadFromStreamSocket(m_socket,
                    static_cast<char*>(buffer) + totalRead,
                    static_cast<int>(size - totalRead));
        if (n <= 0) return false;
        totalRead += n;
    }
    return true;
}

NetworkChannel NetworkChannel::FromAccepted(NET_StreamSocket* socket) {
    NetworkChannel ch(socket);
    return ch;
}

} // namespace Eng::net