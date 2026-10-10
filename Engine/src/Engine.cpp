#include "Engine.h"
#include "core/Log.h"
#include <chrono>
#include <SDL3/SDL_filesystem.h>
#include <SDL3_image/SDL_image.h>
#include "core/Platform.h"
#include <filesystem>

namespace Eng {

Engine& Engine::GetInstance() {
    static Engine eng;
    return eng;
}

bool Engine::Init(const EngineConfig& config, IGame* game) {
    m_engConfig = config;
    m_game.reset(game);

    EnableAnsiConsole();

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

    // ★ 只有需要窗口的模式才创建 WindowManager
    if (NeedsWindow()) {
        m_windowManager = std::make_unique<client::WindowManager>();
    }

    // 打一行 mode，方便排查
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

void Engine::Run() {
    m_game->OnStart(*this);

    m_running = true;

    auto lastTime = std::chrono::steady_clock::now();
    m_logger->log(LogLevel::INFO, "[Engine] Run main loop");

    while (m_running) {
        auto currentTime = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        if (deltaTime > 0.1f) deltaTime = 0.1f;

        m_deltaTime = deltaTime;

        client::Input::Get().Update();

        if (m_windowManager) {
            m_windowManager->PollEvents();
            if (!m_windowManager->HasAnyWindow()) {
                m_running = false;
                break;
            }
        }

        if (m_game->OnUpdate(*this, deltaTime)) {
            break;
        }

        // ★ DedicatedServer 不渲染
        if (NeedsWindow()) {
            m_game->OnRender(*this);
        }

        if (m_windowManager) {
            m_windowManager->Update();
        }
    }

    m_game->OnShutdown(*this);
}

void Engine::Stop() {
    m_running = false;
}

void Engine::Quit() {
    m_windowManager.reset();
    m_game.reset();

    // ★ 只有初始化过才 Quit
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