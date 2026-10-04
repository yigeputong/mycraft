#pragma once

//standards
#include <string>
#include <memory>
#include <functional>

//libraries
#include <SDL3/SDL.h>

//clients
#include "client/Render/RenderAPI.h"
#include "client/WindowManager.h"

namespace Eng::client {

class Window final {
public:

    Window(const WindowConfig& config);
    ~Window();

    bool IsValid();

    void SwapBuffers();

    void SetRelativeMode(bool enabled);
    void SetFullscreen(bool enabled);
    bool IsFullscreen() const;

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Getter/Setter
    WindowConfig& GetConfigs() { return m_config; }
    void SetConfigs(const WindowConfig& config) { m_config = config; }
    
    bool GetRunning() const { return m_running; }
    void SetRunning(bool running) { m_running = running; }

    float GetTime() const { return SDL_GetTicks() / 1000.0f; }
    float GetAspectRatio() const { return static_cast<float>(m_config.windowPixelWidth) / m_config.windowPixelHeight; }
    SDL_Window* GetSDLWindow() const { return m_window; }
    SDL_GLContext GetGLContext() const { return m_glContext; }
    IRenderAPI* GetAPI() const { return m_renderapi.get(); }
    
    // 事件回调
    std::function<void(const SDL_Event&)> onEvent;
    // 窗口大小变化回调（逻辑像素）
    std::function<void(int newWidth, int newHeight)> onResize;
    // 窗口关闭请求回调
    std::function<void()> onCloseRequest;
    // 窗口获得/失去焦点回调
    std::function<void(bool hasFocus)> onFocusChanged;

private:

    SDL_Window* m_window;
    SDL_GLContext m_glContext = nullptr;

    WindowConfig m_config;
    bool m_fullscreen = false;
    int m_windowedHeight =  m_config.windowHeight;
    int m_windowedWidth = m_config.windowWidth;
    std::unique_ptr<IRenderAPI> m_renderapi;

    bool m_running;

    bool m_createSuccess = false;

    bool CreateWindow();
    bool CreateOpenGLWindow();
    bool CreateVulkanWindow();
};

}