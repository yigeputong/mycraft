#include "core/MemoryTransport.h"

namespace Eng {

void MemoryServerTransport::Update() {
    // 1. 先在局部变量收集需要处理的数据（在锁的保护下）
    bool isConnected = false;
    std::queue<std::vector<uint8_t>> incoming;
    
    {
        std::lock_guard lock(m_pipe->mtx);
        if (m_pipe->connected) {
            isConnected = true;
            m_pipe->connected = false; // 消费掉
        }
        if (!m_pipe->c2s.empty()) {
            std::swap(incoming, m_pipe->c2s); // 瞬间把整个队列换出来，比 while pop 快
        }
    } // 锁自动释放

    // 2. 在无锁状态下，安全地调用回调
    if (isConnected && m_connectCb) {
        m_connectCb(1);
    }
    while (!incoming.empty()) {
        auto msg = std::move(incoming.front());
        incoming.pop();
        if (m_msgCb) m_msgCb(1, msg);
    }
}

void MemoryServerTransport::Broadcast(std::span<const uint8_t> data) {
    std::lock_guard lock(m_pipe->mtx);
    m_pipe->s2c.push({data.begin(), data.end()});
}

bool MemoryServerTransport::SendTo(int id, std::span<const uint8_t> data) {
    if (id == 1) { Broadcast(data); return true; }
    return false;
}

void MemoryServerTransport::Shutdown() {
    std::lock_guard lock(m_pipe->mtx);
    if (m_disconnectCb) m_disconnectCb(1);
}

bool MemoryClientTransport::Connect(const std::string&, uint16_t) {
    std::lock_guard lock(m_pipe->mtx);
    m_pipe->connected = true;
    return true;
}

void MemoryClientTransport::Disconnect() {
    std::lock_guard lock(m_pipe->mtx);
    m_pipe->connected = false;
}

bool MemoryClientTransport::Send(std::span<const uint8_t> data) {
    std::lock_guard lock(m_pipe->mtx);
    m_pipe->c2s.push({data.begin(), data.end()});
    return true;
}

bool MemoryClientTransport::Receive(std::vector<uint8_t>& outData) {
    std::lock_guard lock(m_pipe->mtx);
    if (m_pipe->s2c.empty()) return false;
    outData = std::move(m_pipe->s2c.front());
    m_pipe->s2c.pop();
    return true;
}

} // namespace Eng