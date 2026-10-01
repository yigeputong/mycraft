#pragma once

#include "Engine.h"
#include "Protocol.h"
#include "core/NetworkChannel.h"
#include "game/GameServer.h"
#include <unordered_map>
#include <unordered_set>

namespace game {

class MyGame : public Eng::IGame {
public:
    void OnStart(Eng::Engine& engine);

    bool OnUpdate(Eng::Engine& engine, float deltaTime);

    void OnRender(Eng::Engine& engine);

    void OnShutdown(Eng::Engine& engine);
private:
    //resources
    Eng::client::WindowManager* winMgr;
    uint32_t m_win;
    Eng::client::Window* mainWin;
    Eng::client::IRenderAPI* renderer;

    Eng::client::ShaderHandle m_cubeShader;
    // Eng::client::TextureHandle m_cubeTexture;
    // Eng::client::Material m_cubeMaterial;
    Eng::client::Model m_model;

    Eng::client::TextureHandle m_atlasTexture;

    Eng::client::Framebuffer fbo;
    Eng::client::ShaderHandle m_fbShader;

    Eng::client::TextureHandle m_skybox;

    std::unique_ptr<Eng::Log> logger = std::make_unique<Eng::Log>();

    Eng::NetworkChannel m_client;
    std::unique_ptr<game::server::GameServer> m_server;

    uint32_t m_myClientId = 0;
    std::vector<net::PlayerState> m_otherPlayers;

    static constexpr float zNear = 0.5f;

    //camera
    glm::mat4 model = glm::mat4(1.0);
    glm::mat4 projection = glm::mat4(1.0);
    float yaw = -90.0f;
    float pitch = 0.0f;
    glm::vec3 cameraPos = glm::vec3(0.0f, 40.0f,  0.0f);
    glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

    struct Settings {
        bool fullscreen = false;
        float fov = 75.0f;
        float Speed = 3.0f;
        float Sensitivity = 0.01f;
        float zFar = 1000.0f;
    } s;
#ifdef NDEBUG
    bool m_showDebug = false;   // Release
#else
    bool m_showDebug = true;    // Debug
#endif
    bool m_menuOpen = false;
    bool m_shouldQuit = false;
    bool m_minimized = false;

    struct SceneObject {
        Eng::client::Model model;
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};   // 欧拉角，度
        glm::vec3 scale{1.0f};
        bool visible = true;
    };

    struct ClientChunk {
        Chunk chunk;                        // 原始方块数据（备用）
        Eng::client::MeshHandle mesh = 0;   // GPU 网格
    };

    std::unordered_map<uint64_t, ClientChunk> m_chunks;
    std::unordered_set<uint64_t> m_pending;   // 已请求未收到
    int m_renderDistance = 8;                 // 半径（区块数），以后放设置菜单
    int m_lastPlayerChunkX = INT_MIN;
    int m_lastPlayerChunkZ = INT_MIN;
    constexpr static int kRequestsPerTick = 1;   // 每 tick 最多发几个
    std::vector<SceneObject> m_objects;

    bool keyEvents(const SDL_Event& event);
    bool resizeEvents(const SDL_Event& event);
    bool cursorEvents(const SDL_Event& event);
    bool scrollEvents(const SDL_Event& event);

    void HandleServerMessage(const std::vector<uint8_t>& data);

    bool GetInput(float dt);
    Eng::client::MeshHandle BuildChunkMesh(const Chunk& chunk);
    void UpdateChunkStreaming();

};

}