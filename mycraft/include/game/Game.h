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
    Eng::client::TextureHandle  m_skybox;
    Eng::client::Model          m_model;        // 遗留测试模型
    struct SceneObject {
        Eng::client::Model model;
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};   // 欧拉角，度
        glm::vec3 scale{1.0f};
        bool visible = true;
    };
    std::vector<SceneObject>    m_objects;      // 场景对象（遗留）

    // ==================== 网络 ====================
    Eng::NetworkChannel  m_client;
    std::unique_ptr<game::server::GameServer> m_server;
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
    int m_renderDistance    = 8;
    int m_lastPlayerChunkX  = INT_MIN;
    int m_lastPlayerChunkZ  = INT_MIN;
    static constexpr int kRequestsPerTick = 1;

    // ==================== 相机 ====================
    glm::vec3 cameraPos{0.0f, 40.0f, 0.0f};
    glm::vec3 cameraFront{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    float yaw   = -90.0f;
    float pitch =  0.0f;
    glm::mat4 view       = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

    // ==================== Hotbar ====================
    static constexpr BlockType kHotbar[] = {
        BlockType::Stone,
        BlockType::Dirt,
        BlockType::GrassBlock,
        BlockType::Sand
    };
    static constexpr int kHotbarSize = std::size(kHotbar);
    int m_hotbarIndex = 0;

    // ==================== 设置 ====================
    struct Settings {
        float fov         = 75.0f;
        float speed       = 3.0f;
        float sensitivity = 0.01f;
        float zNear       = 0.5f;
        float zFar        = 1000.0f;
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

    // ==================== 指标 ====================
    float m_lastFrameMs  = 0.0f;
    float m_frameMsAvg   = 0.0f;
    float m_frameMsMax   = 0.0f;
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

};

}