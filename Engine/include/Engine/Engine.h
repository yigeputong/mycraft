#pragma once

// =============================================================================
// 标准库与核心前置声明
// =============================================================================
#include <cstdint>
#include <memory>
#include <string>

// =============================================================================
// 1. 基础工具层 (Tool)
// =============================================================================
#include "Engine/tool/Log.h"
#include "Engine/tool/Configs.h"

// =============================================================================
// 2. 全局服务层 (Service)
// =============================================================================
#include "Engine/service/WindowService.h"
// IRenderService / AssetService 不在这里 include（成员用前向声明 + unique_ptr）
// Engine::~Engine() 显式定义在 Engine.cpp 中。

// =============================================================================
// 3. 引擎核心与业务接口
// =============================================================================
#include "Engine/IGame.h"

// 前向声明
namespace Eng::tool {
    class Window;
}
namespace Eng::service {
    class IRenderService;
    class AssetService;
}

namespace Eng {

enum class Mode : std::uint8_t {
    SinglePlayer,
    MultiplayerHost,
    MultiplayerJoin,
    DedicatedServer,
};

struct EngineConfig {
    Mode mode = Mode::SinglePlayer;
    std::string name;
    bool enableNetwork = false;
};

class Engine {
public:
    static Engine& GetInstance();

    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool Init(const EngineConfig& config, IGame* game);
    void Run();
    void Stop();
    void Quit();

    // ==================== 基础查询 ====================
    std::string GetLastError() { return m_lastError; }
    bool IsRunning() const { return m_running; }
    float GetDeltaTime() const;
    service::WindowService* GetWindowManager() { return m_windowManager.get(); }
    tool::AppConfig& GetConfig() { return m_appConfig; }
    tool::Log* GetLogger() { return m_logger.get(); }

    // ==================== Service 访问 ====================
    service::IRenderService* GetRenderService() { return m_renderService.get(); }
    service::AssetService*  GetAssetService()  { return m_assetService.get(); }

    /**
     * @brief 从当前主窗口决定后端并创建 RenderService + AssetService（幂等）。
     * @details 由 MyGame 在窗口创建后调用，生命周期归 Engine 所有。
     * @return true 表示已就绪（含已存在时直接返回 true）。
     */
    bool CreateRenderService();
    // ==================== 模式查询 ====================
    Mode GetMode() const { return m_engConfig.mode; }
    const EngineConfig& GetEngineConfig() const { return m_engConfig; }

    bool IsSinglePlayer()    const { return m_engConfig.mode == Mode::SinglePlayer; }
    bool IsHost()            const { return m_engConfig.mode == Mode::MultiplayerHost; }
    bool IsClientJoin()      const { return m_engConfig.mode == Mode::MultiplayerJoin; }
    bool IsDedicatedServer() const { return m_engConfig.mode == Mode::DedicatedServer; }

    bool NeedsWindow()  const { return m_engConfig.mode != Mode::DedicatedServer; }
    bool NeedsLocalServer() const {
        return m_engConfig.mode == Mode::SinglePlayer ||
               m_engConfig.mode == Mode::MultiplayerHost ||
               m_engConfig.mode == Mode::DedicatedServer;
    }
    bool NeedsClientConnection() const {
        return m_engConfig.mode == Mode::SinglePlayer ||
               m_engConfig.mode == Mode::MultiplayerHost ||
               m_engConfig.mode == Mode::MultiplayerJoin;
    }

private:
    Engine() = default;

    // ⚠️ 成员声明顺序 = 析构逆序；game 依赖其余 Service，最先析构（最后声明）。
    std::unique_ptr<service::WindowService>  m_windowManager;   // 最后析构
    std::unique_ptr<service::IRenderService> m_renderService;   // 依赖 window
    std::unique_ptr<service::AssetService>   m_assetService;    // 依赖 render
    std::unique_ptr<tool::Log>               m_logger = std::make_unique<tool::Log>();
    std::unique_ptr<IGame>                   m_game;            // 最先析构

    bool m_running = false;
    float m_deltaTime = 0.0f;
    std::string m_lastError;
    EngineConfig m_engConfig;
    tool::AppConfig m_appConfig;
};

} // namespace Eng