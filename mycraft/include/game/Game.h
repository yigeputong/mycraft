#pragma once

#include "Engine.h"
#include "Protocol.h"
#include "core/Transport.h"
#include "game/GameServer.h"
#include "game/core/GameContext.h"
#include "game/client/MeshManager.h"
#include "game/client/ClientWorld.h"
#include "game/client/ClientUI.h"
#include "game/client/LODManager.h"
#include "client/AssetManager.h"
#include <memory>
#include <unordered_map>

namespace game {

class MyGame : public Eng::IGame {
public:
    void OnStart(Eng::Engine& engine) override;
    bool OnUpdate(Eng::Engine& engine, float deltaTime) override;
    void OnRender(Eng::Engine& engine) override;
    void OnShutdown(Eng::Engine& engine) override;

private:
    // ==================== 引擎资源与状态 ====================
    bool m_useVulkan = false;

    // ==================== 初始化流程 ====================
    bool StartLocalServer(bool useMemory = false, bool createClient = true);
    bool ConnectToServer(const std::string& host, uint16_t port);
    bool SetupWindow(Eng::Engine& engine);
    bool SetupRenderer(Eng::Engine& engine);
    void SetupShaders();
    Eng::client::ShaderPtr CreateShaderByName(const std::string& name, bool isSky);

    // ==================== 核心对象 ====================
    Eng::client::WindowManager* winMgr = nullptr;
    Eng::client::Window*       mainWin = nullptr;
    Eng::client::IRenderAPI*   renderer = nullptr;
    uint32_t m_win = 0;

    Eng::client::ShaderPtr      m_cubeShader;
    Eng::client::TexturePtr     m_atlasTexture;
    Eng::client::ShaderPtr      m_skyShader;
    Eng::client::ShaderPtr      m_lodShader;
    Eng::client::DeviceInfo     m_deviceInfo;
    Eng::client::FramebufferPtr m_mainFbo;

    Eng::client::TexturePtr     m_playerTexture;
    std::unique_ptr<Eng::client::AssetManager> m_assets;  

    void RecreateFBOs(int w, int h);

    Eng::client::GlobalState    m_globalState;

    enum class GameState : std::uint8_t {
        MainMenu,
        Connecting,
        Playing,
    };
    GameState m_state = GameState::MainMenu;

    // ==================== 主菜单状态 ====================
    bool m_showMultiplayerDialog = false;
    char m_ipBuffer[128]  = "localhost";
    int  m_portInput      = 25565;
    std::string m_statusText;

    // ==================== 网络 ====================
    std::unique_ptr<Eng::IClientTransport> m_client;
    std::unique_ptr<game::server::GameServer> m_server = nullptr;
    uint32_t m_myClientId = 0;
    std::unordered_map<uint32_t, net::PlayerState> m_otherPlayers;

    // ==================== 世界管理 ====================
    game::BlockRegistry m_blockRegistry;                    // 先构造
    game::GameContext   m_ctx{ m_blockRegistry };           // 绑引用
    game::client::ClientWorld m_world;
    int m_renderDistance = 16;

    float m_timeOfDay = 8.0f;
    static constexpr float kDayLengthSec = 300.0f;
    float m_animTime = 0.0f;

    struct ChunkBuffer {
        int receivedSections = 0;
        std::array<BlockId, CHUNK_VOLUME> blocks;
    };
    std::unordered_map<uint64_t, ChunkBuffer> m_chunkBuffers;

    // ==================== 多线程 Mesh ====================
    client::MeshManager m_meshManager;
    void QueueChunkMeshTask(uint64_t key);
    void DrainMeshResults();

    // ==================== LOD 系统 ====================
    client::LodManager m_lodManager;

    // ==================== 相机与物理 ====================
    std::vector<Eng::client::MeshPtr> m_visibleChunkMeshes;
    std::vector<Eng::client::MeshPtr> m_visibleLodMeshes;

    glm::vec3 cameraPos{0.0f, 40.0f, 0.0f};
    glm::vec3 cameraFront{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    float yaw   = -90.0f;
    float pitch =  0.0f;
    glm::mat4 view       = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

#ifdef NDEBUG
    bool m_flyMode = false;
    bool m_showDebug = false;
#else
    bool m_flyMode = true;
    bool m_showDebug = true;
#endif

    game::client::Frustum m_frustum;
    void UpdateFrustum(const glm::mat4& vp);
    bool AabbInFrustum(const glm::vec3& mn, const glm::vec3& mx) const;

    // ==================== 渲染实体 ====================
    Eng::client::MeshPtr m_playerMesh;
    Eng::client::MeshPtr BuildPlayerMesh();
    Eng::client::MeshPtr m_handMesh;
    Eng::client::MeshPtr BuildHandMesh();
    float m_swingProgress = 0.0f;
    float m_walkDist = 0.0f;

    struct ClientItemEntity : game::PointEntity {
        uint32_t  id          = 0;
        BlockId   itemType    = AIR_ID;   // ★ 改为 BlockId
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
    std::unordered_map<BlockId, Eng::client::MeshPtr> m_itemMeshCache; // ★ 改为 BlockId
    
    Eng::client::MeshPtr GetOrCreateItemMesh(BlockId id);  // ★
    Eng::client::MeshPtr BuildItemCubeMesh(BlockId id);    // ★

    // ==================== Hotbar 与物品 ====================
    static constexpr int kHotbarSize = 4;
    int m_hotbarIndex = 0;
    std::array<game::ItemStack, kHotbarSize> m_hotbar{};   // ★ 移除硬编码初始化，改到 OnStart
    void AddToInventory(BlockId id, int count);            // ★

    bool m_inventoryOpen = false;
    std::array<ItemStack, 27> m_inventory{};

    // ==================== 状态与 UI 标志 ====================
    friend class client::ClientUI;
    client::ClientUI m_ui;

    bool m_shouldQuit = false;
    bool m_menuOpen   = false;
    bool m_minimized  = false;
    bool m_applyPending = false;
    bool m_returnToMenuPending = false;
    bool m_relativeMode = true;
    float m_uiScale = 1.0f;
    int m_settingsTab = 0;

    // ==================== 性能指标 ====================
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

    // ==================== 位置平滑与冷却 ====================
    glm::vec3 m_positionError{0.0f};
    glm::vec3 m_playerVelocity{0.0f};
    bool      m_playerOnGround = false;
    glm::vec3 m_lastMoveDir{0.0f};
    float m_jumpHoldTimer = 0.0f;
    float m_placeCooldown = 0.0f;
    float m_digCooldown   = 0.0f;
    static constexpr float kPlaceInterval = 0.2f;
    static constexpr float kDigInterval   = 0.2f;

    // ==================== 配置持久化 ====================
    static constexpr const char* SETTINGS_FILE = "settings.json";
    bool m_apiChanged = false;

    void LoadSettings(Eng::Engine& engine);
    void SaveSettings(Eng::Engine& engine);

    // ==================== 日志 ====================
    std::unique_ptr<Eng::Log> logger = std::make_unique<Eng::Log>();

    // ==================== 游戏主流程方法 ====================
    void ReturnToMainMenu();

    void HandleServerMessage(const std::vector<uint8_t>& data);
    bool GetInput(float dt);

    BlockId GetBlockAt(int wx, int wy, int wz) const; // ★
    bool IsSolidAt(int wx, int wy, int wz) const;
    bool AABBCollides(const glm::vec3& pos) const;
};

} // namespace game