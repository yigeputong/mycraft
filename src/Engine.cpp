#include "Engine.h"
#include <chrono>
#include <iostream>
#include <filesystem>
#include <SDL3_image/SDL_image.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include "client/Render/OpenGLAPI.h"

namespace Eng {

Engine& Engine::GetInstance() {
    static Engine eng;
    return eng;
}

bool Engine::Init(const EngineConfig& config, IGame* game) {
    m_engConfig = config;
    m_game.reset(game);
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
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        m_lastError = "SDL_Init failed: " + std::string(SDL_GetError());
        return false;
    }

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

    m_logger->log(LogLevel::INFO, "[Engine] Run main loop");

    m_game->OnStart(*this);

    m_running = true;

    using namespace std::chrono;
    auto lastTime = steady_clock::now();
    constexpr float fixedDelta = 1.0f / 20.0f;
    float accumulator = 0.0f;

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
    SDL_Quit();
    m_logger->log(LogLevel::INFO, "[Engine] Quit");
}
    
} // namespace Eng
