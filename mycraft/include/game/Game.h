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
    Eng::client::ShaderPtr CreateShaderByName(const std::string& name, bool isSky);

    Eng::client::WindowManager* winMgr = nullptr;
    Eng::client::Window*       mainWin = nullptr;
    Eng::client::IRenderAPI*   renderer = nullptr;
    uint32_t m_win = 0;

    Eng::client::ShaderPtr      m_cubeShader;
    Eng::client::TexturePtr     m_atlasTexture;
    Eng::client::ShaderPtr      m_skyShader;
    Eng::client::ShaderPtr      m_lodShader;
    Eng::client::ShaderPtr      m_entityShader;
    Eng::client::ShaderPtr      m_handShader;
    Eng::client::DeviceInfo     m_deviceInfo;
    Eng::client::FramebufferPtr m_mainFbo;

    Eng::client::TexturePtr     m_playerTexture;
    std::unique_ptr<Eng::client::AssetManager> m_assetManager;  

    void RecreateFBOs(int w, int h);
    Eng::client::GlobalState    m_globalState;

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

    std::unique_ptr<Eng::IClientTransport> m_client;
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

    // ==================== MDI 全局缓冲与指令 ====================
    std::vector<Eng::client::Vertex> m_globalVertices;
    std::vector<uint32_t>            m_globalIndices;
    bool m_globalGeometryDirty = false;
    
    struct ChunkMeshData {
        uint32_t firstIndex   = 0;
        int32_t  vertexOffset = 0;
        uint32_t indexCount   = 0;
        uint32_t vertexCount  = 0;
    };
    std::unordered_map<uint64_t, ChunkMeshData> m_chunkMeshes;
    std::vector<Eng::client::rhi::DrawIndexedIndirectCommand> m_indirectCommands;
    std::vector<Eng::client::rhi::DrawIndexedIndirectCommand> m_lodIndirectCommands;

    std::unordered_map<uint64_t, game::client::MeshResult> m_allMeshes;

    size_t m_globalVertexCursor   = 0;
    size_t m_globalIndexCursor    = 0;
    size_t m_globalVertexCapacity = 0; // 当前 VBO 的容量（顶点数）
    size_t m_globalIndexCapacity  = 0; // 当前 IBO 的容量（索引数）
    // 记录废弃的内存量
    size_t m_garbageVertices = 0;
    size_t m_garbageIndices  = 0;

    client::MeshManager m_meshManager;
    void QueueChunkMeshTask(uint64_t key);
    void DrainMeshResults();
    void RebuildGlobalGeometry();

    client::LodManager m_lodManager;

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

    Eng::client::MeshPtr m_playerMesh;
    Eng::client::MeshPtr BuildPlayerMesh();
    Eng::client::MeshPtr m_handMesh;
    Eng::client::MeshPtr BuildHandMesh();
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
    std::unordered_map<BlockId, Eng::client::MeshPtr> m_itemMeshCache;
    
    Eng::client::MeshPtr GetOrCreateItemMesh(BlockId id);
    Eng::client::MeshPtr BuildItemCubeMesh(BlockId id);

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

    std::unique_ptr<Eng::Log> logger = std::make_unique<Eng::Log>();

    void ReturnToMainMenu();
    void HandleServerMessage(const std::vector<uint8_t>& data);
    bool GetInput(float dt);

    BlockId GetBlockAt(int wx, int wy, int wz) const;
    bool IsSolidAt(int wx, int wy, int wz) const;
    bool AABBCollides(const glm::vec3& pos) const;
};

} // namespace game