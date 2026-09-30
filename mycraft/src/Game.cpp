#include "game/Game.h"

#include "Engine.h"
#include "core/Log.h"
#include "core/MessageWriter.h"
#include "game/Protocol.h"
#include "game/GameServer.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include <algorithm>

namespace {

constexpr int ATLAS_TILE = 16;                    // 每格 16x16
constexpr int ATLAS_COLS = 4;
constexpr int ATLAS_ROWS = 4;
constexpr int ATLAS_W    = ATLAS_TILE * ATLAS_COLS;   // 64
constexpr int ATLAS_H    = ATLAS_TILE * ATLAS_ROWS;   // 64
constexpr float TILE_UV  = 1.0f / ATLAS_COLS;         // 0.25

// 简单 hash 噪声，让每个像素略有差异，不至于纯色块
uint8_t HashNoise(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u
               + static_cast<uint32_t>(y) * 668265263u
               + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<uint8_t>((h ^ (h >> 16)) & 0xFF);
}

struct TileSpec { uint8_t r, g, b; int noise; };
const TileSpec kTiles[6] = {
    {128, 128, 128, 30},   // 0 stone
    { 88, 160,  70, 20},   // 1 grass_top
    { 88, 160,  70, 20},   // 2 grass_side（下面单独画土色下半部分）
    {110,  75,  50, 25},   // 3 dirt
    {220, 210, 160, 20},   // 4 sand
    { 40,  90, 180, 15},   // 5 water
};

// 生成 RGBA 像素数组，64x64
std::vector<uint8_t> GenerateAtlasPixels() {
    std::vector<uint8_t> px(ATLAS_W * ATLAS_H * 4, 0);

    for (int t = 0; t < 6; ++t) {
        int col = t % ATLAS_COLS;
        int row = t / ATLAS_COLS;
        int ox = col * ATLAS_TILE;
        int oy = row * ATLAS_TILE;

        for (int y = 0; y < ATLAS_TILE; ++y) {
            for (int x = 0; x < ATLAS_TILE; ++x) {
                int n = static_cast<int>(HashNoise(x, y, t * 31 + 7)) % (kTiles[t].noise * 2 + 1)
                        - kTiles[t].noise;
                int r = kTiles[t].r + n;
                int g = kTiles[t].g + n;
                int b = kTiles[t].b + n;

                // grass_side：上半部分绿、下半部分土，交界处锯齿
                if (t == 2) {
                    bool grass = (y < 4)
                              || (y == 4 && ((x + HashNoise(x, 0, 99)) % 3 != 0));
                    if (!grass) { r = 110 + n; g = 75 + n; b = 50 + n; }
                }

                int i = ((oy + y) * ATLAS_W + (ox + x)) * 4;
                px[i + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                px[i + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                px[i + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                px[i + 3] = 255;
            }
        }
    }
    return px;
}

// 方块 + 面朝向 -> 图集 tile 索引
// face: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
int TileForBlock(game::BlockType b, int face) {
    switch (b) {
        case game::BlockType::Stone:      return 0;
        case game::BlockType::GrassBlock: return (face == 2) ? 1 : 2;   // 顶面草、其余侧面
        case game::BlockType::Dirt:       return 3;
        case game::BlockType::Sand:       return 4;
        case game::BlockType::Water:      return 5;
        default:                    return 0;
    }
}

} // namespace

namespace game {

void MyGame::OnStart(Eng::Engine& engine) {
    m_server = std::make_unique<game::server::GameServer>();
    if (!m_server->Start(25565)) {
        logError(logger, "[Game] Failed to start server");
        return;
    }

    // 客户端连接（主线程，会阻塞几百毫秒）
    if (!m_client.Connect("localhost", 25565)) {
        logError(logger, "[Game] Failed to connect to server");
        return;
    }
    logInfo(logger, "[Game] Connected to server");


    Eng::client::WindowConfig windowConfig = {
        .apitype = Eng::client::RenderAPItype::OPENGL,
        .title = "Mycraft v0.0.0",
        .windowWidth = 1280,
        .windowHeight = 720
    };
    winMgr = engine.GetWindowManager();
    m_win = winMgr->CreateWindow(windowConfig);
    mainWin = winMgr->GetWindow(m_win);
    if (!mainWin) return;

    renderer = mainWin->GetAPI();
    renderer->Initialize(mainWin->GetConfigs().windowWidth, mainWin->GetConfigs().windowHeight, mainWin);
    renderer->SetClearColor(0.125f,0.125f,0.125f,0.0f);

    Eng::client::Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), true);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!ImGui_ImplSDL3_InitForOpenGL(mainWin->GetSDLWindow(), mainWin->GetGLContext())) {
        logError(logger, "ImGui_ImplSDL3_InitForOpenGL failed!");
        return;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) {
        logError(logger, "ImGui_ImplOpenGL3_Init failed!");
        return;
    }

    mainWin->onEvent = [&](const SDL_Event& e) {
        ImGui_ImplSDL3_ProcessEvent(&e);
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard || io.WantCaptureMouse) {
        }
        Eng::client::Input::Get().ProcessEvent(e);
        switch(e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (e.key.key == SDLK_ESCAPE) {
                    engine.Stop();
                    break;
                }
        }
    };

