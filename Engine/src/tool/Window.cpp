#include "Engine/tool/Window.h"
#include "Engine/service/OpenGLRenderService.h"
#include "Engine/service/VulkanRenderService.h"

#include <SDL3/SDL_video.h>
#include <stdexcept>
#include <format>

namespace Eng::tool {

Window::Window(const WindowConfig& config)
    : m_config(config) {
    m_createSuccess = CreateWindow();
}

Window::~Window() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

bool Window::IsValid() const {
    return m_createSuccess;
}

void Window::SetRelativeMode(bool enabled) {
    SDL_SetWindowRelativeMouseMode(m_window, enabled);
}

void Window::SetFullscreen(bool enabled) {
    SDL_Window* sdlWin = m_window;

    if (enabled) {
        SDL_GetWindowSize(sdlWin, &m_windowedWidth, &m_windowedHeight);
        SDL_SetWindowFullscreen(sdlWin, true);
    } else {
        SDL_SetWindowFullscreen(sdlWin, false);
        SDL_SetWindowBordered(sdlWin, true);
        SDL_SetWindowSize(sdlWin, m_windowedWidth, m_windowedHeight);
        SDL_SetWindowPosition(sdlWin, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    SDL_GetWindowSize(sdlWin, &m_config.windowWidth, &m_config.windowHeight);
    SDL_GetWindowSizeInPixels(sdlWin, &m_config.windowPixelWidth, &m_config.windowPixelHeight);
    // 删除了 m_renderapi->SetViewport(...)：BeginFrame 每帧自动按窗口尺寸刷新
}

bool Window::CreateWindow() {
    // ★ 1. API 探测（仅决定 apitype，不创建任何 context / RenderService）
    if (m_config.apitype == RenderAPItype::AUTO) {
        auto vk = service::VulkanRenderService::Probe();
        if (vk.available) {
            m_config.apitype = RenderAPItype::VULKAN;
        } else {
            auto gl = service::OpenGLRenderService::Probe();
            if (gl.available) {
                m_config.apitype = RenderAPItype::OPENGL;
                m_fallbackReason = "Vulkan 不可用，已自动降级到 OpenGL。\n原因: " + vk.reason;
            } else {
                throw std::runtime_error(std::format(
                    "硬件不支持任何可用的图形 API。\nVulkan: {}\nOpenGL: {}",
                    vk.reason, gl.reason));
            }
        }
    } else if (m_config.apitype == RenderAPItype::VULKAN) {
        auto vk = service::VulkanRenderService::Probe();
        if (!vk.available) {
            auto gl = service::OpenGLRenderService::Probe();
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
        auto gl = service::OpenGLRenderService::Probe();
        if (!gl.available) {
            throw std::runtime_error(std::format(
                "强制使用 OpenGL 失败。\n原因: {}", gl.reason));
        }
    }

    // ★ 2. 按 apitype 创建原生窗口
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
    // 属性留在创建 context 前设置；这些属性是 SDL 全局状态，直到下次修改
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, service::OpenGLRenderService::api_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, service::OpenGLRenderService::api_minor);
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
    // context / glad / renderapi 全部挪到 OpenGLRenderService::Initialize
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
    return true;
}

} // namespace Eng::tool