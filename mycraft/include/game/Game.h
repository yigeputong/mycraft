#pragma once

#include "Engine/Engine.h"
#include "Engine/client/SceneRenderer.h"
#include "Engine/tool/Transport.h"
#include "Engine/service/AssetService.h"
#include "game/Protocol.h"
#include "game/GameServer.h"
#include "game/core/GameContext.h"
#include "game/core/RayCast.h"
#include "game/client/MeshManager.h"
#include "game/client/ClientUI.h"
#include "game/client/LODManager.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>

namespace game {

struct GlobalState {
    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::vec3 viewPos        = glm::vec3(0.0f);
    float     aoStrength     = 1.0f;
    glm::vec3 lightDir       = glm::vec3(0.5f, -1.0f, 0.3f);
    float     lightIntensity = 1.0f;
    glm::vec3 lightColor     = glm::vec3(1.0f);
    float     timeOfDay      = 8.0f;
    glm::vec3 lightAmbient   = glm::vec3(0.3f);
    float     _pad2          = 0.0f;
};

class MyGame : public Eng::IGame {
public:
    void OnStart(Eng::Engine& engine) override;
    bool OnUpdate(Eng::Engine& engine, float deltaTime) override;
    void OnRender(Eng::Engine& engine) override;
    void OnShutdown(Eng::Engine& engine) override;

private:
    // ================================================================
    // 1. 引擎注入的底层服务 (由 Engine 注入，MyGame 仅借用，不负责销毁)
    // ================================================================
    Eng::service::WindowService*   winMgr = nullptr;
    Eng::tool::Window*             mainWin = nullptr;
    Eng::service::IRenderService*  renderer = nullptr;
    Eng::service::AssetService*    m_assetManager = nullptr;   // ★ 归 Engine 所有
    uint32_t                       m_win = 0;
    Eng::service::DeviceInfo       m_deviceInfo;
    bool                           m_useVulkan = false;

    // ================================================================
    // 2. 游戏状态与全局配置 (驱动主循环的核心变量)
    // ================================================================
    enum class GameState : std::uint8_t {
        MainMenu,
        Connecting,
        Playing,
    };
    GameState m_state = GameState::MainMenu;

    bool m_shouldQuit = false;
    bool m_menuOpen   = false;
    bool m_minimized  = false;
    bool m_applyPending = false;
    bool m_returnToMenuPending = false;
    bool m_apiChanged = false;
    int  m_settingsTab = 0;
    std::string m_statusText;

    // ================================================================
    // 3. 全局静态资源 (生命周期与 Engine 同寿，在 OnStart / OnShutdown 处理)
    // ================================================================
    // 注册表与全局配置
    game::BlockRegistry m_blockRegistry;
    game::GameContext   m_ctx{ m_blockRegistry };
    int                 m_renderDistance = 32;

    // 核心渲染资产 (地形、天空、实体、手部 Shader 及贴图)
    Eng::service::ShaderPtr      m_cubeShader;
    Eng::service::ShaderPtr      m_skyShader;
    Eng::service::ShaderPtr      m_lodShader;
    Eng::service::ShaderPtr      m_entityShader;
    Eng::service::ShaderPtr      m_handShader;
    Eng::service::TexturePtr     m_playerTexture;
    Eng::service::TexturePtr     m_atlasTexture;
    Eng::service::FramebufferPtr m_mainFbo;
    Eng::service::BufferPtr      m_globalStateUBO;

    // ================================================================
    // 4. 会话资源 (每次进游戏创建，退游戏销毁)
    // ================================================================
    // --- 4.1 网络相关 ---
    std::unique_ptr<Eng::tool::IClientTransport> m_client;
    std::unique_ptr<game::server::GameServer>    m_server = nullptr;
    uint32_t                                     m_myClientId = 0;
    bool                                         m_showMultiplayerDialog = false;
    char                                         m_ipBuffer[128] = "localhost";
    int                                          m_portInput = 25565;
    std::unordered_map<uint32_t, net::PlayerState> m_otherPlayers;

    // --- 4.2 世界与后台线程 ---
    game::client::ClientWorld                    m_world;
    client::MeshManager                          m_meshManager;
    client::LodManager                           m_lodManager;
    std::unordered_set<uint64_t>                 m_knownSceneChunks;
    std::vector<Eng::client::RenderMesh>           m_visibleLodMeshes;

    // --- 4.3 渲染大脑 ---
    std::unique_ptr<Eng::client::SceneRenderer>  m_sceneRenderer;

