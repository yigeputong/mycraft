#include "Engine/service/WindowService.h"
#include "Engine/tool/Window.h"
#include "Engine/tool/Input.h"
#include "imgui_impl_sdl3.h"

namespace Eng::service {

WindowService::WindowService() = default;

WindowService::~WindowService() {
    try {
        m_windowIds.clear();
        auto windows = std::move(m_windows);
        windows.clear();  // 此时 m_windows 已为空
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "[WindowService] Shutdown threw: %s\n", e.what());
    }
    catch (...) {
        std::fputs("[WindowService] Shutdown threw unknown\n", stderr);
    }
}

uint32_t WindowService::CreateWindow(const tool::WindowConfig& config) {
    auto window = std::make_unique<tool::Window>(config);
    if (!window->IsValid()) {
        return 0;  // 创建失败
    }

    uint32_t id = m_nextId++;
    m_windows[id] = std::move(window);
    m_windowIds.push_back(id);

    if (m_mainWindowId == 0) {
        m_mainWindowId = id;
    }

    return id;
}

void WindowService::DestroyWindow(uint32_t id) {
    auto it = m_windows.find(id);
    if (it == m_windows.end()) return;

    if (m_mainWindowId == id) {
        m_mainWindowId = m_windowIds.empty() ? 0 : m_windowIds[0];
    }

    if (it->second) {
        it->second->onEvent = nullptr;
        it->second->onResize = nullptr;
        it->second->onCloseRequest = nullptr;
    }

    m_windowIds.erase(std::remove(m_windowIds.begin(), m_windowIds.end(), id), m_windowIds.end());
    m_windows.erase(it);
}

tool::Window* WindowService::GetWindow(uint32_t id) {
    auto it = m_windows.find(id);
    return (it != m_windows.end()) ? it->second.get() : nullptr;
}

tool::Window* WindowService::GetMainWindow() {
    return GetWindow(m_mainWindowId);
}

void WindowService::PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        DispatchEvent(event);
    }
}

bool WindowService::HasAnyWindow() const {
    for (const auto& [id, window] : m_windows) {
        if (window && window->GetRunning()) {
            return true;
        }
    }
    return false;
}

void WindowService::DispatchEvent(const SDL_Event& event) {
    tool::Input::Get().ProcessEvent(event);
    if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
        SDL_Window* sdlWin = SDL_GetWindowFromID(event.window.windowID);
        if (!sdlWin) return;


        for (auto& [id, window] : m_windows) {
            if (window->GetSDLWindow() == sdlWin) {
                if (window->onEvent) {
                    window->onEvent(event);
                }

                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        for (auto& [id2, window2] : m_windows) {
                            window2->SetRunning(false);
                        }
                        return;
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                        if (window->onCloseRequest) {
                            window->onCloseRequest();
                        }
                        break;

                    case SDL_EVENT_WINDOW_FOCUS_GAINED:
                        if (window->onFocusChanged) {
                            window->onFocusChanged(true);
                        }
                        break;

                    case SDL_EVENT_WINDOW_FOCUS_LOST:
                        if (window->onFocusChanged) {
                            window->onFocusChanged(false);
                        }
                        break;
                    default:
                        break;
                }
                break;
            }
        }
    } else {
        //非窗口事件分发给主窗口
        auto* main = GetMainWindow();
        if (main && main->onEvent) {
            main->onEvent(event);
        }

        // 广播给所有窗口（可选）
        // for (auto& [id, window] : m_windows) {
        //     if (window->onEvent) {
        //         window->onEvent(event);
        //     }
        // }
    }
}

void WindowService::Update() {
    for (auto& [id, window] : m_windows) {
        if (window->GetRunning()) {
            //window->SwapBuffers();
        }
    }
}
    
} // namespace Eng::client