    for (int cx = 0; cx < 4; ++cx) {
        for (int cz = 0; cz < 4; ++cz) {
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkRequest));
            w.Write(cx);
            w.Write(cz);
            m_client.Send(w.GetBuffer());
        }
    }

    auto pixels = GenerateAtlasPixels();
    m_atlasTexture = renderer->CreateTextureFromPixels(
        pixels.data(), ATLAS_W, ATLAS_H);
    logInfo(logger, "[MyGame] atlas texture handle=" << m_atlasTexture);

    m_cubeShader = renderer->CreateShader("./assets/shaders/OpenGL/model/model.vert", "./assets/shaders/OpenGL/model/model.frag");
    // m_cubeTexture = renderer->CreateTexture("./assets/textures/stone.png");
    // m_cubeMaterial.diffuse = m_cubeTexture;

    m_model = renderer->LoadModel("./assets/objects/testBlock0.obj");
    m_model.subMeshes[0].diffuseTexture = m_atlasTexture;

    constexpr int SIZE = 16;
    for (int x = 0; x < SIZE; ++x) {
        for (int z = 0; z < SIZE; ++z) {
            SceneObject obj;
            obj.model = m_model;
            obj.position = glm::vec3(x, 0.0f, z);
            obj.scale = glm::vec3(1.0f);
            m_objects.push_back(obj);
        }
    }

    fbo = renderer->CreateFramebuffer(mainWin->GetConfigs().windowWidth, mainWin->GetConfigs().windowHeight);
    if (!fbo.isValid) {
        logError(logger, "FBO creation failed!");
    }
    std::vector<std::string> skyboxFaces = {
        "./assets/textures/skybox/right.jpg",
        "./assets/textures/skybox/left.jpg",
        "./assets/textures/skybox/top.jpg",
        "./assets/textures/skybox/bottom.jpg",
        "./assets/textures/skybox/front.jpg",
        "./assets/textures/skybox/back.jpg"
    };
    m_skybox = renderer->CreateSkybox(skyboxFaces);
}

bool MyGame::OnUpdate(Eng::Engine& engine, float deltaTime) {

    if (GetInput(deltaTime)) return true;

    std::vector<uint8_t> data;
    while (m_client.Receive(data)) {
        HandleServerMessage(data);
    }

    return false;
}

