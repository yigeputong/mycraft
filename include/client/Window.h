#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
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

    void Run();

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
    RenderAPItype m_apitype;

    bool getAPI();
    bool CreateOpenGLWindow();
    bool CreateVulkanWindow();
};

}