#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Eng::client {

class Window;

enum class RenderAPItype {
    OPENGL,
    VULKAN,
    DIRECTX12,
    METAL
};

struct WindowConfig {
    std::string title = "Engine Window";
    int windowWidth = 800;
    int windowHeight = 600;
    RenderAPItype apitype = RenderAPItype::OPENGL;
    bool fullscreen = false;
    bool resizable = true;
    bool relativeMode = false;
};

class WindowManager {
public:

    static WindowManager* GetInstance();
    ~WindowManager();
    
    
    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    // 创建窗口，返回窗口 ID
    // return 0; failed
    uint32_t CreateWindow(const WindowConfig& config);

    void DestroyWindow(uint32_t id);

    Window* GetWindow(uint32_t id);
    const Window* GetWindow(uint32_t id) const;

    // 获取所有窗口id
    const std::vector<uint32_t>& GetAllWindowIds() const { return m_windowIds; }

    // 设置/获取主窗口
    void SetMainWindow(uint32_t id) { m_mainWindowId = id; }
    Window* GetMainWindow();

    // 轮询事件
    void PollEvents();

    // 检查是否有任何窗口仍然打开
    bool HasAnyWindow() const { return !m_windows.empty(); }

    // 更新所有窗口（交换缓冲区等）
    void Update();

private:
    WindowManager() = default;
    std::unordered_map<uint32_t, std::unique_ptr<Window>> m_windows;
    std::vector<uint32_t> m_windowIds;  // 保持创建顺序
    uint32_t m_nextId = 1;
    uint32_t m_mainWindowId = 0;

    void DispatchEvent(const SDL_Event& event);
};
    
} // namespace Eng::client
