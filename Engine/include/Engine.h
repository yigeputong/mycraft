#pragma once

//standards
#include <memory>
#include <vector>
#include <string>

//core
#include "IGame.h"
#include "core/Log.h"

//client
#include "client/Window.h"
#include "client/WindowManager.h"
#include "client/Render/RenderAPI.h"
#include "client/Input.h"

//net

//server


namespace Eng {

enum class Mode {
    Client,
    Server,
    ClientAndServer
};

struct EngineConfig {
    Mode mode;
    std::string name;
};

class Engine {
public:

    static Engine& GetInstance(); // 单例引擎

    bool Init(const EngineConfig& config, IGame* game);

    void Run();

    void Stop();

    void Quit();

    std::string GetLastError() { return m_lastError; }
    bool IsRunning() const { return m_running; }
    float GetDeltaTime() const;
    client::WindowManager* GetWindowManager() { return m_windowManager.get(); }
    EngineConfig GetConfig() { return m_engConfig; }

private:
    Engine() = default;
    std::unique_ptr<IGame> m_game;
    std::unique_ptr<client::WindowManager> m_windowManager;
    std::unique_ptr<Log> m_logger = std::make_unique<Log>("Engine.log");

    bool m_running = false;
    float m_deltaTime = 0.0f;
    uint64_t m_lastFrameTime = 0;
    std::string m_lastError;
    EngineConfig m_engConfig;

    bool IsClient = false;
    bool IsServer = false;

    bool InitClient(const EngineConfig& config);
    bool InitServer(const EngineConfig& config);
    
};

}