void MyGame::OnRender(Eng::Engine& engine) {
    renderer->BindFramebuffer(fbo);

        view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        projection = glm::perspective(glm::radians(s.fov), mainWin->GetAspectRatio(), zNear, s.zFar);

        renderer->SetViewMatrix(view);
        renderer->SetProjectionMatrix(projection);
        renderer->SetLightPosition({1.0f, 2.0f, 3.0f});
        renderer->SetViewPosition(cameraPos);

        std::vector<glm::mat4> transforms;
        transforms.reserve(m_objects.size());

        for (const auto& obj : m_objects) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, obj.position);
            model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
            model = glm::scale(model, obj.scale);
            transforms.push_back(model);
        }

        Eng::client::Material mat;
        mat.diffuse = m_atlasTexture;

    renderer->Clear();
        renderer->BeginFrame();

        for (auto& [key, cc] : m_chunks) {
            if (cc.mesh == 0) continue;
            renderer->SetModelMatrix(glm::mat4(1.0f));
            renderer->DrawMesh(cc.mesh, m_cubeShader, mat);
        }

        renderer->DrawSkybox(m_skybox, view);

    renderer->UnbindFramebuffer();
    
    renderer->Clear();

        renderer->DrawFullscreenQuad(fbo.colorTexture);
        
        // ImGui_ImplOpenGL3_NewFrame();
        // ImGui_ImplSDL3_NewFrame();
        // ImGui::NewFrame();

        // // ImGui::ShowDemoWindow();

        // ImGui::Render();
        // ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    winMgr->GetMainWindow()->SwapBuffers();
}

bool MyGame::GetInput(float dt) {
    using namespace Eng::client;

    // ==================== 按 ESC 退出 ====================
    if (Input::IsKeyPressed(KeyCode::Escape)) {
        return true;
    }

    // ==================== F11 全屏切换 ====================
    if (Input::IsKeyPressed(KeyCode::F11)) {
        auto& configs = mainWin->GetConfigs();
        configs.fullscreen = !configs.fullscreen;
        mainWin->SetFullscreen(configs.fullscreen);
        fbo = renderer->CreateFramebuffer(
            mainWin->GetConfigs().windowPixelWidth,
            mainWin->GetConfigs().windowPixelHeight);
    }

    // ==================== 鼠标视角 ====================
    glm::vec2 mouseDelta = Input::GetMouseDelta();
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
        Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), true);
        yaw   += mouseDelta.x * 0.1f;
        pitch -= mouseDelta.y * 0.1f;
        pitch = glm::clamp(pitch, -89.0f, 89.0f);
    } else {
        Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), false);
    }

    // 滚轮 FOV
    float scroll = Input::GetScrollDelta();
    if (scroll != 0.0f) {
        s.fov -= scroll * 2.0f;
        s.fov = glm::clamp(s.fov, 30.0f, 120.0f);
    }

    // 鼠标键盘输入
    glm::vec3 mousefront;
    mousefront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    mousefront.y = sin(glm::radians(pitch));
    mousefront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(mousefront);
    glm::vec3 forward = glm::normalize(glm::vec3(cameraFront.x, 0.0f, cameraFront.z));
    glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    glm::vec3 moveDir = glm::vec3(0.0f);
    if (Input::IsKeyDown(KeyCode::W)) moveDir += forward;
    if (Input::IsKeyDown(KeyCode::S)) moveDir -= forward;
    if (Input::IsKeyDown(KeyCode::D)) moveDir += right;
    if (Input::IsKeyDown(KeyCode::A)) moveDir -= right;
    if (Input::IsKeyDown(KeyCode::Space))  moveDir.y += 1.0f;
    if (Input::IsKeyDown(KeyCode::LShift)) moveDir.y -= 1.0f;
    if (glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
    }
    game::net::PlayerInput input;
    input.moveDir = moveDir;
    input.look    = glm::vec2(glm::radians(yaw), glm::radians(pitch));
    input.jump    = Input::IsKeyDown(KeyCode::Space);

    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::PlayerInput));
    w.WriteVec3(input.moveDir);   // ★ vec3
    w.WriteVec2(input.look);
    w.Write(static_cast<uint8_t>(input.jump ? 1 : 0));
    m_client.Send(w.GetBuffer());
    //本地预测
    float speed = 5.0f * dt;
    cameraPos += input.moveDir * speed;

    return false;
}

void MyGame::OnShutdown(Eng::Engine& engine) {
    m_client.Disconnect();
    if (m_server) {
        m_server->Stop();
        m_server.reset();
    }

    renderer->DestroyShader(m_cubeShader);
    // renderer->DestroyTexture(m_cubeTexture);
    renderer->DestroyShader(m_fbShader);
    renderer->DestroyTexture(m_skybox);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    winMgr->DestroyWindow(m_win);
}