    // ================================================================
    // 5. 玩家状态与物理 (每次进游戏必须彻底重置)
    // ================================================================
    // 镜头与视角
    glm::vec3 cameraPos{0.0f, 40.0f, 0.0f};
    glm::vec3 cameraFront{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    float yaw   = -90.0f;
    float pitch =  0.0f;
    glm::mat4 view       = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

    // 物理与移动
    glm::vec3 m_positionError{0.0f};
    glm::vec3 m_playerVelocity{0.0f};
    bool      m_playerOnGround = false;
    glm::vec3 m_lastMoveDir{0.0f};

    // 当前准星指着的方块（本地预测，仅用于渲染）
    game::RayHit m_currentRayHit;
    
    // 输入状态与冷却
    bool  m_flyMode = true;
    bool  m_relativeMode = true;
    bool  m_showDebug = true;
    float m_jumpHoldTimer = 0.0f;
    float m_placeCooldown = 0.0f;
    float m_digCooldown   = 0.0f;
    static constexpr float kPlaceInterval = 0.2f;
    static constexpr float kDigInterval   = 0.2f;

    // 时间与动画
    float m_timeOfDay = 8.0f;
    static constexpr float kDayLengthSec = 300.0f;
    float m_animTime = 0.0f;
    float m_swingProgress = 0.0f;
    float m_walkDist = 0.0f;

    // ================================================================
    // 6. 实体、物品与背包
    // ================================================================
    struct ClientItemEntity : game::PointEntity {
        uint32_t  id          = 0;
        BlockId   itemType    = AIR_ID;
        glm::vec3 renderPos{0.0f};
        glm::vec3 prevPos  {0.0f};
        glm::vec3 targetPos{0.0f};
        float     interpT = 1.0f;
        bool      pickingUp   = false;
        float     pickupT     = 0.0f;
        glm::vec3 pickupStart{0.0f};
        uint32_t  pickerId    = 0;
    };
    
    std::unordered_map<uint32_t, ClientItemEntity> m_itemEntities;
    std::unordered_map<BlockId, Eng::client::RenderMesh> m_itemMeshCache;
    Eng::client::RenderMesh m_playerMesh;
    Eng::client::RenderMesh m_handMesh;
    Eng::client::RenderMesh m_skyboxMesh;

    // 物品栏与背包
    static constexpr int kHotbarSize = 4;
    int m_hotbarIndex = 0;
    std::array<game::ItemStack, kHotbarSize> m_hotbar{};
    bool m_inventoryOpen = false;
    std::array<ItemStack, 27> m_inventory{};

    // ================================================================
    // 7. UI 与调试统计面板
    // ================================================================
    GlobalState m_globalState;
    float m_uiScale = 1.0f;

    // FPS 与耗时统计
    float m_lastFrameMs = 0.0f;
    float m_fpsTimer      = 0.0f;
    int   m_fpsFrameCount = 0;
    float m_fpsAccumMs    = 0.0f;
    float m_fpsPeakMs     = 0.0f;

    float m_fpsDisplay       = 0.0f;
    float m_frameMsDisplay   = 0.0f;
    float m_frameMsAvgDisp   = 0.0f;
    float m_frameMsPeakDisp  = 0.0f;

    float m_serverTickMs  = 0.0f;
    float m_serverTickAvg = 0.0f;
    float m_serverTickMax = 0.0f;

    std::unique_ptr<Eng::tool::Log> logger = std::make_unique<Eng::tool::Log>();
    client::ClientUI m_ui;

    // ================================================================
    // 8. 生命周期与初始化辅助函数
    // ================================================================
    void EnterGame();
    void ExitGame();
    void ReturnToMainMenu();

    bool SetupWindow(Eng::Engine& engine);
    bool SetupRenderer(Eng::Engine& engine);
    void SetupShaders();
    void RecreateFBOs(int w, int h);
    Eng::service::ShaderPtr CreateShaderByName(const std::string& name, bool isSky);

    // ================================================================
    // 9. 网络与设置辅助函数
    // ================================================================
    bool StartLocalServer(bool useMemory = false, bool createClient = true);
    bool ConnectToServer(const std::string& host, uint16_t port);
    void LoadSettings(Eng::Engine& engine);
    void SaveSettings(Eng::Engine& engine);
    static constexpr const char* SETTINGS_FILE = "settings.json";

    // ================================================================
    // 10. 渲染与 Mesh 构建辅助函数
    // ================================================================
    Eng::client::RenderMesh BuildPlayerMesh();
    Eng::client::RenderMesh BuildHandMesh();
    Eng::client::RenderMesh BuildSkyboxMesh(); 
    const Eng::client::RenderMesh& GetOrCreateItemMesh(BlockId id);
    Eng::client::RenderMesh BuildItemCubeMesh(BlockId id);
    void QueueChunkMeshTask(uint64_t key);
    void DrainMeshResults(size_t maxCount = 16);

    // ================================================================
    // 11. 游戏逻辑与世界查询辅助函数
    // ================================================================
    void HandleServerMessage(const std::vector<uint8_t>& data);
    bool GetInput(float dt);
    void AddToInventory(BlockId id, int count);
    BlockId GetBlockAt(int wx, int wy, int wz) const;
    bool IsSolidAt(int wx, int wy, int wz) const;
    bool AABBCollides(const glm::vec3& pos) const;

    friend class client::ClientUI;
};

} // namespace game