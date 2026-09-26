#pragma once
#include <string>
#include <span>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

namespace Eng {

class NetworkChannel {
public:
    NetworkChannel() = default;
    ~NetworkChannel();

    NetworkChannel(const NetworkChannel&) = delete;
    NetworkChannel& operator=(const NetworkChannel&) = delete;

    NetworkChannel(NetworkChannel&& other) noexcept;
    NetworkChannel& operator=(NetworkChannel&& other) noexcept;

    // Server

    bool Listen(const std::string& host, uint16_t port, int timeout = 5000);
    // Block
    bool AcceptClient();

    bool TryAcceptClient();

    // Client

    // Block
    bool Connect(const std::string& host, uint16_t port, int timeout = 5000);

    // Both

    // data structure
    // | length(4byte) | data/outData |

    bool SendMessage(const std::string& msg);
    bool Send(std::span<const uint8_t> data);
    // Not block, false -> no data
    bool ReceiveMessage(std::string& outMsg);
    bool Receive(std::vector<uint8_t>& outData);

    bool IsConnected() const { return m_connected; }
    void Disconnect();

    static NetworkChannel FromAccepted(NET_StreamSocket* socket);

private:
    NET_Address*      m_addr   = nullptr;
    NET_Server*       m_server = nullptr;
    NET_StreamSocket* m_socket = nullptr;
    bool              m_connected = false;

    bool ReadExact(void* buffer, size_t size);

    explicit NetworkChannel(NET_StreamSocket* socket)
        : m_socket(socket), m_connected(true) {}
};

} // namespace Eng