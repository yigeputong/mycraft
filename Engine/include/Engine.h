#pragma once

//standards
#include <memory>
#include <string>

//core
#include "IGame.h"
#include "core/Log.h"
#include "core/Configs.h"
#include "core/NetworkChannel.h"
#include "core/NetworkServer.h"
#include "core/MessageWriter.h"
#include "core/MessageReader.h"

//client
#include "client/Input.h"
#include "client/Window.h"
#include "client/WindowManager.h"
#include "client/Render/RenderAPI.h"

//server


namespace Eng {

// ★ 游戏模式：Engine 不判断"你是干什么的"，只把 mode 透传给 MyGame
enum class Mode {
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
    client::WindowManager* GetWindowManager() { return m_windowManager.get(); }
    AppConfig& GetConfig() { return m_appConfig; }
    Log* GetLogger() { return m_logger.get(); }

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
    std::unique_ptr<client::WindowManager> m_windowManager;
    std::unique_ptr<Log> m_logger = std::make_unique<Log>();

    bool m_running = false;
    float m_deltaTime = 0.0f;
    uint64_t m_lastFrameTime = 0;
    std::string m_lastError;
    EngineConfig m_engConfig;
    AppConfig m_appConfig;
};

} // namespace Eng