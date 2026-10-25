#include "client/Window.h"
#include "client/Render/OpenGLAPI.h"
#include "client/Render/VulkanAPI.h"
#include <SDL3/SDL_video.h>
#include <glad/glad.h>
#include <iostream>

namespace Eng::client {

Window::Window(const WindowConfig& config)
    :m_config(config) {
    m_createSuccess = CreateWindow();    
}

Window::~Window() {
    m_renderapi.reset();
    if (m_glContext) {
        SDL_GL_DestroyContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

bool Window::IsValid() {
    return m_createSuccess;
}

void Window::SwapBuffers() {
    SDL_GL_SwapWindow(m_window);
}

void Window::SetRelativeMode(bool enabled) {
    SDL_SetWindowRelativeMouseMode(m_window, enabled);
}

void Window::SetFullscreen(bool enabled) {
    SDL_Window* sdlWin = m_window;

    if (enabled) {
        // 保存当前窗口尺寸（用于退出全屏时恢复）
        SDL_GetWindowSize(sdlWin, &m_windowedWidth, &m_windowedHeight);
        SDL_SetWindowFullscreen(sdlWin, true);   // 就这一行
    } else {
        SDL_SetWindowFullscreen(sdlWin, false);
        SDL_SetWindowBordered(sdlWin, true);
        SDL_SetWindowSize(sdlWin, m_windowedWidth, m_windowedHeight);
        SDL_SetWindowPosition(sdlWin, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    SDL_GetWindowSize(sdlWin, &m_config.windowWidth, &m_config.windowHeight);
    SDL_GetWindowSizeInPixels(sdlWin, &m_config.windowPixelWidth, &m_config.windowPixelHeight);

    if (m_renderapi) {
        m_renderapi->SetViewport(0, 0, m_config.windowPixelWidth, m_config.windowPixelHeight);
    }
}

bool Window::CreateWindow() {
    // ★ 1. 处理 AUTO 和降级逻辑
    if (m_config.apitype == RenderAPItype::AUTO) {
        auto vk = VulkanAPI::Probe();
        if (vk.available) {
            m_config.apitype = RenderAPItype::VULKAN;
        } else {
            auto gl = OpenGLAPI::Probe();
            if (gl.available) {
                m_config.apitype = RenderAPItype::OPENGL;
                // 记录降级原因，供 MyGame 读取并弹窗
                m_fallbackReason = "Vulkan 不可用，已自动降级到 OpenGL。\n原因: " + vk.reason;
            } else {
                // 既然 Probe 的 reason 里已经包含了 (4.5 / 1.4) 等信息，直接拼接即可
                throw std::runtime_error(std::format(
                    "硬件不支持任何可用的图形 API。\nVulkan: {}\nOpenGL: {}", 
                    vk.reason, gl.reason));
            }
        }
    } else if (m_config.apitype == RenderAPItype::VULKAN) {
        auto vk = VulkanAPI::Probe();
        if (!vk.available) {
            auto gl = OpenGLAPI::Probe();
            if (gl.available) {
                m_config.apitype = RenderAPItype::OPENGL;
                m_fallbackReason = "Vulkan 不可用，已自动降级到 OpenGL。\n原因: " + vk.reason;
            } else {
                throw std::runtime_error(std::format(
                    "强制使用 Vulkan 失败，且 OpenGL 不可用。\nVulkan: {}\nOpenGL: {}", 
                    vk.reason, gl.reason));
            }
        }
    } else if (m_config.apitype == RenderAPItype::OPENGL) {
        auto gl = OpenGLAPI::Probe();
        if (!gl.available) {
            throw std::runtime_error(std::format(
                "强制使用 OpenGL 失败。\n原因: {}", gl.reason));
        }
    }

    // ★ 2. 根据决定好的 API 创建窗口
    switch (m_config.apitype) {
    case RenderAPItype::OPENGL:
        return CreateOpenGLWindow();
    case RenderAPItype::VULKAN:
        return CreateVulkanWindow();
    default:
        return false;
    }
}

bool Window::CreateOpenGLWindow() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, OpenGLAPI::api_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, OpenGLAPI::api_minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    m_window = SDL_CreateWindow(
        m_config.title.c_str(),
        m_config.windowWidth,
        m_config.windowHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!m_window) {
        SDL_Log("SDL_CreateWindow Error: %s", SDL_GetError());
        return false;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        SDL_Log("OpenGL Context Error: %s", SDL_GetError());
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    SDL_GL_SetSwapInterval(m_config.vsync ? 1 : 0);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cout << "Failed to initalize GLAD." << '\n';
        return false;
    }

    m_renderapi = std::make_unique<OpenGLAPI>();
    return true;
}

bool Window::CreateVulkanWindow() {
    m_window = SDL_CreateWindow(
        m_config.title.c_str(),
        m_config.windowWidth,
        m_config.windowHeight,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    if (!m_window) {
        SDL_Log("SDL_CreateWindow Error: %s", SDL_GetError());
        return false;
    }

    m_renderapi = std::make_unique<VulkanAPI>();
    return true;
}

}