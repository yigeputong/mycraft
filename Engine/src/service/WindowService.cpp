#include "Engine/service/WindowService.h"
#include "Engine/tool/Window.h"
#include "Engine/service/InputService.h"
#include "imgui_impl_sdl3.h"

#include <algorithm>
#include <cstdio>

namespace Eng::service {

WindowService::WindowService(service::InputService* inputService)
    : m_inputService(inputService) {}

WindowService::~WindowService() {
    try {
        m_windowIds.clear();
        auto windows = std::move(m_windows);
        windows.clear();
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
        return 0;
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
        it->second->onEvent          = nullptr;
        it->second->onResize         = nullptr;
        it->second->onCloseRequest   = nullptr;
        it->second->onFocusChanged   = nullptr;
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
    // 铁律 3：Input 事件转发是本服务的职责；若未注入 InputService，则跳过（而不是回落到单例）
    if (m_inputService) {
        m_inputService->ProcessEvent(event);
    }

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
                        if (window->onFocusChanged) window->onFocusChanged(true);
                        break;

                    case SDL_EVENT_WINDOW_FOCUS_LOST:
                        if (window->onFocusChanged) window->onFocusChanged(false);
                        break;

                    case SDL_EVENT_WINDOW_RESIZED:
                    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                        int newWidth  = event.window.data1;
                        int newHeight = event.window.data2;
                        window->GetConfigs().windowPixelWidth  = newWidth;
                        window->GetConfigs().windowPixelHeight = newHeight;
                        break;
                    }

                    default:
                        break;
                }
                break;
            }
        }
    } else {
        auto* main = GetMainWindow();
        if (main && main->onEvent) {
            main->onEvent(event);
        }
    }
}

void WindowService::Update() {
    // 目前无需 swap buffers（由 RenderService 负责）
}

} // namespace Eng::service