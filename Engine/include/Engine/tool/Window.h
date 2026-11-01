#pragma once

//standards
#include <string>
#include <functional>

//libraries
#include <SDL3/SDL.h>

//clients
#include "Engine/tool/Configs.h"

namespace Eng::tool {

class Window final {
public:

    Window(const WindowConfig& config);
    ~Window();

    bool IsValid() const;

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

    // ★ 保留：API 探测降级原因（Engine / MyGame 会读取并弹窗）
    std::string GetFallbackReason() const { return m_fallbackReason; }

    // 事件回调
    std::function<void(const SDL_Event&)> onEvent;
    std::function<void(int newWidth, int newHeight)> onResize;
    std::function<void()> onCloseRequest;
    std::function<void(bool hasFocus)> onFocusChanged;

private:

    SDL_Window* m_window = nullptr;

    WindowConfig m_config;
    bool m_fullscreen = false;
    int m_windowedHeight =  m_config.windowHeight;
    int m_windowedWidth = m_config.windowWidth;

    bool m_running = true;
    bool m_createSuccess = false;
    std::string m_fallbackReason;

    bool CreateWindow();
    bool CreateOpenGLWindow();
    bool CreateVulkanWindow();
};

}