#pragma once

#include <vector>
#include <functional>
#include <cstdint>

namespace Eng::tool {

/**
 * @brief 通用的多帧延迟销毁器
 * @details 用于处理图形 API 的 Frame-in-Flight（在途帧）资源安全释放。
 *          业务层和渲染层通过 RAII 释放智能指针，RHI 底层拦截后将其塞入此队列，
 *          等待 GPU 彻底执行完 N 帧后再实际调用底层 API 销毁。
 */
class DeferredDeleter {
public:
    // 将一个销毁动作推迟到 framesToWait 帧之后
    void Defer(std::function<void()> deleter, uint64_t currentFrame, uint32_t framesToWait = 2) {
        m_pendingDeletes.push_back({ currentFrame + framesToWait, std::move(deleter) });
    }

    // 每帧调用一次，执行过期的销毁动作
    void Process(uint64_t currentFrame) {
        for (auto it = m_pendingDeletes.begin(); it != m_pendingDeletes.end(); ) {
            if (it->retireFrame <= currentFrame) {
                it->deleter();
                it = m_pendingDeletes.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 引擎关闭时强制清空所有未过期的销毁动作
    void Flush() {
        for (auto& item : m_pendingDeletes) {
            item.deleter();
        }
        m_pendingDeletes.clear();
    }

private:
    struct PendingItem {
        uint64_t              retireFrame;
        std::function<void()> deleter;
    };
    std::vector<PendingItem> m_pendingDeletes;
};

} // namespace Eng::tool