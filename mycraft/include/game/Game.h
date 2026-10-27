#pragma once

#include "Engine.h"
#include "Protocol.h"
#include "core/Transport.h"
#include "game/GameServer.h"
#include "game/client/MeshManager.h"
#include "game/client/ClientWorld.h"
#include "game/server/WorldGenerator.h"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

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
    Eng::client::ShaderHandle CreateShaderByName(const std::string& name, bool isSky);

    // ==================== 核心对象 ====================
    Eng::client::WindowManager* winMgr = nullptr;
    Eng::client::Window*       mainWin = nullptr;
    Eng::client::IRenderAPI*   renderer = nullptr;
    uint32_t m_win = 0;

    Eng::client::ShaderHandle   m_cubeShader;
    Eng::client::TextureHandle  m_atlasTexture;
    Eng::client::Framebuffer    fbo;
    Eng::client::ShaderHandle   m_skyShader;
    Eng::client::ShaderHandle   m_lodShader;
    Eng::client::DeviceInfo     m_deviceInfo;

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
    game::client::ClientWorld m_world; // ★ 替换掉 m_chunks, m_pending 等
    int m_renderDistance = 16;

    float m_timeOfDay = 8.0f;
    static constexpr float kDayLengthSec = 300.0f;
    float m_animTime = 0.0f;

    // ==================== 多线程 Mesh ====================
    client::MeshManager m_meshManager;
    void QueueChunkMeshTask(uint64_t key, const Chunk& chunk);
    void DrainMeshResults();

    // ==================== LOD 系统 ====================
    std::unique_ptr<game::server::TerrainGenerator> m_lodTerrain;
    struct LodKey {
        int8_t  level;
        int32_t cx, cz;
        bool operator==(const LodKey&) const = default;
    };
    struct LodKeyHash {
        size_t operator()(const LodKey& k) const {
            size_t h = (size_t)(uint8_t)k.level;
            h = h * 31 + (size_t)(uint32_t)k.cx;
            h = h * 31 + (size_t)(uint32_t)k.cz;
            return h;
        }
    };

    struct LodUnit {
        int8_t  level = 1;
        int32_t cx = 0, cz = 0;
        Eng::client::MeshHandle mesh = 0;
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
    };
    std::unordered_map<LodKey, LodUnit, LodKeyHash> m_lodUnits;
    std::unordered_set<LodKey, LodKeyHash>          m_lodPending;

    struct LodTask   { int8_t level; int32_t cx, cz; };
    struct LodResult {
        int8_t  level;
        int32_t cx, cz;
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
        std::vector<Eng::client::Vertex> vertices;
        std::vector<uint32_t>            indices;
    };

    std::vector<std::thread>      m_lodWorkers;
    std::mutex                    m_lodTaskMutex;
    std::condition_variable       m_lodTaskCv;
    std::queue<LodTask>           m_lodTaskQueue;
    std::atomic<bool>             m_lodWorkerStop{false};

    std::mutex                    m_lodResultMutex;
    std::queue<LodResult>         m_lodResultQueue;
    std::vector<LodKey>           m_lodNeedQueue;
    size_t                        m_lodNeedCursor = 0;

    static constexpr int kLodLevels   = 3;
    static constexpr int kLodUnitSize = 16;

    void StartLodWorker(uint32_t seed);
    void StopLodWorker();
    void LodWorkerLoop(uint32_t seed);
    void UpdateLodStreaming();
    void DrainLodResults();
    void BuildLodUnit(const game::server::TerrainGenerator& terrain,
                    int level, int ucx, int ucz,
                    std::vector<Eng::client::Vertex>& outV,
                    std::vector<uint32_t>& outI,
                    glm::vec3& outMin, glm::vec3& outMax) const;
    int m_lastLodPcx = INT_MIN;
    int m_lastLodPcz = INT_MIN;
    void BuildLod1FromChunks(const LodKey& key);
    glm::vec3 m_lastCameraPos{0.0f};
    float     m_stationaryTime = 0.0f;

    // ==================== 相机与物理 ====================
    std::vector<Eng::client::MeshHandle> m_visibleChunkMeshes;
    std::vector<Eng::client::MeshHandle> m_visibleLodMeshes;

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

    game::client::Frustum m_frustum; // 使用 ClientWorld.h 中定义的 Frustum
    void  UpdateFrustum(const glm::mat4& vp);
    bool  AabbInFrustum(const glm::vec3& mn, const glm::vec3& mx) const;

    char m_nameBuffer[32] = "player";

    // ==================== 渲染实体 ====================
    Eng::client::MeshHandle m_playerMesh = 0;
    Eng::client::MeshHandle BuildPlayerMesh();

    struct ClientItemEntity : game::PointEntity {
        uint32_t  id          = 0;
        BlockType itemType    = BlockType::Air;
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
    std::unordered_map<BlockType, Eng::client::MeshHandle> m_itemMeshCache;
    Eng::client::MeshHandle GetOrCreateItemMesh(BlockType type);
    Eng::client::MeshHandle BuildItemCubeMesh(BlockType type);

    // ==================== Hotbar 与物品 ====================
    static constexpr int kHotbarSize = 4;
    int m_hotbarIndex = 0;
    std::array<game::ItemStack, kHotbarSize> m_hotbar = {{
        game::MakeItemStack(BlockType::Stone,      64),
        game::MakeItemStack(BlockType::Dirt,       64),
        game::MakeItemStack(BlockType::GrassBlock, 64),
        game::MakeItemStack(BlockType::Sand,       64),
    }};
    void AddToInventory(BlockType type, int count);

    // 背包与 UI 状态
    bool m_inventoryOpen = false;
    std::array<ItemStack, 27> m_inventory{};

    // ==================== 状态与 UI 标志 ====================
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

    // ==================== 方法 ====================
    void DrawMainMenu(Eng::Engine& engine);
    void DrawUI(Eng::Engine& engine);
    void DrawSettingsMenu(Eng::Engine& engine);
    void ReturnToMainMenu();

    bool keyEvents(const SDL_Event& event);
    bool resizeEvents(const SDL_Event& event);
    bool cursorEvents(const SDL_Event& event);
    bool scrollEvents(const SDL_Event& event);

    void HandleServerMessage(const std::vector<uint8_t>& data);
    bool GetInput(float dt);

    BlockType GetBlockAt(int wx, int wy, int wz) const;
    bool IsSolidAt(int wx, int wy, int wz) const;
    bool AABBCollides(const glm::vec3& pos) const;
};

}