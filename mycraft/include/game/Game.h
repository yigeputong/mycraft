#pragma once

#include "Engine.h"
#include "Protocol.h"
#include "core/NetworkChannel.h"
#include "game/GameServer.h"
#include <unordered_map>
#include <unordered_set>
#include <queue>

namespace game {

class MyGame : public Eng::IGame {
public:
    void OnStart(Eng::Engine& engine);

    bool OnUpdate(Eng::Engine& engine, float deltaTime);

    void OnRender(Eng::Engine& engine);

    void OnShutdown(Eng::Engine& engine);
private:
    // ==================== 引擎资源 ====================
    Eng::client::WindowManager* winMgr = nullptr;
    Eng::client::Window*       mainWin = nullptr;
    Eng::client::IRenderAPI*   renderer = nullptr;
    uint32_t m_win = 0;

    Eng::client::ShaderHandle   m_cubeShader;
    Eng::client::TextureHandle  m_atlasTexture;
    Eng::client::ShaderHandle   m_fbShader;
    Eng::client::Framebuffer    fbo;
    Eng::client::ShaderHandle   m_skyShader;

    Eng::client::DeviceInfo     m_deviceInfo;

    // ==================== 网络 ====================
    Eng::NetworkChannel  m_client;
    std::unique_ptr<game::server::GameServer> m_server = nullptr;
    uint32_t m_myClientId = 0;
    std::vector<net::PlayerState> m_otherPlayers;

    // ==================== 世界 ====================
    struct ClientChunk {
        Chunk chunk;                        // 原始方块数据（备用）
        Eng::client::MeshHandle mesh = 0;   // GPU 网格
    };
    struct PendingMesh {
        uint64_t chunkKey;
        Chunk    chunk;
    };
    std::unordered_map<uint64_t, ClientChunk> m_chunks;
    std::unordered_set<uint64_t>              m_pending;
    std::queue<PendingMesh>                   m_meshQueue;
    int m_renderDistance    = 16;
    int m_lastPlayerChunkX  = INT_MIN;
    int m_lastPlayerChunkZ  = INT_MIN;
    static constexpr int kRequestsPerTick = 1;

    float m_timeOfDay = 8.0f;                          // 0~24 小时
    static constexpr float kDayLengthSec = 300.0f;     // 一整天 = 5 分钟
    float m_animTime = 0.0f;

    // ==================== 相机 ====================
    glm::vec3 cameraPos{0.0f, 40.0f, 0.0f};
    glm::vec3 cameraFront{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    float yaw   = -90.0f;
    float pitch =  0.0f;
    glm::mat4 view       = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    bool m_flyMode = false;

    // ==================== Hotbar ====================
    static constexpr int kHotbarSize = 4;
    int m_hotbarIndex = 0;
    std::array<game::ItemStack, kHotbarSize> m_hotbar = {{
        game::MakeItemStack(BlockType::Stone,      64),
        game::MakeItemStack(BlockType::Dirt,       64),
        game::MakeItemStack(BlockType::GrassBlock, 64),
        game::MakeItemStack(BlockType::Sand,       64),
    }};

    // ==================== 实体 ====================
    struct ClientItemEntity {
        uint32_t  id;
        glm::vec3 pos;
        BlockType type;
    };
    std::unordered_map<uint32_t, ClientItemEntity> m_itemEntities;
    std::unordered_map<BlockType, Eng::client::MeshHandle> m_itemMeshCache;
    Eng::client::MeshHandle GetOrCreateItemMesh(BlockType type);

    Eng::client::MeshHandle BuildItemCubeMesh(BlockType type);

    // ==================== 设置 ====================
    struct Settings {
        float speed       = 3.0f;
    } s;

    // ==================== 状态标志 ====================
    bool m_shouldQuit = false;
    bool m_menuOpen   = false;
    bool m_minimized  = false;
#ifdef NDEBUG
    bool m_showDebug = false;
#else
    bool m_showDebug = true;
#endif
    bool m_applyPending = false;

    // ==================== 指标 ====================
    float m_lastFrameMs = 0.0f;
    // FPS 统计（每秒刷新）
    float m_fpsTimer      = 0.0f;
    int   m_fpsFrameCount = 0;
    float m_fpsAccumMs    = 0.0f;
    float m_fpsPeakMs     = 0.0f;

    // 显示值（上一秒结算结果，用于 UI）
    float m_fpsDisplay       = 0.0f;
    float m_frameMsDisplay   = 0.0f;
    float m_frameMsAvgDisp   = 0.0f;
    float m_frameMsPeakDisp  = 0.0f;

    float m_serverTickMs  = 0.0f;
    float m_serverTickAvg = 0.0f;
    float m_serverTickMax = 0.0f;

    // ==================== 位置平滑 ====================
    glm::vec3 m_positionError{0.0f};
    glm::vec3 m_playerVelocity{0.0f};
    bool      m_playerOnGround = false;
    glm::vec3 m_lastMoveDir{0.0f};   // 最近一次输入的移动方向
    float m_jumpHoldTimer = 0.0f;
    float m_placeCooldown = 0.0f;   // 距离下次可放还剩多少秒
    float m_digCooldown   = 0.0f;
    static constexpr float kPlaceInterval = 0.2f;   // 放一个后等 0.2 秒
    static constexpr float kDigInterval   = 0.2f;

    // ==================== 日志 ====================
    std::unique_ptr<Eng::Log> logger = std::make_unique<Eng::Log>();

    // 方法
    void DrawUI(Eng::Engine& engine);

    bool keyEvents(const SDL_Event& event);
    bool resizeEvents(const SDL_Event& event);
    bool cursorEvents(const SDL_Event& event);
    bool scrollEvents(const SDL_Event& event);

    void HandleServerMessage(const std::vector<uint8_t>& data);
    bool GetInput(float dt);
    Eng::client::MeshHandle BuildChunkMesh(const Chunk& chunk);
    void UpdateChunkStreaming();

    BlockType GetBlockAt(int wx, int wy, int wz) const;
    bool AABBCollides(const glm::vec3& pos) const;
    bool IsSolidAt(int wx, int wy, int wz) const;
    void AddToInventory(BlockType type, int count);

};

}