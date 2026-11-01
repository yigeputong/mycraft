#pragma once

// =============================================================================
// 标准库与核心前置声明
// =============================================================================
#include <cstdint>
#include <memory>
#include <string>
// #include <span>
// #include <vector>

// =============================================================================
// 1. 基础工具层 (Tool)
// =============================================================================
#include "Engine/tool/Log.h"
#include "Engine/tool/Configs.h"
// #include "Engine/tool/Error.h"
// #include "Engine/tool/Input.h"
// #include "Engine/tool/Window.h"
// #include "Engine/tool/RAIIThread.h"
// #include "Engine/tool/Platform.h"

// 网络传输 
// #include "Engine/tool/MessageWriter.h"
// #include "Engine/tool/MessageReader.h"
// #include "Engine/tool/Transport.h"
// #include "Engine/tool/MemoryTransport.h"
// #include "Engine/tool/NetworkTransport.h"
// #include "Engine/tool/NetworkChannel.h"
// #include "Engine/tool/NetworkServer.h"

// =============================================================================
// 2. 全局服务层 (Service)
// =============================================================================
// #include "Engine/service/IRenderService.h"
#include "Engine/service/WindowService.h"
// 注意：VulkanRenderService.h / OpenGLRenderService.h 绝对不要放这里！
// 业务层只需要 IRenderService*，具体后端的创建应该由 Engine 内部完成。

// =============================================================================
// 3. 场景渲染 (Client)
// =============================================================================
// #include "Engine/client/SceneRenderer.h"

// =============================================================================
// 4. 引擎核心与业务接口
// =============================================================================
#include "Engine/IGame.h"
// ^ 如果你的 Engine.h 本身需要包含自己内部的 Engine 类，可以这么写。
// 但如果当前文件就是 Engine.h，则不需要这一行。


namespace Eng {

// 游戏模式：Engine 把 mode 透传给 MyGame
enum class Mode : std::uint8_t {
    SinglePlayer,      // 单人：集成服务端（未来：内存连接）
    MultiplayerHost,   // 多人主机：本地服务端 + 允许其他玩家连接
    MultiplayerJoin,   // 多人加入：只连远程服务端
    DedicatedServer,   // 专用服务端：无窗口
};

struct EngineConfig {
    Mode mode = Mode::SinglePlayer;
    std::string name;
    bool enableNetwork = false;
};

class Engine {
public:

    static Engine& GetInstance(); // 单例引擎

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

    // ==================== 模式查询（给 MyGame 用） ====================
    Mode GetMode() const { return m_engConfig.mode; }
    const EngineConfig& GetEngineConfig() const { return m_engConfig; }

    bool IsSinglePlayer()    const { return m_engConfig.mode == Mode::SinglePlayer; }
    bool IsHost()            const { return m_engConfig.mode == Mode::MultiplayerHost; }
    bool IsClientJoin()      const { return m_engConfig.mode == Mode::MultiplayerJoin; }
    bool IsDedicatedServer() const { return m_engConfig.mode == Mode::DedicatedServer; }

    // ★ 该模式要不要窗口 / 渲染器
    bool NeedsWindow()  const { return m_engConfig.mode != Mode::DedicatedServer; }
    // ★ 该模式要不要启动本地服务端
    bool NeedsLocalServer() const {
        return m_engConfig.mode == Mode::SinglePlayer ||
               m_engConfig.mode == Mode::MultiplayerHost ||
               m_engConfig.mode == Mode::DedicatedServer;
    }
    // ★ 该模式要不要连接服务端（连本地或远程）
    bool NeedsClientConnection() const {
        return m_engConfig.mode == Mode::SinglePlayer ||
               m_engConfig.mode == Mode::MultiplayerHost ||
               m_engConfig.mode == Mode::MultiplayerJoin;
    }

private:
    Engine() = default;
    std::unique_ptr<IGame> m_game;
    std::unique_ptr<service::WindowService> m_windowManager;
    std::unique_ptr<tool::Log> m_logger = std::make_unique<tool::Log>();

    bool m_running = false;
    float m_deltaTime = 0.0f;
    std::string m_lastError;
    EngineConfig m_engConfig;
    tool::AppConfig m_appConfig;
};

} // namespace Eng