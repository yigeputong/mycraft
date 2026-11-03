#pragma once

#include <thread>
#include <stop_token>
#include <functional>
#include <utility>

namespace Eng::tool {

/**
 * @brief 自动 join 的线程包装器，支持 std::stop_token 优雅中断。
 * @note 替代 std::jthread，以兼容 Android NDK 和 iOS 旧版 libc++。
 */
class RAIIThread {
public:
    using Task = std::function<void(const std::stop_token&)>;

    explicit RAIIThread(Task task) : m_task(std::move(task)) {
        m_thread = std::thread([this] {
            m_task(m_stopSource.get_token());
        });
    }

    ~RAIIThread() {
        m_stopSource.request_stop();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    RAIIThread(const RAIIThread&) = delete;
    RAIIThread& operator=(const RAIIThread&) = delete;

    RAIIThread(RAIIThread&& other) noexcept
        : m_stopSource(std::move(other.m_stopSource)),
          m_thread(std::move(other.m_thread)),
          m_task(std::move(other.m_task)) {}

    RAIIThread& operator=(RAIIThread&& other) noexcept {
        if (this != &other) {
            if (m_thread.joinable()) m_thread.join();
            m_stopSource = std::move(other.m_stopSource);
            m_thread = std::move(other.m_thread);
            m_task = std::move(other.m_task);
        }
        return *this;
    }

    void RequestStop() { m_stopSource.request_stop(); }

private:
    std::stop_source m_stopSource;
    std::thread m_thread;
    Task m_task;
};

} // namespace Eng