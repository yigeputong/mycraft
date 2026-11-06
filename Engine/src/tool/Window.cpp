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

    // 强制同步一次尺寸（重要：让 Vulkan Swapchain 捕获到正确的新尺寸）
    SDL_GetWindowSize(sdlWin, &m_config.windowWidth, &m_config.windowHeight);
    SDL_GetWindowSizeInPixels(sdlWin, &m_config.windowPixelWidth, &m_config.windowPixelHeight);
}

bool Window::CreateWindow() {
    // 统一的能力打印格式化函数
    auto printCaps = [&](const char* backend, const Eng::service::rhi::RenderCapabilities& c) {
        logInfo(m_logger, "==================================================");
        logInfo(m_logger, "  [" << backend << "] 硬件能力探测完毕");
        logInfo(m_logger, "==================================================");
        logInfo(m_logger, "  API 版本       : " << c.apiName << " " << c.apiMajor << "." << c.apiMinor);
        
        logInfo(m_logger, "  [硬件极限]");
        logInfo(m_logger, "    Max Texture Size : " << c.maxTextureSize);
        logInfo(m_logger, "    Max Compute WG   : " << c.maxComputeWorkGroupInvocations);
        logInfo(m_logger, "    Max Vertex Attrs : " << c.maxVertexAttributes);
        logInfo(m_logger, "    Max Vert Bindings: " << c.maxVertexBufferBindings);

        logInfo(m_logger, "  [管线特性]");
        logInfo(m_logger, "    Geometry Shader  : " << (c.supportsGeometryShader ? "YES" : "NO"));
        logInfo(m_logger, "    Tessellation     : " << (c.supportsTessellation ? "YES" : "NO"));
        logInfo(m_logger, "    Dynamic Rendering: " << (c.supportsDynamicRendering ? "YES" : "NO"));
        logInfo(m_logger, "    Compute Shader   : " << (c.supportsCompute ? "YES" : "NO"));
        logInfo(m_logger, "    Multi-Draw Ind.  : " << (c.supportsMultiDrawIndirect ? "YES" : "NO"));

        logInfo(m_logger, "  [高级渲染]");
        logInfo(m_logger, "    Mesh Shader      : " << (c.supportsMeshShaders ? "YES" : "NO"));
        logInfo(m_logger, "    Ray Tracing      : " << (c.supportsRayTracing ? "YES" : "NO"));

        logInfo(m_logger, "  [内存与绑定]");
        logInfo(m_logger, "    Bindless Texture : " << (c.supportsBindless ? "YES" : "NO"));
        logInfo(m_logger, "    Buffer Address   : " << (c.supportsBufferDeviceAddress ? "YES" : "NO"));
        logInfo(m_logger, "    Timeline Sem     : " << (c.supportsTimelineSemaphore ? "YES" : "NO"));
        logInfo(m_logger, "    Sparse Binding   : " << (c.supportsSparseBinding ? "YES" : "NO"));
        logInfo(m_logger, "==================================================");
    };

    // 1. API 探测（仅决定 apitype，不创建任何 context / RenderService）
    if (m_config.apitype == RenderAPItype::AUTO) {
        auto vk = service::VulkanRenderService::Probe();
        if (vk.available) {
            m_config.apitype = RenderAPItype::VULKAN;
            m_renderCaps = vk.caps;
            printCaps("Vulkan (Auto)", vk.caps);
        } else {
            logWarning(m_logger, "[Probe] Vulkan 探测失败: " << vk.reason);
            auto gl = service::OpenGLRenderService::Probe();
            if (gl.available) {
                m_config.apitype = RenderAPItype::OPENGL;
                m_renderCaps = gl.caps;
                m_fallbackReason = "Vulkan 不可用，已自动降级到 OpenGL。\n原因: " + vk.reason;
                printCaps("OpenGL (Auto Fallback)", gl.caps);
            } else {
                logError(m_logger, "[Probe] OpenGL 探测失败: " << gl.reason);
                throw std::runtime_error(std::format(
                    "硬件不支持任何可用的图形 API。\nVulkan: {}\nOpenGL: {}",
                    vk.reason, gl.reason));
            }
        }
    } else if (m_config.apitype == RenderAPItype::VULKAN) {
        auto vk = service::VulkanRenderService::Probe();
        if (!vk.available) {
            logWarning(m_logger, "[Probe] 强制指定的 Vulkan 不可用: " << vk.reason);
            auto gl = service::OpenGLRenderService::Probe();
            if (gl.available) {
                m_config.apitype = RenderAPItype::OPENGL;
                m_renderCaps = gl.caps;
                m_fallbackReason = "Vulkan 不可用，已自动降级到 OpenGL。\n原因: " + vk.reason;
                printCaps("OpenGL (Vulkan Forced Fallback)", gl.caps);
            } else {
                logError(m_logger, "[Probe] 强制 Vulkan 失败，且回退 OpenGL 也失败: " << gl.reason);
                throw std::runtime_error(std::format(
                    "强制使用 Vulkan 失败，且 OpenGL 不可用。\nVulkan: {}\nOpenGL: {}",
                    vk.reason, gl.reason));
            }
        } else {
            m_renderCaps = vk.caps;
            printCaps("Vulkan (Forced)", vk.caps);
        }
    } 
    else if (m_config.apitype == RenderAPItype::OPENGL) {
        auto gl = service::OpenGLRenderService::Probe();
        if (!gl.available) {
            logError(m_logger, "[Probe] 强制指定的 OpenGL 不可用: " << gl.reason);
            throw std::runtime_error(std::format(
                "强制使用 OpenGL 失败。\n原因: {}", gl.reason));
        }
        m_renderCaps = gl.caps;
        printCaps("OpenGL (Forced)", gl.caps);
    }

    // 2. 按 apitype 创建原生窗口
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
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (m_config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    m_window = SDL_CreateWindow(
        m_config.title.c_str(),
        m_config.windowWidth,
        m_config.windowHeight,
        flags
    );
    if (!m_window) {
        SDL_Log("SDL_CreateWindow Error: %s", SDL_GetError());
        return false;
    }
    // context / glad / renderapi 全部挪到 OpenGLRenderService::Initialize
    return true;
}

bool Window::CreateVulkanWindow() {
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
    if (m_config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    m_window = SDL_CreateWindow(
        m_config.title.c_str(),
        m_config.windowWidth,
        m_config.windowHeight,
        flags
    );
    if (!m_window) {
        SDL_Log("SDL_CreateWindow Error: %s", SDL_GetError());
        return false;
    }
    return true;
}

} // namespace Eng::tool