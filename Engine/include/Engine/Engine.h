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
// IRenderService / AssetService / InputService 不在这里 include
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
    class InputService;
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
    service::InputService*     GetInputService()   { return m_inputService.get(); }

    // ==================== Service 创建（显式依赖注入） ====================
    // 铁律 3：依赖关系必须显式写在参数里，Engine 内部不再隐式查找。
    //
    // 所有 CreateXXX 均幂等：
    //   - 已存在时直接返回现有实例
    //   - 参数为 nullptr 时返回 nullptr 并写入 GetLastError()
    //   - 生命周期归 Engine 所有

    /// @brief 创建 RenderService。@param mainWindow 必须非空。
    service::IRenderService* CreateRenderService(tool::Window* mainWindow);

    /// @brief 创建 AssetService。@param renderService 必须非空。
    service::AssetService*   CreateAssetService(service::IRenderService* renderService);

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

    // 成员声明顺序 = 析构逆序；game 依赖全部 Service，最先析构（最后声明）。
    // InputService 是 WindowService 的依赖，必须比它晚析构 → 声明更早。
    std::unique_ptr<service::InputService>   m_inputService;    // 最后析构（依赖链最底层）
    std::unique_ptr<service::WindowService>  m_windowManager;   // 依赖 input
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