void MyGame::HandleServerMessage(const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    auto type = static_cast<game::net::MessageType>(data[0]);
    Eng::MessageReader r(std::span<const uint8_t>(data).subspan(1));

    switch (type) {
        case game::net::MessageType::WorldState: {
            uint32_t count = r.ReadU32BE();
            for (uint32_t i = 0; i < count; ++i) {
                game::net::PlayerState ps;
                ps.id       = r.ReadU32BE();
                ps.position = r.ReadVec3();
                ps.yaw      = r.Read<float>();
                ps.pitch    = r.Read<float>();

                if (ps.id == m_myClientId) {
                    glm::vec3 serverPos = ps.position;
                    glm::vec3 diff = serverPos - cameraPos;
                    float dist = glm::length(diff);

                    if (dist > 2.0f) {
                        // 偏差太大，直接校正（瞬移）
                        cameraPos = serverPos;
                    } else if (dist > 0.1f) {
                        // 偏差小，平滑修正
                        cameraPos += diff * 0.2f;   // 每帧修正 20%
                    }
                } else {
                    // 其他玩家，存入玩家表（渲染时用）
                    m_otherPlayers[ps.id] = ps;
                }
            }
            break;
        }
        case net::MessageType::PlayerId: {  // YourId
            m_myClientId = r.ReadU32BE();
            logInfo(logger, "[Net] My client ID: " << m_myClientId);
            break;
        }
        case game::net::MessageType::ChunkData: {
            int cx = r.Read<int>();
            int cz = r.Read<int>();
            uint16_t sy = r.Read<uint16_t>();
            if (sy != CHUNK_SIZE_Y) {
                logInfo(logger, "[client] server CHUNK_SIZE_Y=" << static_cast<int>(sy)
                        << " client=" << static_cast<int>(CHUNK_SIZE_Y) << ", reject chunk");
                return;
            }

            ClientChunk cc;
            cc.chunk.chunk_x = cx;
            cc.chunk.chunk_z = cz;

            // 读 4096 字节
            auto bytes = r.ReadBytes(CHUNK_VOLUME);
            std::memcpy(cc.chunk.blocks.data(), bytes.data(), CHUNK_VOLUME);

            // 生成网格
            cc.mesh = BuildChunkMesh(cc.chunk);
            // 快速指纹:对 block 数据做 hash
            uint32_t hash = 2166136261u;
            for (auto b : cc.chunk.blocks) {
                hash = (hash ^ static_cast<uint8_t>(b)) * 16777619u;
            }
            m_chunks[ChunkKey(cx, cz)] = std::move(cc);
            break;
        }
    }
}

