#include "Engine.h"
#include "core/Log.h"
#include <chrono>
#include <SDL3_image/SDL_image.h>

namespace Eng {

Engine& Engine::GetInstance() {
    static Engine eng;
    return eng;
}

bool Engine::Init(const EngineConfig& config, IGame* game) {
    m_engConfig = config;
    m_game.reset(game);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        logError(m_logger, "[Engine] SDL_Init failed: " << SDL_GetError());
        return false;
    }
    logInfo(m_logger, "[Engine] SDL initialized");

    if (config.enableNetwork) {
        if (!NET_Init()) {
            logError(m_logger, "[Engine] NET_Init failed: " << SDL_GetError());
            SDL_Quit();
            return false;
        }
        logInfo(m_logger, "[Engine] Networking initialized");
    }

    switch (config.mode) {
    case Mode::Client:
        return InitClient(config);
    case Mode::Server:
        return InitServer(config);
    case Mode::ClientAndServer:
      {
        bool success = true;
        if(!InitServer(config)) 
            success = false;
        if(!InitClient(config)) 
            success = false;
        return success;
      }
    }
    return true;
}

bool Engine::InitClient(const EngineConfig& config) {
    m_windowManager.reset(new client::WindowManager);

    IsClient = true;
    m_logger->log(LogLevel::INFO, "[Engine] Client Init");
    return true;
}

bool Engine::InitServer(const EngineConfig& config) {
    IsServer = true;
    m_logger->log(LogLevel::INFO, "[Engine] Server Init");
    return true;
}

void Engine::Run() {


    m_game->OnStart(*this);

    m_running = true;

    using namespace std::chrono;
    auto lastTime = steady_clock::now();
    constexpr float fixedDelta = 1.0f / 20.0f;
    float accumulator = 0.0f;

    m_logger->log(LogLevel::INFO, "[Engine] Run main loop");
    while (m_running) {
        auto currentTime = steady_clock::now();
        float deltaTime = duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        if (deltaTime > 0.1f) deltaTime = 0.1f;

        client::Input::Get().Update();

        if (m_windowManager) {
            m_windowManager->PollEvents();
            if (!m_windowManager->HasAnyWindow()) {
                m_running = false;
                break;
            }
        }

        accumulator += deltaTime;

        if (m_game->OnUpdate(*this, fixedDelta)) {
            break;
        }

        m_game->OnRender(*this);

        if (m_windowManager)
            m_windowManager->Update();

    }

    m_game->OnShutdown(*this);
}

void Engine::Stop() {
    m_running = false;
}

void Engine::Quit() {
    m_windowManager.reset();
    m_game.reset();
    NET_Quit();
    logInfo(m_logger, "[Engine] Networking shutdown");
    SDL_Quit();
    logInfo(m_logger, "[Engine] Quit");
}
    
} // namespace Eng
