#pragma once

#include <array>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Engine/Engine.h"
#include "Engine/client/SceneRenderer.h"
#include "Engine/ecs/SystemScheduler.h"
#include "Engine/service/AssetService.h"
#include "Engine/tool/Transport.h"

#include "game/GameServer.h"
#include "game/Protocol.h"
#include "game/client/ClientEntity.h"
#include "game/client/ClientUI.h"
#include "game/client/LODManager.h"
#include "game/client/MeshManager.h"
#include "game/client/ModelLoader.h"
#include "game/client/RenderSystem.h"
#include "game/core/GameContext.h"
#include "game/core/Physics.h"
#include "game/core/RayCast.h"
#include "game/core/ecs/ItemSystem.h"
#include "game/core/ecs/PhysicsSystem.h"
#include "game/core/ecs/PigAISystem.h"
#include "game/server/WorldSave.h"

namespace game {

// 全局渲染状态（传递给 Shader）
struct GlobalState {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::vec3 viewPos = glm::vec3(0.0f);
    float aoStrength = 1.0f;
    glm::vec3 lightDir = glm::vec3(0.5f, -1.0f, 0.3f);
    float lightIntensity = 1.0f;
    glm::vec3 lightColor = glm::vec3(1.0f);
    float timeOfDay = 8.0f;
    glm::vec3 lightAmbient = glm::vec3(0.3f);
    float _pad2 = 0.0f;
};

class MyGame : public Eng::IGame {
public:
    void OnStart(Eng::Engine& engine) override;
    bool OnUpdate(Eng::Engine& engine, float deltaTime) override;
    void OnRender(Eng::Engine& engine) override;
    void OnShutdown(Eng::Engine& engine) override;

private:
    // ================================================================
    // 1. 引擎注入的底层服务 (MyGame 仅借用，不负责销毁)
    // ================================================================
    Eng::service::WindowService* winMgr = nullptr;
    Eng::tool::Window* mainWin = nullptr;
    Eng::service::IRenderService* renderer = nullptr;
    Eng::service::AssetService* m_assetManager = nullptr;
    Eng::service::InputService* m_inputService = nullptr;
    uint32_t m_win = 0;
    Eng::service::DeviceInfo m_deviceInfo;
    bool m_useVulkan = false;

    // ================================================================
    // 2. 游戏状态与全局配置
    // ================================================================
    enum class GameState : std::uint8_t {
        MainMenu,
        WorldSelect,
        Connecting,
        Playing,
    };
    GameState m_state = GameState::MainMenu;

    bool m_shouldQuit = false;
    bool m_menuOpen = false;
    bool m_minimized = false;
    bool m_applyPending = false;
    bool m_returnToMenuPending = false;
    bool m_apiChanged = false;
    int m_settingsTab = 0;
    std::string m_statusText;

    // ================================================================
    // 3. 全局静态资源 (生命周期与 Engine 同寿)
    // ================================================================
    game::BlockRegistry m_blockRegistry;
    game::GameContext m_ctx{m_blockRegistry};

    int m_renderDistance = 2;
    int m_lodRenderDistance = m_renderDistance * 4;
    bool m_enableLod = false;

    // 核心渲染资产
    Eng::service::ShaderPtr m_cubeShader;
    Eng::service::ShaderPtr m_skyShader;
    Eng::service::ShaderPtr m_lodShader;
    Eng::service::ShaderPtr m_entityShader;
    Eng::service::ShaderPtr m_handShader;
    Eng::service::TexturePtr m_playerTexture;
    Eng::service::TexturePtr m_atlasTexture;
    Eng::service::FramebufferPtr m_mainFbo;
    Eng::service::BufferPtr m_globalStateUBO;

    // ================================================================
    // 4. 会话资源 (每次进游戏创建，退游戏销毁)
    // ================================================================
    // --- 4.1 网络相关 ---
    std::unique_ptr<Eng::tool::IClientTransport> m_client;
    std::unique_ptr<game::server::GameServer> m_server = nullptr;
    uint32_t m_myClientId = 0;
    uint32_t m_worldSeed = 0;
    std::unordered_map<uint32_t, net::PlayerState> m_otherPlayers;

    // --- 4.2 世界、后台线程与 ECS 容器 ---
    game::client::ClientWorld m_world;  // 目前仅用于网络请求驱动与 LOD 采样
    client::MeshManager m_meshManager;
    client::LodManager m_lodManager;
    std::unordered_map<uint64_t, uint32_t> m_chunkMeshVersions;  // 异步建 Mesh 的版本号

    game::client::ClientEntityManager m_entityManager;
    // ECS 系统按注册顺序执行（见 MyGame::OnStart）。系统对象归调度器所有，
    // 三个系统都没有状态，所以整个进程注册一次即可。
    game::client::ClientSystemScheduler m_systems;
    // 渲染系统不参与 tick 调度，由 OnRender 直接调用。
    game::client::RenderSystem m_renderSystem;

    // --- 4.3 渲染大脑 ---
    std::unique_ptr<Eng::client::SceneRenderer> m_sceneRenderer;

    // --- 4.4 存档与界面交互状态 ---
    std::vector<game::server::WorldMetadata> m_savedWorlds;
    std::string m_selectedWorldId;
    char m_searchBuffer[128] = "";

    bool m_showNewWorldDialog = false;
    char m_newWorldNameBuffer[128] = "";
    char m_newWorldSeedBuffer[64] = "";
    bool m_showDeleteConfirmDialog = false;
    std::string m_worldToDelete;

    bool m_showMultiplayerDialog = false;
    char m_ipBuffer[128] = "localhost";
    int m_portInput = 25565;

