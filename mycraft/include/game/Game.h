#pragma once

#include "Engine/Engine.h"
#include "Engine/client/SceneRenderer.h"
#include "game/Protocol.h"
#include "Engine/tool/Transport.h"
#include "game/GameServer.h"
#include "game/core/GameContext.h"
#include "game/client/MeshManager.h"
#include "game/client/ClientUI.h"
#include "game/client/LODManager.h"
#include "Engine/service/AssetService.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>

namespace game {

class MyGame : public Eng::IGame {
public:
    void OnStart(Eng::Engine& engine) override;
    bool OnUpdate(Eng::Engine& engine, float deltaTime) override;
    void OnRender(Eng::Engine& engine) override;
    void OnShutdown(Eng::Engine& engine) override;

private:
    bool m_useVulkan = false;

    bool StartLocalServer(bool useMemory = false, bool createClient = true);
    bool ConnectToServer(const std::string& host, uint16_t port);
    bool SetupWindow(Eng::Engine& engine);
    bool SetupRenderer(Eng::Engine& engine);
    void SetupShaders();
    Eng::service::ShaderPtr CreateShaderByName(const std::string& name, bool isSky);

    Eng::service::WindowService* winMgr = nullptr;
    Eng::tool::Window*           mainWin = nullptr;
    Eng::service::IRenderService*   renderer = nullptr;
    uint32_t m_win = 0;

    Eng::service::ShaderPtr      m_skyShader;
    Eng::service::ShaderPtr      m_lodShader;
    Eng::service::ShaderPtr      m_entityShader;
    Eng::service::ShaderPtr      m_handShader;
    Eng::service::DeviceInfo     m_deviceInfo;
    Eng::service::FramebufferPtr m_mainFbo;

    Eng::service::TexturePtr     m_playerTexture;
    Eng::service::TexturePtr     m_atlasTexture;
    Eng::service::AssetService*  m_assetManager = nullptr;   // ★ 归 Engine 所有

    void RecreateFBOs(int w, int h);
    Eng::service::GlobalState    m_globalState;

    enum class GameState : std::uint8_t {
        MainMenu,
        Connecting,
        Playing,
    };
    GameState m_state = GameState::MainMenu;

    bool m_showMultiplayerDialog = false;
    char m_ipBuffer[128]  = "localhost";
    int  m_portInput      = 25565;
    std::string m_statusText;

    std::unique_ptr<Eng::tool::IClientTransport> m_client;
    std::unique_ptr<game::server::GameServer> m_server = nullptr;
    uint32_t m_myClientId = 0;
    std::unordered_map<uint32_t, net::PlayerState> m_otherPlayers;

    game::BlockRegistry m_blockRegistry;
    game::GameContext   m_ctx{ m_blockRegistry };
    game::client::ClientWorld m_world;
    int m_renderDistance = 16;

    float m_timeOfDay = 8.0f;
    static constexpr float kDayLengthSec = 300.0f;
    float m_animTime = 0.0f;

    // ==================== ★ 地形渲染交给 SceneRenderer ====================
    std::unique_ptr<Eng::client::SceneRenderer> m_sceneRenderer;
    // 已提交给 SceneRenderer 的 chunk key（用于检测世界卸载时同步通知）
    std::unordered_set<uint64_t> m_knownSceneChunks;

    client::MeshManager m_meshManager;
    void QueueChunkMeshTask(uint64_t key);
    void DrainMeshResults();

    client::LodManager m_lodManager;

    std::vector<Eng::service::MeshPtr> m_visibleLodMeshes;

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

    Eng::service::MeshPtr m_playerMesh;
    Eng::service::MeshPtr BuildPlayerMesh();
    Eng::service::MeshPtr m_handMesh;
    Eng::service::MeshPtr BuildHandMesh();
    float m_swingProgress = 0.0f;
    float m_walkDist = 0.0f;

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
    std::unordered_map<BlockId, Eng::service::MeshPtr> m_itemMeshCache;

    Eng::service::MeshPtr GetOrCreateItemMesh(BlockId id);
    Eng::service::MeshPtr BuildItemCubeMesh(BlockId id);

    static constexpr int kHotbarSize = 4;
    int m_hotbarIndex = 0;
    std::array<game::ItemStack, kHotbarSize> m_hotbar{};
    void AddToInventory(BlockId id, int count);

    bool m_inventoryOpen = false;
    std::array<ItemStack, 27> m_inventory{};

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

    glm::vec3 m_positionError{0.0f};
    glm::vec3 m_playerVelocity{0.0f};
    bool      m_playerOnGround = false;
    glm::vec3 m_lastMoveDir{0.0f};
    float m_jumpHoldTimer = 0.0f;
    float m_placeCooldown = 0.0f;
    float m_digCooldown   = 0.0f;
    static constexpr float kPlaceInterval = 0.2f;
    static constexpr float kDigInterval   = 0.2f;

    static constexpr const char* SETTINGS_FILE = "settings.json";
    bool m_apiChanged = false;

    void LoadSettings(Eng::Engine& engine);
    void SaveSettings(Eng::Engine& engine);

    std::unique_ptr<Eng::tool::Log> logger = std::make_unique<Eng::tool::Log>();

    void ReturnToMainMenu();
    void HandleServerMessage(const std::vector<uint8_t>& data);
    bool GetInput(float dt);

    BlockId GetBlockAt(int wx, int wy, int wz) const;
    bool IsSolidAt(int wx, int wy, int wz) const;
    bool AABBCollides(const glm::vec3& pos) const;
};

} // namespace game