#include "Engine/Engine.h"
#include "Engine/tool/Log.h"
#include "Engine/tool/Window.h"
#include "Engine/service/IRenderService.h"
#include "Engine/service/AssetService.h"
#include "Engine/service/InputService.h"
#include "Engine/service/OpenGLRenderService.h"
#include "Engine/service/VulkanRenderService.h"

#include <chrono>
#include <filesystem>
#include <SDL3/SDL_filesystem.h>
#include <SDL3_net/SDL_net.h>
#include <SDL3_image/SDL_image.h>
#include "Engine/tool/Platform.h"

namespace Eng {

Engine& Engine::GetInstance() {
    static Engine eng;
    return eng;
}

Engine::~Engine() = default;

bool Engine::Init(const EngineConfig& config, IGame* game) {
    m_engConfig = config;
    m_game.reset(game);

    tool::EnableAnsiConsole();
    tool::SetConsoleUTF8Output();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        logError(m_logger, "[Engine] SDL_Init failed: " << SDL_GetError());
        return false;
    }
    logInfo(m_logger, "[Engine] SDL initialized");

    if (const char* base = SDL_GetBasePath()) {
        std::filesystem::current_path(base);
    }

    if (config.enableNetwork) {
        if (!NET_Init()) {
            logError(m_logger, "[Engine] NET_Init failed: " << SDL_GetError());
            SDL_Quit();
            return false;
        }
        logInfo(m_logger, "[Engine] Networking initialized");
    }

    // 基础能力：Input 由 Engine 显式初始化（不再懒单例）
    m_inputService = std::make_unique<service::InputService>();
    logInfo(m_logger, "[Engine] InputService initialized");

    // WindowService 需要 InputService 完成事件转发（依赖显式注入）
    if (NeedsWindow()) {
        m_windowManager = std::make_unique<service::WindowService>(m_inputService.get());
    }

    const char* modeStr = "?";
    switch (config.mode) {
        case Mode::SinglePlayer:    modeStr = "SinglePlayer";    break;
        case Mode::MultiplayerHost: modeStr = "MultiplayerHost"; break;
        case Mode::MultiplayerJoin: modeStr = "MultiplayerJoin"; break;
        case Mode::DedicatedServer: modeStr = "DedicatedServer"; break;
    }
    logInfo(m_logger, "[Engine] Mode: " << modeStr);

    return true;
}

service::IRenderService* Engine::CreateRenderService(tool::Window* mainWindow) {
    if (m_renderService) return m_renderService.get();

    if (!mainWindow) {
        m_lastError = "CreateRenderService: mainWindow is null";
        logError(m_logger, "[Engine] " << m_lastError);
        return nullptr;
    }

    const auto apitype = mainWindow->GetConfigs().apitype;

    std::unique_ptr<service::IRenderService> service;
    try {
        switch (apitype) {
            case tool::RenderAPItype::OPENGL:
                service = std::make_unique<service::OpenGLRenderService>();
                break;
            case tool::RenderAPItype::VULKAN:
                service = std::make_unique<service::VulkanRenderService>();
                break;
            default:
                m_lastError = "CreateRenderService: unsupported apitype";
                logError(m_logger, "[Engine] " << m_lastError);
                return nullptr;
        }
        service->Initialize(
            mainWindow->GetConfigs().windowWidth,
            mainWindow->GetConfigs().windowHeight,
            mainWindow);
    } catch (const std::exception& e) {
        m_lastError = std::string("RenderService init failed: ") + e.what();
        logFatal(m_logger, "[Engine] " << m_lastError);
        return nullptr;
    }

    m_renderService = std::move(service);
    logInfo(m_logger, "[Engine] RenderService created");
    return m_renderService.get();
}

service::AssetService* Engine::CreateAssetService(service::IRenderService* renderService) {
    if (m_assetService) return m_assetService.get();

    if (!renderService) {
        m_lastError = "CreateAssetService: renderService is null";
        logError(m_logger, "[Engine] " << m_lastError);
        return nullptr;
    }

    m_assetService = std::make_unique<service::AssetService>(renderService);
    logInfo(m_logger, "[Engine] AssetService created");
    return m_assetService.get();
}

void Engine::Run() {
    m_game->OnStart(*this);

    m_running = true;

    auto lastTime = std::chrono::steady_clock::now();
    logInfo(m_logger, "[Engine] Run main loop");

    while (m_running) {
        auto currentTime = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        if (deltaTime > 0.1f) deltaTime = 0.1f;

        m_deltaTime = deltaTime;

        if (m_inputService) {
            m_inputService->Update();
        }

        if (m_windowManager) {
            m_windowManager->PollEvents();
            if (NeedsWindow() && !m_windowManager->HasAnyWindow()) {
                m_running = false;
                break;
            }
        }

        if (m_game->OnUpdate(*this, deltaTime)) {
            break;
        }

        if (NeedsWindow()) {
            m_game->OnRender(*this);
        }

        if (m_windowManager) {
            m_windowManager->Update();
        }
    }

    if (m_game) {
        m_game->OnShutdown(*this);
    }
}

void Engine::Stop() {
    m_running = false;
}

void Engine::Quit() {
    // 严格销毁顺序（与声明逆序一致）
    // game → ImGui backend → asset → render → window → input
    m_game.reset();

    if (m_renderService) {
        m_renderService->ShutdownImGuiBackend();
    }
    if (m_assetService) {
        m_assetService.reset();
        logInfo(m_logger, "[Engine] AssetService shutdown");
    }
    if (m_renderService) {
        m_renderService.reset();
        logInfo(m_logger, "[Engine] RenderService shutdown");
    }
    if (m_windowManager) {
        m_windowManager.reset();
        logInfo(m_logger, "[Engine] WindowService shutdown");
    }
    if (m_inputService) {
        m_inputService.reset();
        logInfo(m_logger, "[Engine] InputService shutdown");
    }

    if (m_engConfig.enableNetwork) {
        NET_Quit();
        logInfo(m_logger, "[Engine] Networking shutdown");
    }
    SDL_Quit();
    logInfo(m_logger, "[Engine] Quit");
}

float Engine::GetDeltaTime() const {
    return m_deltaTime;
}

} // namespace Eng