    // ================================================================
    // 5. 本地玩家状态与物理输入 (静态数据已剥离至 ECS)
    // ================================================================
    game::core::ecs::EntityId m_localPlayerEntity = game::core::ecs::INVALID_ENTITY;

    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

    glm::vec3 m_lastMoveDir{0.0f};
    game::RayHit m_currentRayHit;

    bool m_relativeMode = true;
    bool m_showDebug = true;
    float m_placeCooldown = 0.0f;
    float m_digCooldown = 0.0f;
    static constexpr float kPlaceInterval = 0.2f;
    static constexpr float kDigInterval = 0.2f;

    glm::vec3 ComputeCameraFront();
    glm::vec3 GetPlayerEyePos();

    // ================================================================
    // 6. 输入模式与触控 UI 状态
    // ================================================================
    enum class InputMode : std::uint8_t { Auto = 0, KeyboardMouse = 1, Touch = 2 };
    InputMode m_inputMode = InputMode::Auto;
    InputMode m_activeInputMode = InputMode::KeyboardMouse;

    float m_touchSensitivity = 0.15f;
    float m_touchDeadzone = 5.0f;

    int m_leftTouchId = -1;
    glm::vec2 m_joystickBasePos = {0.18f, 0.70f};
    glm::vec2 m_joystickOriginPx = {0.0f, 0.0f};
    glm::vec2 m_joystickCurrentPx = {0.0f, 0.0f};

    struct VirtualButtonConfig {
        std::string actionName;
        glm::vec2 pos = {0.5f, 0.5f};
        float radius = 0.05f;
        std::string label;
    };
    std::vector<VirtualButtonConfig> m_virtualButtons;
    bool m_isEditingLayout = false;

    // 时间与动画
    float m_timeOfDay = 8.0f;
    static constexpr float kDayLengthSec = 300.0f;
    float m_animTime = 0.0f;
    float m_swingProgress = 0.0f;
    float m_walkDist = 0.0f;

    // ================================================================
    // 7. 物品与背包 (客户端预测缓存)
    // ================================================================
    std::unordered_map<BlockId, Eng::client::RenderMesh> m_itemMeshCache;
    std::unordered_map<uint32_t, game::core::ecs::EntityId> m_netToEcsMap;    // 网络实体 ID 映射
    std::unordered_map<uint64_t, game::core::ecs::EntityId> m_chunkToEcsMap;  // 区块坐标映射

    Eng::client::RenderMesh m_playerMesh;
    Eng::client::RenderMesh m_handMesh;
    Eng::client::RenderMesh m_skyboxMesh;

    static constexpr int kHotbarSize = 9;
    uint8_t m_hotbarIndex = 0;
    std::array<ItemStack, 36> m_inventory{};
    int32_t m_inventoryStateId = 0;
    ItemStack m_heldItemStack{};
    bool m_inventoryOpen = false;

    // ================================================================
    // 8. UI 与调试统计面板
    // ================================================================
    GlobalState m_globalState;
    float m_uiScale = 1.0f;

    float m_lastFrameMs = 0.0f;
    float m_fpsTimer = 0.0f;
    int m_fpsFrameCount = 0;
    float m_fpsAccumMs = 0.0f;
    float m_fpsPeakMs = 0.0f;
    float m_fpsDisplay = 0.0f;
    float m_frameMsDisplay = 0.0f;
    float m_frameMsAvgDisp = 0.0f;
    float m_frameMsPeakDisp = 0.0f;

    float m_serverTickMs = 0.0f;
    float m_serverTickAvg = 0.0f;
    float m_serverTickMax = 0.0f;

    std::unique_ptr<Eng::tool::Log> logger = std::make_unique<Eng::tool::Log>();
    client::ClientUI m_ui;

    std::unique_ptr<game::client::LoadedModel> m_pigModel;

    // ================================================================
    // 9. 辅助函数声明
    // ================================================================
    void EnterGame();
    void ExitGame();
    void ReturnToMainMenu();

    bool SetupWindow(Eng::Engine& engine);
    bool SetupRenderer(Eng::Engine& engine);
    void SetupShaders();
    void RecreateFBOs(int w, int h);
    Eng::service::ShaderPtr CreateShaderByName(const std::string& name, bool isSky);

    bool StartLocalServer(bool useMemory, bool createClient, const std::string& worldId);
    bool ConnectToServer(const std::string& host, uint16_t port);
    void RefreshWorldList();
    void LoadSettings(Eng::Engine& engine);
    void SaveSettings(Eng::Engine& engine);
    void RegisterGameActions();
    void LoadInputBindings(const nlohmann::json& j);
    void SaveInputBindings(nlohmann::json& j);
    static constexpr const char* SETTINGS_FILE = "settings.json";

    Eng::client::RenderMesh BuildPlayerMesh();
    Eng::client::RenderMesh BuildHandMesh();
    Eng::client::RenderMesh BuildSkyboxMesh();
    const Eng::client::RenderMesh& GetOrCreateItemMesh(BlockId id);
    Eng::client::RenderMesh BuildItemCubeMesh(BlockId id);

    // ★ 签名已更新为接收 ECS 实体
    void QueueChunkMeshTask(game::client::ClientEntity& chunkEntity);
    void DrainMeshResults(size_t maxCount = 16);

    void HandleServerMessage(const std::vector<uint8_t>& data);
    bool GetInput(float dt);
    void UpdateActiveInputMode();
    void ProcessTouchInput();
    void AddToInventory(BlockId id, int count);
    BlockId GetBlockAt(int wx, int wy, int wz) const;
    bool IsSolidAt(int wx, int wy, int wz) const;
    bool AABBCollides(const glm::vec3& pos) const;
    void SendClickContainer(int16_t slot, uint8_t button, uint8_t mode);

    friend class client::ClientUI;
};

}  // namespace game