Eng::client::MeshHandle MyGame::BuildChunkMesh(const Chunk& chunk) {
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t> indices;

    const int cx = chunk.chunk_x;
    const int cz = chunk.chunk_z;
    const int baseX = cx * CHUNK_SIZE_X;
    const int baseZ = cz * CHUNK_SIZE_Z;

    // ★ 拿 4 个邻居（可能为 null——还没收到）
    auto findChunk = [this](int ccx, int ccz) -> const Chunk* {
        auto it = m_chunks.find(ChunkKey(ccx, ccz));
        if (it == m_chunks.end()) return nullptr;
        return &it->second.chunk;
    };
    const Chunk* nXm = findChunk(cx - 1, cz);
    const Chunk* nXp = findChunk(cx + 1, cz);
    const Chunk* nZm = findChunk(cx, cz - 1);
    const Chunk* nZp = findChunk(cx, cz + 1);

    // ★ 跨区块取方块，接受 lx/lz ∈ [-1, 16]
    auto getBlock = [&](int lx, int ly, int lz) -> BlockType {
        if (ly < 0 || ly >= CHUNK_SIZE_Y) return BlockType::Air;
        if (lx >= 0 && lx < CHUNK_SIZE_X && lz >= 0 && lz < CHUNK_SIZE_Z)
            return ChunkGet(chunk, lx, ly, lz);

        const Chunk* n = nullptr;
        int nlx = lx, nlz = lz;
        if      (lx < 0)                  { n = nXm; nlx = lx + CHUNK_SIZE_X; }
        else if (lx >= CHUNK_SIZE_X)      { n = nXp; nlx = lx - CHUNK_SIZE_X; }
        else if (lz < 0)                  { n = nZm; nlz = lz + CHUNK_SIZE_Z; }
        else if (lz >= CHUNK_SIZE_Z)      { n = nZp; nlz = lz - CHUNK_SIZE_Z; }

        if (!n) return BlockType::Air;   // 邻居没到，当空气（多画一个面）
        if (nlx < 0 || nlx >= CHUNK_SIZE_X) return BlockType::Air;  // 对角线情况
        if (nlz < 0 || nlz >= CHUNK_SIZE_Z) return BlockType::Air;
        return ChunkGet(*n, nlx, ly, nlz);
    };

    // 6 个面的顶点偏移
    static constexpr int faceOffsets[6][4][3] = {
        // +X
        {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},
        // -X
        {{0,0,1},{0,1,1},{0,1,0},{0,0,0}},
        // +Y
        {{0,1,1},{1,1,1},{1,1,0},{0,1,0}},
        // -Y
        {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
        // +Z
        {{1,0,1},{1,1,1},{0,1,1},{0,0,1}},
        // -Z
        {{0,0,0},{0,1,0},{1,1,0},{1,0,0}},
    };
    static constexpr int faceNormals[6][3] = {
        { 1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
    };
    static constexpr float faceUVs[4][2] = {
        {0,0},{0,1},{1,1},{1,0}
    };

    for (int lx = 0; lx < CHUNK_SIZE_X; ++lx) {
        for (int ly = 0; ly < CHUNK_SIZE_Y; ++ly) {
            for (int lz = 0; lz < CHUNK_SIZE_Z; ++lz) {
                BlockType type = ChunkGet(chunk, lx, ly, lz);
                if (!IsSolid(type)) continue;

                // 检查 6 个面
                for (int f = 0; f < 6; ++f) {
                    int nx = lx + faceNormals[f][0];
                    int ny = ly + faceNormals[f][1];
                    int nz = lz + faceNormals[f][2];

                    // 检查相邻方块（跨区块暂时不查，简化）
                    BlockType neighbor = getBlock( nx, ny, nz);
                    if (IsSolid(neighbor)) continue;   // 被遮挡，跳过

                    uint32_t baseIndex = (uint32_t)vertices.size();

                    for (int v = 0; v < 4; ++v) {
                        Eng::client::Vertex vert;
                        vert.position = {
                            baseX + lx + faceOffsets[f][v][0],
                            (float)ly + faceOffsets[f][v][1],
                            baseZ + lz + faceOffsets[f][v][2]
                        };
                        vert.normal = {
                            (float)faceNormals[f][0],
                            (float)faceNormals[f][1],
                            (float)faceNormals[f][2]
                        };
                        int tile = TileForBlock(type, f);
                        int tileRow = tile / ATLAS_COLS;
                        int tileCol = tile % ATLAS_COLS;
                        float u0 = static_cast<float>(tileCol) * TILE_UV;
                        float v0 = static_cast<float>(tileRow) * TILE_UV;
                        vert.uv = {
                            u0 + faceUVs[v][0] * TILE_UV,
                            v0 + (1.0f - faceUVs[v][1]) * TILE_UV
                        };
                        vertices.push_back(vert);
                    }

                    indices.push_back(baseIndex + 0);
                    indices.push_back(baseIndex + 1);
                    indices.push_back(baseIndex + 2);
                    indices.push_back(baseIndex + 2);
                    indices.push_back(baseIndex + 3);
                    indices.push_back(baseIndex + 0);
                }
            }
        }
    }

    // 上传到 GPU
    if (vertices.empty()) return 0;

    Eng::client::MeshData data;
    data.vertices = vertices;
    data.indices = indices;

    auto handle = renderer->CreateMesh(data);

    uint64_t key = ChunkKey(chunk.chunk_x, chunk.chunk_z);

    return handle;
}

}