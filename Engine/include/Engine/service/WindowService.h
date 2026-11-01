#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include "Engine/tool/Configs.h"

namespace Eng::tool{

    class Window;

}

namespace Eng::service {

class WindowService {
public:

    WindowService();
    ~WindowService();
    
    
    WindowService(const WindowService&) = delete;
    WindowService& operator=(const WindowService&) = delete;

    /// @brief 创建窗口，返回窗口 ID
    /// @param config 
    /// @return 0: 失败
    uint32_t CreateWindow(const tool::WindowConfig& config);

    void DestroyWindow(uint32_t id);

    tool::Window* GetWindow(uint32_t id);

    // 获取所有窗口id
    const std::vector<uint32_t>& GetAllWindowIds() const { return m_windowIds; }

    // 设置/获取主窗口
    void SetMainWindow(uint32_t id) { m_mainWindowId = id; }
    tool::Window* GetMainWindow();

    // 轮询事件
    void PollEvents();

    // 检查是否有任何窗口仍然打开（正在运行）
    bool HasAnyWindow() const;

    // 更新所有窗口（交换缓冲区等）
    void Update();

private:
    std::unordered_map<uint32_t, std::unique_ptr<tool::Window>> m_windows;
    std::vector<uint32_t> m_windowIds;  // 保持创建顺序
    uint32_t m_nextId = 1;
    uint32_t m_mainWindowId = 0;

    void DispatchEvent(const SDL_Event& event);
};
    
} // namespace Eng::client
