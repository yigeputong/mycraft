#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <memory>
#include "client/Render/RenderAPI.h"

namespace mycraft {

class Window final {
public:
    enum class RenderAPItype {
        OPENGL,
        OPENGLES,
        VULKAN,
        DIRECTX,
        METAL
    };

    static Window& getInstance();

    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Init(const std::string& title, int width, int height, RenderAPItype apitype);

    // SDL3 主回调将调用的三个核心函数
    void Update(float deltaTime);
    void Render();
    bool HandleEvent(SDL_Event& event);  // 返回 true 表示请求退出

    // Getter（供外部使用）
    SDL_Window* getWindow() const { return m_window; }
    bool isRunning() const { return m_running; }

    void Shutdown();

private:
    Window();

    SDL_Window* m_window;
    RenderAPItype m_apitype;
    std::unique_ptr<IRenderAPI> m_renderapi;

    std::string m_title;
    int m_width;
    int m_height;
    bool m_running;
    SDL_GLContext glContext;

    bool getAPI();
    bool CreateOpenGLWindow();
    bool CreateVulkanWindow();
};

}