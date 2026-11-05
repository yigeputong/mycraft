#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include "Engine/tool/Configs.h"

namespace Eng::tool {
    class Window;
}

namespace Eng::service {
    
class InputService;

class WindowService {
public:
    /// @brief 构造时显式注入 InputService。
    /// @param inputService 可为空；若为空，PollEvents 将不会转发输入事件（对应"Input 未初始化"场景）。
    explicit WindowService(service::InputService* inputService = nullptr);
    ~WindowService();

    WindowService(const WindowService&) = delete;
    WindowService& operator=(const WindowService&) = delete;

    // ---- 可选后置注入（例如 Engine 先构造 WindowService、后构造 InputService 时） ----
    void SetInputService(service::InputService* inputService) { m_inputService = inputService; }
    service::InputService* GetInputService() const { return m_inputService; }

    /// @brief 创建窗口，返回窗口 ID（0 表示失败）。
    uint32_t CreateWindow(const tool::WindowConfig& config);

    void DestroyWindow(uint32_t id);

    tool::Window* GetWindow(uint32_t id);

    const std::vector<uint32_t>& GetAllWindowIds() const { return m_windowIds; }

    void SetMainWindow(uint32_t id) { m_mainWindowId = id; }
    tool::Window* GetMainWindow();

    // 轮询事件（含 Input 转发；若未注入 InputService 则跳过转发）
    void PollEvents();

    bool HasAnyWindow() const;

    void Update();

private:
    std::unordered_map<uint32_t, std::unique_ptr<tool::Window>> m_windows;
    std::vector<uint32_t> m_windowIds;
    uint32_t m_nextId = 1;
    uint32_t m_mainWindowId = 0;

    service::InputService* m_inputService = nullptr;  // 观察指针，不持有

    void DispatchEvent(const SDL_Event& event);
};

} // namespace Eng::service