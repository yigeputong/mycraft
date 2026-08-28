#include "Engine.h"
#include <chrono>
#include <iostream>
#include <SDL3_image/SDL_image.h>

#include "client/Render/OpenGLAPI.h"

namespace Eng {

Engine& Engine::GetInstance() {
    static Engine eng;
    return eng;
}

bool Engine::Init(const EngineConfig& config) {
    m_engConfig = config;
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

    m_windowManager = client::WindowManager::GetInstance();

    switch(config.windowConfig.apitype) {
        case client::RenderAPItype::OPENGL:
            m_renderapi = std::make_unique<client::OpenGLAPI>();
    }

    IsClient = true;
    return true;
}

bool Engine::InitServer(const EngineConfig& config) {
    IsServer = true;
    return true;
}

void Engine::Run(IGame* game) {
    if (!game) return;

    game->OnStart(*this);

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

        m_windowManager->PollEvents();
        if (!m_windowManager->HasAnyWindow()) {
            m_running = false;
            break;
        }

        accumulator += deltaTime;

        // 固定步长更新
        while (accumulator >= fixedDelta) {
            game->OnUpdate(*this, fixedDelta);   // 逻辑更新，固定时间
            accumulator -= fixedDelta;
        }

        game->OnUpdate(*this, deltaTime);

        game->OnRender(*this);

        m_windowManager->Update();

    }

    game->OnShutdown(*this);
}

void Engine::Stop() {
    m_running = false;
}

void Engine::Quit() {
    
}
    
} // namespace Eng
