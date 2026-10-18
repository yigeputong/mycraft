#include "game/Game.h"

#include "Engine.h"
#include "core/Log.h"
#include "core/MessageWriter.h"
#include "core/MemoryTransport.h"
#include "core/NetworkTransport.h"
#include "game/Protocol.h"
#include "game/GameServer.h"
#include "game/core/Blocks.h"   
#include "game/core/Physics.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include <SDL3/SDL_timer.h>
#include <algorithm>

namespace {

constexpr int ATLAS_TILE = 16;                    // 每格 16x16
constexpr int ATLAS_COLS = 8;
constexpr int ATLAS_ROWS = 8;
constexpr int ATLAS_W    = ATLAS_TILE * ATLAS_COLS;
constexpr int ATLAS_H    = ATLAS_TILE * ATLAS_ROWS;
constexpr float TILE_UV  = 1.0f / ATLAS_COLS;

// 简单 hash 噪声，让每个像素略有差异，不至于纯色块
constexpr auto HashNoise = [](int x, int y, uint32_t seed) -> uint8_t {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u
               + static_cast<uint32_t>(y) * 668265263u
               + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<uint8_t>((h ^ (h >> 16)) & 0xFF);
};

struct TileSpec { uint8_t r, g, b; int noise; };
const TileSpec kTiles[19] = {
    {128, 128, 128, 30},    // 0 stone
    { 88, 160,  70, 20},    // 1 grass_top
    { 88, 160,  70, 20},    // 2 grass_side
    {110,  75,  50, 25},    // 3 dirt
    {220, 210, 160, 20},    // 4 sand
    { 40,  90, 180, 15},    // 5 water
    {210, 218, 228, 20},    // 6 snow_top（微蓝白，加大 noise）
    {210, 218, 228, 20},    // 7 snow_side
    {200, 180, 130, 25},    // 8 sandstone（浅黄）
    { 60,  60,  60, 25},    // 9  coal_ore（深灰带黑点）
    {180, 140, 100, 25},   // 10 iron_ore（浅棕带橙点）
    {139, 90,  43, 25},    // 11 wood（棕色，纵向纹）
    { 42, 100, 45, 35},    // 12 leaves（深绿，大 noise）
    {224, 180, 150, 15},   // 13 skin  （肉色）
    { 60, 110, 200, 20},   // 14 shirt （蓝）
    { 45,  55, 110, 20},   // 15 pants （深蓝）
    { 88, 160,  70, 20},   // 16 TallGrass (绿)
    {220,  40,  40, 10},   // 17 FlowerRed (红)
    {220, 200,  40, 10}   // 18 FlowerYellow (黄)
};

// 生成 RGBA 像素数组，64x64
std::vector<uint8_t> GenerateAtlasPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(ATLAS_W) * ATLAS_H * 4, 0);

    for (int t = 0; t < static_cast<int>(sizeof(kTiles) / sizeof(TileSpec)); ++t) {
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

                if (t == 2 || t == 7) {   // ★ 草侧面 / 雪侧面
                    bool top = (y < 4) || (y == 4 && ((x + HashNoise(x, 0, 99)) % 3 != 0));
                    if (!top) { r = 110 + n; g = 75 + n; b = 50 + n; }   // 下半土色
                }
                if (t == 11) {
                    if ((x + HashNoise(0, y, 42)) % 4 == 0) {
                        r = 110 + n; g = 70 + n; b = 30 + n;   // 深色条纹
                    }
                }

                int i = ((oy + y) * ATLAS_W + (ox + x)) * 4;

                if (t >= 16) {
                    // 默认全透明
                    px[i + 0] = 0; px[i + 1] = 0; px[i + 2] = 0; px[i + 3] = 0;

                    if (t == 16) { // 高草：随机几根草
                        if (HashNoise(x, y, 42) % 3 == 0 && y > 4) {
                            px[i + 0] = 88; px[i + 1] = 160; px[i + 2] = 70; px[i + 3] = 255;
                        }
                    } else if (t == 17 || t == 18) { // 花：茎 + 花瓣
                        if (x >= 7 && x <= 8 && y >= 7 && y <= 15) { // 茎
                            px[i + 0] = 42; px[i + 1] = 100; px[i + 2] = 45; px[i + 3] = 255;
                        } else if (x >= 5 && x <= 10 && y >= 3 && y <= 8) { // 花瓣
                            px[i + 0] = (t == 17) ? 220 : 220;
                            px[i + 1] = (t == 17) ? 40  : 200;
                            px[i + 2] = (t == 17) ? 40  : 40;
                            px[i + 3] = 255;
                        }
                    }
                    continue; // 跳过下面的默认不透明赋值
                }

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
        case game::BlockType::Stone:        return 0;
        case game::BlockType::GrassBlock:
            if (face == 2) return 1;
            if (face == 3) return 3;
            return 2;
        case game::BlockType::Dirt:         return 3;
        case game::BlockType::Sand:         return 4;
        case game::BlockType::Water:        return 5;
        case game::BlockType::Snow:
            // 顶=6（纯白）、底=3（土）、侧面=7
            if (face == 2) return 6;
            if (face == 3) return 3;
            return 7;

        case game::BlockType::Sandstone:
            return 8;
        case game::BlockType::CoalOre:      return 9;
        case game::BlockType::IronOre:      return 10;
        case game::BlockType::Wood:
            return 11;
        case game::BlockType::Leaves:       return 12;
        case game::BlockType::TallGrass:    return 16;
        case game::BlockType::FlowerRed:    return 17;
        case game::BlockType::FlowerYellow: return 18;
        default:                    return 0;
    }
}

static const char* BlockName(game::BlockType t) {
    switch (t) {
        case game::BlockType::Stone:      return "Stone";
        case game::BlockType::Dirt:       return "Dirt";
        case game::BlockType::GrassBlock: return "Grass";
        case game::BlockType::Sand:       return "Sand";
        case game::BlockType::Water:      return "Water";
        default:                    return "?";
    }
}

} // namespace

namespace game {

// ============================================================
// OnStart 分阶段
// ============================================================

bool MyGame::StartLocalServer(bool useMemory, bool createClient) {
    if (m_server) return true;

    auto server = std::make_unique<game::server::GameServer>();
    if (!server->Start(25565, "./saves/world", useMemory)) {
        logError(logger, "[Game] Failed to start local server");
        return false;
    }

    if (createClient) {
        if (useMemory) {
            m_client = std::make_unique<Eng::MemoryClientTransport>(server->GetMemoryPipe());
            m_client->Connect("", 0);
            logInfo(logger, "[Game] Started memory transport for single player");
        } else {
            m_client = std::make_unique<Eng::NetworkClientTransport>();
            m_client->Connect("127.0.0.1", 25565);
        }
    }

    m_server = std::move(server);
    return true;
}

bool MyGame::ConnectToServer(const std::string& host, uint16_t port) {
    m_client = std::make_unique<Eng::NetworkClientTransport>();
    if (!m_client->Connect(host, port)) {
        logError(logger, "[Game] Failed to connect to " << host << ":" << port);
        m_client.reset();
        return false;
    }
    logInfo(logger, "[Game] Connected to " << host << ":" << port);
    return true;
}

bool MyGame::SetupWindow(Eng::Engine& engine) {
    Eng::client::WindowConfig windowConfig = {
        .apitype      = m_apiType,
        .title        = "Mycraft v" MYCRAFT_VERSION,
        .windowWidth  = 1280,
        .windowHeight = 720
    };

    winMgr = engine.GetWindowManager();
    m_win  = winMgr->CreateWindow(windowConfig);
    mainWin = winMgr->GetWindow(m_win);
    if (!mainWin) return false;

    mainWin->onEvent = [this](const SDL_Event& e) {
        ImGui_ImplSDL3_ProcessEvent(&e);
        Eng::client::Input::Get().ProcessEvent(e);
        switch (e.type) {
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                renderer->DestroyFramebuffer(fbo);
                fbo = renderer->CreateFramebuffer(e.window.data1, e.window.data2);
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                m_minimized = true;
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                m_minimized = false;
                break;
            default:
                break;
        }
    };

    Eng::client::Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), false);
    m_relativeMode = false;
    return true;
}

bool MyGame::SetupRenderer(Eng::Engine& /*engine*/) {
    renderer = mainWin->GetAPI();
    if (!renderer) {
        logError(logger, "[Game] renderer is null");
        return false;
    }

    try {
        renderer->Initialize(mainWin->GetConfigs().windowWidth,
                            mainWin->GetConfigs().windowHeight,
                            mainWin);
    } catch (const std::exception& e) {
        logFatal(logger, "[RenderAPI] RenderAPI init failed: " << e.what());
        return false;
    }

    // 从 renderer 反推用哪个后端 —— 不依赖 OnStart 里硬编码的 m_apiType
    m_deviceInfo = renderer->GetDeviceInfo();
    m_useVulkan  = (m_deviceInfo.backend == "Vulkan");

    logInfo(logger, "[RenderAPI] Backend:     " << m_deviceInfo.backend);
    logInfo(logger, "[RenderAPI] Device:      " << m_deviceInfo.deviceName);
    logInfo(logger, "[RenderAPI] Vendor:      " << m_deviceInfo.vendor);
    logInfo(logger, "[RenderAPI] API version: " << m_deviceInfo.apiVersion);
    logInfo(logger, "[RenderAPI] Driver:      " << m_deviceInfo.driverVersion);
    logInfo(logger, "[RenderAPI] Shader lang: " << m_deviceInfo.shadingLanguage);

    renderer->SetClearColor(0.125f, 0.125f, 0.125f, 0.0f);

    if (!renderer->InitImGuiBackend()) {
        logError(logger, "[Game] ImGui backend init failed");
        return false;
    }

    // 图集纹理
    auto pixels = GenerateAtlasPixels();
    m_atlasTexture = renderer->CreateTextureFromPixels(pixels.data(), ATLAS_W, ATLAS_H);
    logInfo(logger, "[MyGame] atlas texture created");

    // 主 FBO
    fbo = renderer->CreateFramebuffer(mainWin->GetConfigs().windowWidth,
                                       mainWin->GetConfigs().windowHeight);
    if (!fbo.isValid()) {
        logError(logger, "[Game] FBO creation failed!");
        return false;
    }

    return true;
}

void MyGame::SetupShaders() {
    m_cubeShader = CreateShaderByName("mesh", false);
    m_skyShader  = CreateShaderByName("sky",  true);
    m_lodShader = CreateShaderByName("lod", false);
}

Eng::client::ShaderHandle MyGame::CreateShaderByName(const std::string& name, bool isSky) {
    const std::string ext  = m_useVulkan ? ".vk.spv" : ".gl.spv";
    const std::string base = "./assets/shaders/build/" + name + "/";
    const std::string vp   = base + "vert" + ext;
    const std::string fp   = base + "frag" + ext;
    return isSky ? renderer->CreateSkybox(vp, fp)
                 : renderer->CreateShader(vp, fp);
}

void MyGame::OnStart(Eng::Engine& engine) {
    if (!logger) logger = std::make_unique<Eng::Log>();

    // ============ Dedicated server 模式：无窗口，自动启服务端 ============
    if (engine.IsDedicatedServer()) {
        // 走网络模式，且不创建本地客户端
        if (!StartLocalServer(false, false)) {
            logError(logger, "[Game] Failed to start dedicated server");
            m_shouldQuit = true;
            return;
        }
        logInfo(logger, "[Game] Running as dedicated server");
        return;
    }

    // ============ 交互模式：建窗口/渲染器，进主菜单 ============
    if (!SetupWindow(engine))   return;
    if (!SetupRenderer(engine)) return;
    SetupShaders();

    m_state = GameState::MainMenu;
}

bool MyGame::OnUpdate(Eng::Engine&, float deltaTime) {
    if (m_minimized) {
        SDL_Delay(16);
        return false;
    }

    if (m_returnToMenuPending) {
        m_returnToMenuPending = false;
        ReturnToMainMenu();
        return false;   // 本帧不再跑游戏逻辑
    }

    const bool wantRelative = (m_state == GameState::Playing) && !m_menuOpen;
    if (wantRelative != m_relativeMode) {
        Eng::client::Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), wantRelative);
        m_relativeMode = wantRelative;
    }

    // ---- FPS 统计（任何状态都跑，不改）----
    float frameMs = deltaTime * 1000.0f;
    m_lastFrameMs = frameMs;
    m_fpsTimer      += deltaTime;
    m_fpsFrameCount += 1;
    m_fpsAccumMs    += frameMs;
    m_fpsPeakMs      = std::max(m_fpsPeakMs, frameMs);
    if (m_fpsTimer >= 1.0f) {
        m_fpsDisplay      = m_fpsFrameCount / m_fpsTimer;
        m_frameMsDisplay  = frameMs;
        m_frameMsAvgDisp  = m_fpsAccumMs / m_fpsFrameCount;
        m_frameMsPeakDisp = m_fpsPeakMs;
        m_fpsTimer = 0.0f; m_fpsFrameCount = 0;
        m_fpsAccumMs = 0.0f; m_fpsPeakMs = 0.0f;
    }

    m_animTime += deltaTime;

    // 主菜单状态：只更新动画时间，不跑物理/网络/流式加载
    if (m_state != GameState::Playing) {
        return m_shouldQuit;
    }

    game::PlayerMotion m;
    m.position = cameraPos;
    m.velocity = m_playerVelocity;
    m.onGround = m_playerOnGround;
    m.flyMode  = m_flyMode;   // ★

    game::StepPlayer(m, m_lastMoveDir, m_jumpHoldTimer > 0.0f, deltaTime,
        [this](int x, int y, int z) { return IsSolidAt(x, y, z); });

    cameraPos        = m.position;
    m_playerVelocity = m.velocity;
    m_playerOnGround = m.onGround;

    constexpr float CORRECTION_RATE = 8.0f;
    float t = 1.0f - std::exp(-CORRECTION_RATE * deltaTime);
    cameraPos      += m_positionError * t;
    m_positionError *= (1.0f - t);

    // 一天 5 分钟，24 小时 = 300 秒
    float hoursPerSec = 24.0f / kDayLengthSec;
    m_timeOfDay = std::fmod(m_timeOfDay + hoursPerSec * deltaTime, 24.0f);

    // ---- 太阳方向 ----
    float angle = (m_timeOfDay / 24.0f) * 6.2831853f - 1.5707963f;
    glm::vec3 sunDir = glm::normalize(glm::vec3(cos(angle), sin(angle), 0.3f));
    float sunH = sunDir.y;

    // ---- 光强：夜晚为 0，白天为 1 ----
    float sunI = glm::clamp(sunH * 4.0f, 0.0f, 1.0f);

    // ---- 光色：正午白，黄昏偏橙 ----
    float warm = 1.0f - glm::clamp(sunH * 3.0f, 0.0f, 1.0f);
    glm::vec3 sunColor = glm::mix(glm::vec3(1.0f, 1.0f, 1.0f),
                                glm::vec3(1.0f, 0.5f, 0.2f), warm);

    // ---- 环境光：夜暗蓝，昼亮白 ----
    glm::vec3 ambient = glm::mix(glm::vec3(0.10f, 0.12f, 0.20f),     // 夜
                            glm::vec3(0.55f, 0.60f, 0.70f),      // 昼
                            glm::clamp(sunH * 2.0f, 0.0f, 1.0f));

    // ---- 传给渲染器 ----
    renderer->SetLightPosition(sunDir);
    renderer->SetLightColor(sunColor, sunI);
    renderer->SetLightAmbient(ambient);

    if (GetInput(deltaTime)) return true;

    std::vector<uint8_t> data;
    while (m_client->Receive(data)) {
        HandleServerMessage(data);
    }

    constexpr float PICKUP_TIME = 0.2f;   // 0.2 秒飞向玩家
    for (auto it = m_itemEntities.begin(); it != m_itemEntities.end(); ) {
        auto& e = it->second;

        // ---- 捡拾动画 ----
        if (e.pickingUp) {
            e.pickupT += deltaTime / PICKUP_TIME;
            if (e.pickupT >= 1.0f) {
                if (e.pickerId == m_myClientId) {       // 只有捡的人进背包
                    AddToInventory(e.itemType, 1);
                }
                it = m_itemEntities.erase(it);
                continue;
            }
            // 飞向玩家眼睛
            glm::vec3 target = cameraPos + glm::vec3(0.0f, PLAYER_EYE / 2, 0.0f);
            float pickupT = e.pickupT;
            pickupT = pickupT * pickupT;   // ease-in：先慢后快
            e.renderPos = glm::mix(e.pickupStart, target, pickupT);
            ++it;
            continue;
        }

        // ---- 位置插值（原有）----
        if (e.interpT < 1.0f) {
            e.interpT += deltaTime * 20.0f;
            if (e.interpT > 1.0f) e.interpT = 1.0f;
            e.renderPos = glm::mix(e.prevPos, e.targetPos, e.interpT);
        }
        ++it;
    }

    UpdateChunkStreaming();
    UpdateLodStreaming();
    DrainLodResults();

    if (!m_meshQueue.empty()) {
        auto pm = m_meshQueue.front();
        m_meshQueue.pop();

        auto it = m_chunks.find(pm.chunkKey);
        if (it != m_chunks.end() && it->second.mesh == 0) {
            it->second.mesh = BuildChunkMesh(pm.chunk);
        }
    }

    return false;
}

void MyGame::OnRender(Eng::Engine& engine) {
    if (m_minimized) {
        return;
    }
    if (m_applyPending) {
        renderer->ApplySettings(mainWin->GetConfigs(), engine.GetConfig().render);
        m_applyPending = false;
    }
    renderer->BeginFrame();

    glm::mat4 vp = projection * view;
    UpdateFrustum(vp);

    // ============ Pass 1: 场景 → FBO ============
    renderer->BindFramebuffer(fbo);
    renderer->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    renderer->Clear();

    if (m_state == GameState::Playing) {
        glm::vec3 eye = cameraPos + glm::vec3(0.0f, PLAYER_EYE, 0.0f);
        view = glm::lookAt(eye, eye + cameraFront, cameraUp);
        projection = glm::perspective(glm::radians(engine.GetConfig().render.fov), 
                                mainWin->GetAspectRatio(), 
                                engine.GetConfig().render.zNear, 
                                engine.GetConfig().render.zFar);

        renderer->SetViewMatrix(view);
        renderer->SetProjectionMatrix(projection);
        renderer->SetViewPosition(cameraPos);

        Eng::client::Material mat;
        mat.diffuse = m_atlasTexture;

        renderer->SetUniform(m_cubeShader, "uAOStrength", engine.GetConfig().render.m_aoStrength);
        for (auto& [key, cc] : m_chunks) {
            if (cc.mesh == 0) continue;
            renderer->SetModelMatrix(glm::mat4(1.0f));
            renderer->DrawMesh(cc.mesh, m_cubeShader, mat);
        }

        // ---- 远景 LOD ----
        renderer->SetModelMatrix(glm::mat4(1.0f));
        for (auto& [key, u] : m_lodUnits) {
            if (u.mesh == 0) continue;
            if (!AabbInFrustum(u.aabbMin, u.aabbMax)) { continue; }
            Eng::client::Material lodMat;
            lodMat.diffuse = m_atlasTexture;
            renderer->DrawMesh(u.mesh, m_lodShader, lodMat);
        }

        // ---- 其他玩家 ----
        if (m_playerMesh == 0) m_playerMesh = BuildPlayerMesh();
        Eng::client::Material playerMat;
        playerMat.diffuse = m_atlasTexture;
        for (auto& [id, ps] : m_otherPlayers) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, ps.position);
            // ps.yaw 是弧度（协议里传的），但服务端 yaw 语义看 player input
            // 现在 input.look.x 是 radians(yaw)，所以直接用
            model = glm::rotate(model, ps.yaw, glm::vec3(0, 1, 0));
            renderer->SetModelMatrix(model);
            renderer->DrawMesh(m_playerMesh, m_cubeShader, playerMat);
        }

        // ---- 实体 ---- 
        Eng::client::Material itemMat;
        itemMat.diffuse = m_atlasTexture;

        // ---- 所有人保持 Q 弹!!! ----
        constexpr float BOUNCE_FREQ = 1.0f / 0.4f;   // 每 0.4 秒一次
        constexpr float BOUNCE_AMP  = 0.45f;
        constexpr float XZ_RATIO    = 0.8f;

        float t = m_animTime * BOUNCE_FREQ * 6.2831853f;

        float sY  = 1.0f + BOUNCE_AMP * std::sin(t);
        float sXZ = 1.0f - BOUNCE_AMP * XZ_RATIO * std::sin(t);

        for (auto& [id, e] : m_itemEntities) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, e.renderPos);
            model = model * glm::rotate(glm::mat4(1.0f), m_animTime * 2.0f, glm::vec3(0, 1, 0));   // 见下方注意
            model = glm::scale(model, glm::vec3(sXZ * 0.3f, sY * 0.3f, sXZ * 0.3f));

            renderer->SetModelMatrix(model);
            auto mesh = GetOrCreateItemMesh(e.itemType);
            renderer->DrawMesh(mesh, m_cubeShader, itemMat);
        }

        renderer->SetUniform(m_skyShader, "uTimeOfDay", m_timeOfDay);
        renderer->DrawSkybox(m_skyShader);
    }

    renderer->UnbindFramebuffer();
    
    // ============ Pass 2: 后处理 → 屏幕 ============
    renderer->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    renderer->Clear();

        renderer->DrawFullscreenQuad(renderer->GetFramebufferTexture(fbo));

        DrawUI(engine);

    renderer->EndFrame();
}

void MyGame::DrawMainMenu(Eng::Engine& /*engine*/) {
    ImGuiIO& io = ImGui::GetIO();

    // ---- 主面板 ----
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);

    if (ImGui::Begin("##MainMenu", nullptr,
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar)) {
        ImGui::SetWindowFontScale(2.0f);
        ImGui::TextUnformatted("Mycraft");
        ImGui::SetWindowFontScale(1.4f);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::InputText("Name", m_nameBuffer, sizeof(m_nameBuffer));
        ImGui::Spacing();

        if (ImGui::Button("Single Player", ImVec2(-1, 50))) {
            if (StartLocalServer(true, true)) {
                m_state = GameState::Playing;
                m_statusText.clear();
            } else {
                m_statusText = "Failed to start local server";
            }
        }

        if (ImGui::Button("Multiplayer", ImVec2(-1, 50))) {
            m_showMultiplayerDialog = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Quit", ImVec2(-1, 40))) {
            m_shouldQuit = true;
        }

        if (!m_statusText.empty()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_statusText.c_str());
        }
    }
    ImGui::End();

    // ---- 多人连接对话框 ----
    if (m_showMultiplayerDialog) ImGui::OpenPopup("Connect to Server");

    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Connect to Server", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Address", m_ipBuffer, sizeof(m_ipBuffer));
        ImGui::InputInt("Port", &m_portInput);
        ImGui::Spacing();

        if (ImGui::Button("Connect", ImVec2(120, 40))) {
            if (ConnectToServer(m_ipBuffer, (uint16_t)m_portInput)) {
                m_state = GameState::Playing;
                m_showMultiplayerDialog = false;
                m_statusText.clear();
                ImGui::CloseCurrentPopup();
            } else {
                m_statusText = "Connection failed";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 40))) {
            m_showMultiplayerDialog = false;
            m_statusText.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MyGame::DrawUI(Eng::Engine& engine) {
    renderer->ImGuiNewFrame();
    ImGui::NewFrame();

    if (m_state == GameState::MainMenu) {
        DrawMainMenu(engine);
    } else {    
        // 准心
        if (!m_menuOpen) {
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
            ImDrawList* dl = ImGui::GetForegroundDrawList();   // ★ 前景层，画在所有窗口之上

            constexpr float LEN    = 8.0f;   // 每条臂的长度
            constexpr float THICK  = 2.0f;   // 线宽
            constexpr float GAP    = 2.0f;   // 中心留空
            ImU32 color = IM_COL32(255, 255, 255, 200);

            dl->AddLine(ImVec2(center.x - GAP - LEN, center.y),
                        ImVec2(center.x - GAP,       center.y), color, THICK);
            dl->AddLine(ImVec2(center.x + GAP,       center.y),
                        ImVec2(center.x + GAP + LEN, center.y), color, THICK);
            dl->AddLine(ImVec2(center.x, center.y - GAP - LEN),
                        ImVec2(center.x, center.y - GAP),       color, THICK);
            dl->AddLine(ImVec2(center.x, center.y + GAP),
                        ImVec2(center.x, center.y + GAP + LEN), color, THICK);
        }

        // 物品栏
        if (!m_menuOpen) {
            ImGuiIO& io = ImGui::GetIO();
            constexpr float SLOT = 50.0f;
            constexpr float GAP  = 4.0f;
            float totalW = kHotbarSize * SLOT + (kHotbarSize - 1) * GAP;
            float startX = (io.DisplaySize.x - totalW) * 0.5f;
            float y      = io.DisplaySize.y - SLOT - 20.0f;

            ImDrawList* dl = ImGui::GetForegroundDrawList();
            for (int i = 0; i < kHotbarSize; ++i) {
                float x = startX + i * (SLOT + GAP);
                ImVec2 p0(x, y), p1(x + SLOT, y + SLOT);

                ImU32 bg = (i == m_hotbarIndex) ? IM_COL32(255, 255, 255, 200)
                                                : IM_COL32(0, 0, 0, 120);
                dl->AddRectFilled(p0, p1, bg);
                dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 0, 0, 2.0f);

                const auto& slot = m_hotbar[i];
                if (slot.isEmpty()) continue;

                // 方块名
                dl->AddText(ImVec2(x + 4, y + SLOT - 18),
                            IM_COL32(255, 255, 255, 255),
                            BlockName(slot.type));

                // 数量（右下角）
                if (slot.count > 1) {
                    char cnt[8];
                    std::snprintf(cnt, sizeof(cnt), "%d", slot.count);
                    dl->AddText(ImVec2(x + SLOT - 20, y + 4),
                                IM_COL32(255, 255, 255, 255), cnt);
                }
            }
        }

        // 调试面板
        if (m_showDebug) {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.5f);
            if (ImGui::Begin("Debug",  nullptr, 
                            ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoNav |
                            ImGuiWindowFlags_NoInputs |
                            ImGuiWindowFlags_NoMove)) {
                ImGui::SetWindowFontScale(1.5f);
                ImGui::Text("%.0f FPS  |  Frame: %.2f ms (avg %.2f, peak %.2f)",
                            m_fpsDisplay,
                            m_frameMsDisplay, m_frameMsAvgDisp, m_frameMsPeakDisp);
                ImGui::Text("Server tick: %.2f ms (avg %.2f, peak %.2f)",
                            m_serverTickMs, m_serverTickAvg, m_serverTickMax);
                ImGui::Text("Pos: %.2f, %.2f, %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
                ImGui::Text("Yaw/Pitch: %.1f / %.1f", yaw, pitch);
                ImGui::Separator();
                ImGui::Text("Chunks loaded: %zu", m_chunks.size());
                ImGui::Text("Chunks: %zu | pending: %zu", m_chunks.size(), m_pending.size());
                ImGui::Text("Render R: %d", m_renderDistance);
                ImGui::Text("Player ID: %d", m_myClientId);
                int hh = (int)m_timeOfDay;
                int mm = (int)((m_timeOfDay - hh) * 60);
                ImGui::Text("Time: %02d:%02d", hh, mm);
                ImGui::Separator();
                ImGui::Text("Backend: %s", m_deviceInfo.backend.c_str());
                ImGui::Text("GPU: %s", m_deviceInfo.deviceName.c_str());
                ImGui::Text("Vendor: %s", m_deviceInfo.vendor.c_str());
                ImGui::Text("API: %s", m_deviceInfo.apiVersion.c_str());
                ImGui::Text("Driver: %s", m_deviceInfo.driverVersion.c_str());
                ImGui::Text("Shader lang: %s", m_deviceInfo.shadingLanguage.c_str());
            }
            ImGui::End();
        }

        // ============ 主菜单 ============
        if (m_menuOpen) {
            ImGuiIO& io2 = ImGui::GetIO();
            ImGui::SetNextWindowPos(
                ImVec2(io2.DisplaySize.x * 0.5f, io2.DisplaySize.y * 0.5f),
                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));   // 居中，只第一次
            ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);

            if (ImGui::Begin("Menu", nullptr,
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove)) {
                ImGui::SetWindowFontScale(1.5f);

                if (ImGui::Button("Resume", ImVec2(-1, 44))) {
                    m_menuOpen = false;
                }

                ImGui::Separator();

                // ---- 窗口 ----
                auto& wcfg = mainWin->GetConfigs();
                if (ImGui::Checkbox("VSync", &wcfg.vsync)) {
                    m_applyPending = true;
                }

                bool fs = wcfg.fullscreen;
                if (ImGui::Checkbox("Fullscreen", &fs)) {
                    wcfg.fullscreen = fs;
                    mainWin->SetFullscreen(fs);
                    fbo = renderer->CreateFramebuffer(
                        mainWin->GetConfigs().windowPixelWidth,
                        mainWin->GetConfigs().windowPixelHeight);
                }

                bool ao = engine.GetConfig().render.m_aoStrength > 0.5f;
                if (ImGui::Checkbox("AO", &ao)) {
                    engine.GetConfig().render.m_aoStrength = ao ? 1.0f : 0.0f;
                }

                // ---- 相机 ----
                ImGui::SliderFloat("FOV", &engine.GetConfig().render.fov, 30.0f, 120.0f, "%.0f");

                ImGui::Separator();

                if (ImGui::Button("Apply", ImVec2(-1, 36))) {
                    m_applyPending = true;
                }
                if (ImGui::Button("Quit", ImVec2(-1, 44))) {
                    m_returnToMenuPending = true;
                }
            }
            ImGui::End();
        }
    }
}

void MyGame::ReturnToMainMenu() {
    if (renderer) renderer->WaitIdle();

    // 1. 断网 / 停服务器
    if (m_client) m_client.reset(); // 安全 reset
    if (m_server) {
        m_server->Stop();
        m_server.reset();
    }

    // 2. 先停 LOD worker
    StopLodWorker();

    // 3. 销毁 GPU 资源
    if (renderer) {
        for (auto& [key, cc] : m_chunks) {
            if (cc.mesh) renderer->DestroyMesh(cc.mesh);
        }
        for (auto& [type, mesh] : m_itemMeshCache) {
            if (mesh) renderer->DestroyMesh(mesh);
        }
        for (auto& [key, u] : m_lodUnits) {
            if (u.mesh != 0) renderer->DestroyMesh(u.mesh);
        }
        if (m_playerMesh != 0) { renderer->DestroyMesh(m_playerMesh); m_playerMesh = 0; }
    }
    m_chunks.clear();
    m_itemMeshCache.clear();
    m_pending.clear();
    while (!m_meshQueue.empty()) m_meshQueue.pop();
    m_itemEntities.clear();
    m_otherPlayers.clear();
    m_myClientId = 0;

    // --- LOD 状态清理 ---
    m_lodUnits.clear();
    m_lodPending.clear();
    {
        std::lock_guard lk(m_lodResultMutex);
        while (!m_lodResultQueue.empty()) m_lodResultQueue.pop();
    }
    {
        std::lock_guard lk(m_lodTaskMutex);
        while (!m_lodTaskQueue.empty()) m_lodTaskQueue.pop();
    }
    
    // ★ 新增：重置 LOD 游标与请求队列
    m_lodNeedQueue.clear();
    m_lodNeedCursor = 0;
    m_lastLodPcx = INT_MIN;
    m_lastLodPcz = INT_MIN;

    // 4. 最后释放 terrain
    m_lodTerrain.reset();

    // 5. 重置玩家 / 相机
    cameraPos   = glm::vec3(0.0f, 40.0f, 0.0f);
    cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    yaw   = -90.0f;
    pitch = 0.0f;
    m_playerVelocity   = glm::vec3(0.0f);
    m_positionError    = glm::vec3(0.0f);
    m_lastPlayerChunkX = INT_MIN;
    m_lastPlayerChunkZ = INT_MIN;

    m_menuOpen = false;
    m_state    = GameState::MainMenu;
    m_statusText.clear();

    logInfo(logger, "[Game] Returned to main menu");
}

void MyGame::OnShutdown(Eng::Engine& /*engine*/) {
    if (m_client) {
        m_client->Disconnect();
        m_client.reset();
    }
    if (m_server) { 
        m_server->Stop(); 
        m_server.reset(); 
    }

    // ★ 从 mainWin 重新获取 API，确保在 Window 存活时它才有效
    auto* safeRenderer = mainWin ? mainWin->GetAPI() : nullptr;
    if (safeRenderer) {
        StopLodWorker();
        for (auto& [key, u] : m_lodUnits) {
            if (u.mesh != 0) safeRenderer->DestroyMesh(u.mesh);
        }
        if (m_cubeShader) safeRenderer->DestroyShader(m_cubeShader);
        if (m_skyShader)  safeRenderer->DestroyShader(m_skyShader);
        if (m_lodShader)  safeRenderer->DestroyShader(m_lodShader);
        if (m_playerMesh != 0) { safeRenderer->DestroyMesh(m_playerMesh); m_playerMesh = 0; }
        safeRenderer->ShutdownImGuiBackend();
    }

    // 清理缓存，避免野指针/二次释放
    m_lodUnits.clear();
    m_lodPending.clear();
    m_lodTerrain.reset();
    renderer = nullptr;
    mainWin = nullptr; // 因为 WindowManager 可能已经销毁了它
}

bool MyGame::GetInput(float dt) {
    using namespace Eng::client;

    // ==================== ESC 开关菜单 ====================
    if (Input::IsKeyPressed(KeyCode::Escape)) {
        m_menuOpen = !m_menuOpen;
    }
    if (m_menuOpen) { // 菜单打开时冻结游戏输入
        return m_shouldQuit;
    }

    // ==================== F 切换飞行（调试） ====================
    if (Input::IsKeyPressed(KeyCode::F)) {
        m_flyMode = !m_flyMode;
        m_positionError = glm::vec3(0.0f);
        m_playerVelocity = glm::vec3(0.0f);
        logDebug(logger, "[Client] Fly mode " << (m_flyMode ? "ON" : "OFF"));
    }

    // ==================== F11 全屏切换 ====================
    if (Input::IsKeyPressed(KeyCode::F11)) {
        auto& configs = mainWin->GetConfigs();
        configs.fullscreen = !configs.fullscreen;
        mainWin->SetFullscreen(configs.fullscreen);
        renderer->DestroyFramebuffer(fbo);
        fbo = renderer->CreateFramebuffer(
            mainWin->GetConfigs().windowPixelWidth,
            mainWin->GetConfigs().windowPixelHeight);
    }

    // ==================== F3 调试面板 ====================
    if (Input::IsKeyPressed(KeyCode::F3)) {
        m_showDebug = !m_showDebug;
    }

    // ==================== 鼠标视角 ====================
    glm::vec2 mouseDelta = Input::GetMouseDelta();
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
        yaw   += mouseDelta.x * 0.1f;
        pitch -= mouseDelta.y * 0.1f;
        pitch = glm::clamp(pitch, -89.0f, 89.0f);
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
    if (m_flyMode) {
        if (Input::IsKeyDown(KeyCode::Space))  moveDir.y += 1.0f;
        if (Input::IsKeyDown(KeyCode::LShift)) moveDir.y -= 1.0f;
    }
    if (glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
    }

    game::net::PlayerInput input;
    input.moveDir = moveDir;
        m_lastMoveDir = moveDir;   // 供本地物理用
    input.look    = glm::vec2(glm::radians(yaw), glm::radians(pitch));
        if (Input::IsKeyDown(KeyCode::Space)) {
            m_jumpHoldTimer = 0.2f;   // 按下就续期
        }
        m_jumpHoldTimer = std::max(0.0f, m_jumpHoldTimer - dt);
    input.jump = (m_jumpHoldTimer > 0.0f);
        // 冷却递减
        m_placeCooldown = std::max(0.0f, m_placeCooldown - dt);
        m_digCooldown   = std::max(0.0f, m_digCooldown   - dt);

        // 按住鼠标 + 冷却完了 → 触发一次，重置冷却
        bool wantDig   = Input::IsKeyDown(KeyCode::MouseLeft);
        bool wantPlace = Input::IsKeyDown(KeyCode::MouseRight);

    input.dig   = (wantDig   && m_digCooldown   <= 0.0f);
    input.place = (wantPlace && m_placeCooldown <= 0.0f);

        if (input.dig)   m_digCooldown   = kDigInterval;
        if (input.place) m_placeCooldown = kPlaceInterval;

    // 滚轮切换物品
    float scroll = Input::GetScrollDelta();
    if (scroll > 0) m_hotbarIndex = (m_hotbarIndex - 1 + kHotbarSize) % kHotbarSize;
    if (scroll < 0) m_hotbarIndex = (m_hotbarIndex + 1) % kHotbarSize;
    // 数字键 1~4 直接选（kHotbarSize = 4）
    if (Input::IsKeyPressed(KeyCode::Num1)) m_hotbarIndex = 0;
    if (Input::IsKeyPressed(KeyCode::Num2)) m_hotbarIndex = 1;
    if (Input::IsKeyPressed(KeyCode::Num3)) m_hotbarIndex = 2;
    if (Input::IsKeyPressed(KeyCode::Num4)) m_hotbarIndex = 3;
    if (m_hotbarIndex < 0 || m_hotbarIndex >= kHotbarSize) {
        logError(logger, "[BUG] m_hotbarIndex out of range: " << m_hotbarIndex);
        m_hotbarIndex = 0;
    }
    auto& curSlot = m_hotbar[m_hotbarIndex];
    if (curSlot.isEmpty()) {
        input.placeBlock = 0;
    } else {
        input.placeBlock = static_cast<uint16_t>(curSlot.type);
        if (input.place) {
            curSlot.count--;
            if (curSlot.count <= 0) curSlot = game::ItemStack{};
        }
    }
    input.fly = m_flyMode;

    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::PlayerInput));
    w.WriteVec3(input.moveDir);   // ★ vec3
    w.WriteVec2(input.look);
    w.Write(static_cast<uint8_t>(input.jump  ? 1 : 0));
    w.Write(static_cast<uint8_t>(input.dig   ? 1 : 0));
    w.Write(static_cast<uint8_t>(input.place ? 1 : 0));
    w.Write<uint16_t>(input.placeBlock);
    w.Write(static_cast<uint8_t>(input.fly ? 1 : 0));
    m_client->Send(w.GetBuffer());
    // 本地预测
    // float speed = 5.0f * dt;
    // cameraPos += input.moveDir * speed;

    return m_shouldQuit;
}

void MyGame::HandleServerMessage(const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    auto type = static_cast<game::net::MessageType>(data[0]);
    Eng::MessageReader r(std::span<const uint8_t>(data).subspan(1));

    switch (type) {
        case game::net::MessageType::WorldState: {
            uint32_t count = r.ReadU32BE();

            m_serverTickMs = r.Read<float>();
            m_serverTickAvg = m_serverTickAvg * 0.95f + m_serverTickMs * 0.05f;
            if (m_serverTickMs > m_serverTickMax) m_serverTickMax = m_serverTickMs;

            for (uint32_t i = 0; i < count; ++i) {
                game::net::PlayerState ps;
                ps.id = r.ReadU32BE();

                uint8_t nlen = r.Read<uint8_t>();
                auto nbytes = r.ReadBytes(nlen);
                ps.name.assign(nbytes.begin(), nbytes.end());

                ps.position = r.ReadVec3();
                ps.yaw      = r.Read<float>();
                ps.pitch    = r.Read<float>();

                if (ps.id == m_myClientId) {
                    glm::vec3 diff = ps.position - cameraPos;
                    if (glm::length(diff) > 3.0f) {
                        cameraPos = ps.position;
                        m_playerVelocity = glm::vec3(0.0f);
                        m_positionError  = glm::vec3(0.0f);
                    } else {
                        m_positionError = diff;
                    }
                } else {
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
            auto bytes = r.ReadBytes(CHUNK_VOLUME);
            std::memcpy(cc.chunk.blocks.data(), bytes.data(), CHUNK_VOLUME);
            cc.mesh = 0;

            uint64_t key = ChunkKey(cx, cz);
            m_chunks[key] = cc;
            m_pending.erase(key);

            // 顺便算高度图，供 LOD1 用
            for (int lx = 0; lx < 16; ++lx) {
                for (int lz = 0; lz < 16; ++lz) {
                    int topY = 0;
                    uint8_t topB = 0;   // Air
                    for (int y = CHUNK_SIZE_Y - 1; y >= 0; --y) {
                        BlockType b = ChunkGet(cc.chunk, lx, y, lz);
                        if (b != BlockType::Air && b != BlockType::Water) {
                            topY = y;
                            topB = (uint8_t)b;
                            break;
                        }
                    }
                    cc.heights[lx * 16 + lz] = (int16_t)topY;
                    cc.topBlock[lx * 16 + lz] = topB;
                }
            }

            PendingMesh pm;
            pm.chunkKey = key;
            pm.chunk = m_chunks[key].chunk;
            m_meshQueue.push(pm);

            m_pending.erase(ChunkKey(cx, cz));
            break;
        }
        case game::net::MessageType::BlockChange: {
            int bx = r.Read<int32_t>();
            int by = r.Read<int32_t>();
            int bz = r.Read<int32_t>();
            BlockType t = static_cast<BlockType>(r.Read<uint16_t>());

            int cx = bx >> 4, cz = bz >> 4;
            int lx = bx & 15, lz = bz & 15;

            auto it = m_chunks.find(ChunkKey(cx, cz));
            if (it == m_chunks.end()) break;

            ChunkSet(it->second.chunk, lx, by, lz, t);

            // 重建 mesh
            if (it->second.mesh != 0) {
                renderer->DestroyMesh(it->second.mesh);
            }
            it->second.mesh = BuildChunkMesh(it->second.chunk);

            // 如果改的是边界方块，邻居区块的 mesh 也得重建
            if (lx == 0 || lx == 15 || lz == 0 || lz == 15) {
                auto rebuild = [&](int ncx, int ncz) {
                    auto nit = m_chunks.find(ChunkKey(ncx, ncz));
                    if (nit == m_chunks.end()) return;
                    if (nit->second.mesh != 0) renderer->DestroyMesh(nit->second.mesh);
                    nit->second.mesh = BuildChunkMesh(nit->second.chunk);
                };
                if (lx == 0)  rebuild(cx - 1, cz);
                if (lx == 15) rebuild(cx + 1, cz);
                if (lz == 0)  rebuild(cx, cz - 1);
                if (lz == 15) rebuild(cx, cz + 1);
            }
            break;
        }
        case game::net::MessageType::EntitySpawn: {
            uint32_t  id   = r.Read<uint32_t>();
            glm::vec3 pos  = r.ReadVec3();
            BlockType blockType = static_cast<BlockType>(r.Read<uint16_t>());

            ClientItemEntity e;
            e.id        = id;
            e.itemType  = blockType;
            e.renderPos = pos;
            e.prevPos   = pos;
            e.targetPos = pos;
            e.interpT   = 1.0f;
            m_itemEntities[id] = e;
            break;
        }

        case game::net::MessageType::EntityUpdate: {
            uint32_t  id  = r.Read<uint32_t>();
            glm::vec3 pos = r.ReadVec3();

            auto it = m_itemEntities.find(id);
            if (it != m_itemEntities.end()) {
                // ★ 关键：起点 = 当前渲染位置（不是上次目标位置）
                it->second.prevPos   = it->second.renderPos;
                it->second.targetPos = pos;
                it->second.interpT   = 0.0f;   // 从 0 开始插值
            }
            break;
        }
        case game::net::MessageType::EntityDestroy: {
            uint32_t id       = r.Read<uint32_t>();
            uint8_t  reason   = r.Read<uint8_t>();
            uint32_t pickerId = r.ReadU32BE();

            auto it = m_itemEntities.find(id);
            if (it != m_itemEntities.end()) {
                if (reason == 1) {
                    it->second.pickingUp   = true;
                    it->second.pickupT     = 0.0f;
                    it->second.pickupStart = it->second.renderPos;
                    it->second.pickerId    = pickerId;
                } else {
                    m_itemEntities.erase(it);
                }
            }
            break;
        }
        case game::net::MessageType::ServerInfo: {
            uint32_t seed = r.ReadU32BE();
            glm::vec3 spawn = r.ReadVec3();
            m_lodTerrain = std::make_unique<game::server::TerrainGenerator>(seed);
            StartLodWorker(seed);
            logInfo(logger, "[LOD] Received world seed: " << seed
                    << " spawn: " << spawn.x << "," << spawn.y << "," << spawn.z);
            break;
        }
        case game::net::MessageType::PlayerInput:
        case game::net::MessageType::ChunkRequest:
        case game::net::MessageType::ClientHello:
            // 客户端不处理
            break;
    }
}

Eng::client::MeshHandle MyGame::GetOrCreateItemMesh(BlockType type) {
    auto it = m_itemMeshCache.find(type);
    if (it != m_itemMeshCache.end()) return it->second;

    auto mesh = BuildItemCubeMesh(type);
    m_itemMeshCache[type] = mesh;
    return mesh;
}

Eng::client::MeshHandle MyGame::BuildItemCubeMesh(BlockType type) {
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t> indices;

    static constexpr float faceOffsets[6][4][3] = {
        // +X
        {{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f}},
        // -X
        {{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},{-0.5f,-0.5f,-0.5f}},
        // +Y
        {{-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f}},
        // -Y
        {{-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}},
        // +Z
        {{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}},
        // -Z
        {{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f}},
    };
    static constexpr int faceNormals[6][3] = {
        { 1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
    };
    static constexpr float faceUVs[4][2] = {
        {0,0},{0,1},{1,1},{1,0}
    };

    for (int f = 0; f < 6; ++f) {
        int tile = TileForBlock(type, f);
        int tileRow = tile / ATLAS_COLS;
        int tileCol = tile % ATLAS_COLS;
        float u0 = tileCol * TILE_UV;
        float v0 = tileRow * TILE_UV;

        uint32_t baseIndex = (uint32_t)vertices.size();
        for (int v = 0; v < 4; ++v) {
            Eng::client::Vertex vert;
            vert.position = {
                (float)faceOffsets[f][v][0],
                (float)faceOffsets[f][v][1],
                (float)faceOffsets[f][v][2]
            };
            vert.normal = {
                (float)faceNormals[f][0],
                (float)faceNormals[f][1],
                (float)faceNormals[f][2]
            };
            vert.uv = {
                u0 + faceUVs[v][0] * TILE_UV,
                v0 + (1.0f - faceUVs[v][1]) * TILE_UV
            };
            vert.ao = 1.0f;
            vertices.push_back(vert);
        }
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 3);
        indices.push_back(baseIndex + 0);
    }

    Eng::client::MeshData data;
    data.vertices = vertices;
    data.indices  = indices;
    return renderer->CreateMesh(data);
}

Eng::client::MeshHandle MyGame::BuildChunkMesh(const Chunk& chunk) {
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t> indices;

    const int cx = chunk.chunk_x;
    const int cz = chunk.chunk_z;
    const int baseX = cx * CHUNK_SIZE_X;
    const int baseZ = cz * CHUNK_SIZE_Z;

    // 拿 4 个邻居（可能为 null——还没收到）
    auto findChunk = [this](int ccx, int ccz) -> const Chunk* {
        auto it = m_chunks.find(ChunkKey(ccx, ccz));
        if (it == m_chunks.end()) return nullptr;
        return &it->second.chunk;
    };
    const Chunk* nXm = findChunk(cx - 1, cz);
    const Chunk* nXp = findChunk(cx + 1, cz);
    const Chunk* nZm = findChunk(cx, cz - 1);
    const Chunk* nZp = findChunk(cx, cz + 1);

    // 跨区块取方块，接受 lx/lz ∈ [-1, 16]
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

    // ★ 计算 AO 专用（排除水、草、花，确保它们不投射环境光遮蔽）
    auto isSolidAt = [&](int lx, int ly, int lz) -> bool {
        BlockType b = getBlock(lx, ly, lz);
        return b != BlockType::Air && b != BlockType::Water && b != BlockType::Void &&
               b != BlockType::TallGrass && b != BlockType::FlowerRed && b != BlockType::FlowerYellow;
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

                // 1. 跳过纯空气和虚空
                if (type == BlockType::Air || type == BlockType::Void) continue;

                // 2. 十字交叉网格（草丛/花）
                if (type == BlockType::TallGrass || type == BlockType::FlowerRed || type == BlockType::FlowerYellow) {
                    int tile = TileForBlock(type, 0);
                    float u0 = (tile % ATLAS_COLS) * TILE_UV;
                    float v0 = (tile / ATLAS_COLS) * TILE_UV;
                    float u1 = u0 + TILE_UV;
                    float v1 = v0 + TILE_UV;
                    
                    float x0 = baseX + lx + 0.15f;
                    float x1 = baseX + lx + 0.85f;
                    float z0 = baseZ + lz + 0.15f;
                    float z1 = baseZ + lz + 0.85f;
                    float y0 = (float)ly;
                    float y1 = (float)ly + 1.0f;

                    auto addQuad = [&](glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, glm::vec3 p4) {
                        uint32_t baseIndex = (uint32_t)vertices.size();
                        Eng::client::Vertex v;
                        v.ao = 1.0f; // 植物不吃 AO
                        v.normal = {0, 1, 0};

                        v.position = p1; v.uv = {u0, v1}; vertices.push_back(v);
                        v.position = p2; v.uv = {u1, v1}; vertices.push_back(v);
                        v.position = p3; v.uv = {u1, v0}; vertices.push_back(v);
                        v.position = p4; v.uv = {u0, v0}; vertices.push_back(v);

                        // 正面
                        indices.push_back(baseIndex + 0); indices.push_back(baseIndex + 1); indices.push_back(baseIndex + 2);
                        indices.push_back(baseIndex + 2); indices.push_back(baseIndex + 3); indices.push_back(baseIndex + 0);
                        // 反面 (让背面也能看到)
                        indices.push_back(baseIndex + 0); indices.push_back(baseIndex + 2); indices.push_back(baseIndex + 1);
                        indices.push_back(baseIndex + 3); indices.push_back(baseIndex + 2); indices.push_back(baseIndex + 0);
                    };

                    // 平面 1
                    addQuad({x0, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z0});
                    // 平面 2
                    addQuad({x0, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z1});

                    continue; 
                }

                // 3. 处理实心方块和水
                for (int f = 0; f < 6; ++f) {
                    int nx = lx + faceNormals[f][0];
                    int ny = ly + faceNormals[f][1];
                    int nz = lz + faceNormals[f][2];

                    BlockType neighbor = getBlock(nx, ny, nz);
                    
                    // 如果邻居是实心方块，遮挡面
                    if (IsSolid(neighbor)) continue;
                    // 如果邻居同为水，不画内部面
                    if (type == BlockType::Water && neighbor == BlockType::Water) continue;

                    int N_axis = (faceNormals[f][0] != 0) ? 0
                               : (faceNormals[f][1] != 0) ? 1 : 2;
                    int U_axis = (N_axis + 1) % 3;
                    int V_axis = (N_axis + 2) % 3;

                    glm::ivec3 N(faceNormals[f][0], faceNormals[f][1], faceNormals[f][2]);

                    uint32_t baseIndex = (uint32_t)vertices.size();

                    for (int v = 0; v < 4; ++v) {
                        Eng::client::Vertex vert;
                        
                        // 让水面稍微低一点（0.9 格高）
                        float yOffset = (type == BlockType::Water && f == 2 && neighbor == BlockType::Air) ? 0.9f : 0.0f;
                        float yPos = (float)ly + faceOffsets[f][v][1];
                        if (type == BlockType::Water && faceOffsets[f][v][1] == 1) {
                            yPos = (float)ly + yOffset;
                        }

                        vert.position = {
                            baseX + lx + faceOffsets[f][v][0],
                            yPos,
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

                        // AO 计算
                        int du = (faceOffsets[f][v][U_axis] == 0) ? -1 : +1;
                        int dv = (faceOffsets[f][v][V_axis] == 0) ? -1 : +1;

                        glm::ivec3 U(0), V(0);
                        U[U_axis] = du;
                        V[V_axis] = dv;

                        int s1 = isSolidAt(lx + N.x + U.x, ly + N.y + U.y, lz + N.z + U.z) ? 1 : 0;
                        int s2 = isSolidAt(lx + N.x + V.x, ly + N.y + V.y, lz + N.z + V.z) ? 1 : 0;
                        int sc = isSolidAt(lx + N.x + U.x + V.x, ly + N.y + U.y + V.y, lz + N.z + U.z + V.z) ? 1 : 0;

                        vert.ao = (s1 && s2) ? 0.4f : 1.0f - 0.2f * (s1 + s2 + sc);
                        
                        // 水强制不吃 AO
                        if (type == BlockType::Water) vert.ao = 1.0f;

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

    return handle;
}

Eng::client::MeshHandle MyGame::BuildPlayerMesh() {
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t> indices;

    // 一个 cuboid：中心 (cx,cy,cz)，尺寸 (sx,sy,sz)，tile = tile 索引
    auto addBox = [&](glm::vec3 c, glm::vec3 size, int tile) {
        const float x0 = c.x - size.x*0.5f, x1 = c.x + size.x*0.5f;
        const float y0 = c.y - size.y*0.5f, y1 = c.y + size.y*0.5f;
        const float z0 = c.z - size.z*0.5f, z1 = c.z + size.z*0.5f;

        int tileRow = tile / ATLAS_COLS;
        int tileCol = tile % ATLAS_COLS;
        float u0 = tileCol * TILE_UV;
        float v0 = tileRow * TILE_UV;

        // 6 面：+X -X +Y -Y +Z -Z
        const glm::vec3 corners[8] = {
            {x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},   // 底 0-3
            {x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1},   // 顶 4-7
        };
        const int faces[6][4] = {
            {1,5,6,2},   // +X ✓
            {3,7,4,0},   // -X（原来 {0,4,7,3} 反的）
            {7,6,5,4},   // +Y（原来 {4,5,6,7} 反的）
            {0,1,2,3},   // -Y ✓
            {3,2,6,7},   // +Z ✓
            {0,4,5,1},   // -Z（原来 {0,1,5,4} 反的）
        };
        const glm::vec3 normals[6] = {
            {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
        };

        static constexpr float fuv[4][2] = {{0,0},{1,0},{1,1},{0,1}};

        for (int f = 0; f < 6; ++f) {
            uint32_t base = (uint32_t)vertices.size();
            for (int k = 0; k < 4; ++k) {
                Eng::client::Vertex v;
                v.position = corners[faces[f][k]];
                v.normal   = normals[f];
                v.uv = { u0 + fuv[k][0]*TILE_UV,
                         v0 + (1.0f - fuv[k][1])*TILE_UV };
                v.ao = 1.0f;
                vertices.push_back(v);
            }
            indices.push_back(base+0); indices.push_back(base+1); indices.push_back(base+2);
            indices.push_back(base+2); indices.push_back(base+3); indices.push_back(base+0);
        }
    };

    // 头 / 身体 / 双臂 / 双腿
    addBox({0.0f, 1.55f, 0.0f}, {0.5f, 0.5f, 0.5f}, 13);   // 头（skin）
    addBox({0.0f, 1.05f, 0.0f}, {0.5f, 0.5f, 0.25f}, 14);  // 身体（shirt）
    addBox({-0.375f, 1.05f, 0.0f}, {0.25f, 0.5f, 0.25f}, 14);  // 左臂
    addBox({ 0.375f, 1.05f, 0.0f}, {0.25f, 0.5f, 0.25f}, 14);  // 右臂
    addBox({-0.125f, 0.4f, 0.0f}, {0.25f, 0.8f, 0.25f}, 15);   // 左腿
    addBox({ 0.125f, 0.4f, 0.0f}, {0.25f, 0.8f, 0.25f}, 15);   // 右腿

    Eng::client::MeshData data;
    data.vertices = std::move(vertices);
    data.indices  = std::move(indices);
    return renderer->CreateMesh(data);
}

void MyGame::UpdateChunkStreaming() {
    int pcx = static_cast<int>(std::floor(cameraPos.x)) >> 4;
    int pcz = static_cast<int>(std::floor(cameraPos.z)) >> 4;

    const int R = m_renderDistance;

    // 卸载超过 R+2 的区块
    const int unloadR = m_renderDistance + 2;

    for (auto it = m_chunks.begin(); it != m_chunks.end(); ) {
        uint64_t key = it->first;
        int cx = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFFu));
        int cz = static_cast<int>(static_cast<int32_t>(key >> 32));
        int dx = cx - pcx;
        int dz = cz - pcz;

        if (std::abs(dx) > unloadR || std::abs(dz) > unloadR) {
            if (it->second.mesh != 0) {
                renderer->DestroyMesh(it->second.mesh);
            }
            m_pending.erase(key);
            it = m_chunks.erase(it);
        } else {
            ++it;
        }
    }

    // 收集缺失的区块，按距离排序
    struct Need { int cx, cz, d2; };
    std::vector<Need> need;
    for (int dz = -R; dz <= R; ++dz) {
        for (int dx = -R; dx <= R; ++dx) {
            int cx = pcx + dx;
            int cz = pcz + dz;
            uint64_t key = ChunkKey(cx, cz);
            if (m_chunks.count(key)) continue;
            if (m_pending.count(key)) continue;
            need.push_back({cx, cz, dx*dx + dz*dz});
        }
    }

    // 近的优先
    std::sort(need.begin(), need.end(),
              [](const Need& a, const Need& b) { return a.d2 < b.d2; });

    // 只发前 K 个
    int sent = 0;
    for (const auto& n : need) {
        if (sent++ >= kRequestsPerTick) break;

        Eng::MessageWriter w;
        w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkRequest));
        w.Write<int32_t>(n.cx);
        w.Write<int32_t>(n.cz);
        m_client->Send(w.GetBuffer());

        m_pending.insert(ChunkKey(n.cx, n.cz));
    }
}

BlockType MyGame::GetBlockAt(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return BlockType::Air;
    int cx = wx >> 4;
    int cz = wz >> 4;
    int lx = wx & 15;
    int lz = wz & 15;
    auto it = m_chunks.find(ChunkKey(cx, cz));
    if (it == m_chunks.end()) return BlockType::Air;
    return ChunkGet(it->second.chunk, lx, wy, lz);
}

bool MyGame::IsSolidAt(int wx, int wy, int wz) const {
    BlockType b = GetBlockAt(wx, wy, wz);
    return b != BlockType::Air && b != BlockType::Water;
}

bool MyGame::AABBCollides(const glm::vec3& pos) const {
    int minX = (int)std::floor(pos.x - PLAYER_HALF_W);
    int maxX = (int)std::floor(pos.x + PLAYER_HALF_W);
    int minY = (int)std::floor(pos.y);
    int maxY = (int)std::floor(pos.y + PLAYER_HEIGHT);
    int minZ = (int)std::floor(pos.z - PLAYER_HALF_W);
    int maxZ = (int)std::floor(pos.z + PLAYER_HALF_W);

    for (int x = minX; x <= maxX; ++x)
        for (int y = minY; y <= maxY; ++y)
            for (int z = minZ; z <= maxZ; ++z)
                if (IsSolidAt(x, y, z)) return true;
    return false;
}

void MyGame::AddToInventory(BlockType type, int count) {
    if (type == BlockType::Air || count <= 0) return;

    // 1. 已有同类型 → 累加
    for (auto& slot : m_hotbar) {
        if (slot.type == type) {
            slot.count += count;
            return;
        }
    }

    // 2. 空 slot → 新建
    for (auto& slot : m_hotbar) {
        if (slot.isEmpty()) {
            slot.type  = type;
            slot.count = count;
            return;
        }
    }

    // 3. 背包满 → 丢弃（暂时）
}

// ============================================================
// LOD worker
// ============================================================

void MyGame::StartLodWorker(uint32_t seed) {
    unsigned hw = std::thread::hardware_concurrency();
    unsigned n  = std::clamp(hw > 2 ? hw - 2 : 2u, 2u, 6u);   // 别太多，主线程也要跑

    m_lodWorkerStop = false;
    for (unsigned i = 0; i < n; ++i) {
        m_lodWorkers.emplace_back([this, seed] { LodWorkerLoop(seed); });
    }
    logInfo(logger, "[LOD] Started " << n << " workers");
}

void MyGame::StopLodWorker() {
    if (m_lodWorkers.empty()) return;

    {
        std::lock_guard lk(m_lodTaskMutex);
        m_lodWorkerStop = true;
        while (!m_lodTaskQueue.empty()) m_lodTaskQueue.pop();   // 清空，别让 worker 还跑
    }
    m_lodTaskCv.notify_all();
    for (auto& t : m_lodWorkers) {
        if (t.joinable()) t.join();
    }
    m_lodWorkers.clear();
}

void MyGame::LodWorkerLoop(uint32_t seed) {
    game::server::TerrainGenerator terrain(seed);

    while (true) {
        LodTask task;
        {
            std::unique_lock lk(m_lodTaskMutex);
            m_lodTaskCv.wait(lk, [this] {
                return m_lodWorkerStop.load() || !m_lodTaskQueue.empty();
            });
            if (m_lodWorkerStop.load()) return;
            task = m_lodTaskQueue.front();
            m_lodTaskQueue.pop();
        }

        LodResult r;
        r.level = task.level;
        r.cx = task.cx;
        r.cz = task.cz;
        BuildLodUnit(terrain, task.level, task.cx, task.cz,
                     r.vertices, r.indices, r.aabbMin, r.aabbMax);

        {
            std::lock_guard lk(m_lodResultMutex);
            m_lodResultQueue.push(std::move(r));
        }
    }
}

// ============================================================
// 构建一个 LOD 单元：33×33 顶点网格，每格一个水平 quad
// ============================================================
void MyGame::BuildLodUnit(const game::server::TerrainGenerator& terrain,
                          int level, int ucx, int ucz,
                          std::vector<Eng::client::Vertex>& outV,
                          std::vector<uint32_t>& outI,
                          glm::vec3& outMin, glm::vec3& outMax) const {
    const int sampleStep = 1 << level;   // 从 1<<(level-1) → 1<<level，即 2/4/8
    const int unitSize   = kLodUnitSize * sampleStep; // 32, 64, 128
    constexpr int N  = kLodUnitSize;                  // 32 格子
    constexpr int NP = N + 1;

    const int baseX = ucx * unitSize;
    const int baseZ = ucz * unitSize;

    outV.clear(); outI.clear();
    outV.reserve(N * N * 12);
    outI.reserve(N * N * 18);
    outMin = glm::vec3(1e30f);
    outMax = glm::vec3(-1e30f);

    auto tileForBiome = [](game::server::Biome bb) -> int {
        using B = game::server::Biome;
        switch (bb) {
            case B::Ocean:    return 5;
            case B::Beach:
            case B::Desert:   return 4;
            case B::Snow:     return 6;
            case B::Mountain: return 0;
            case B::Forest:
            case B::Plains:
            default:          return 1;
        }
    };

    static thread_local int     hP[NP][NP];
    static thread_local uint8_t bP[NP][NP];
    for (int j = 0; j < NP; ++j)
        for (int i = 0; i < NP; ++i) {
            int wx = baseX + i * sampleStep;
            int wz = baseZ + j * sampleStep;
            hP[j][i] = terrain.GetSurfaceY(wx, wz);
            bP[j][i] = (uint8_t)terrain.GetBiome(wx, wz);
        }

    auto cellY = [&](int i, int j) -> float {
        return (float)std::max(hP[j][i] + 1, SEA_LEVEL);
    };
    auto cellTile = [&](int i, int j) -> int {
        int h = hP[j][i];
        auto b = (game::server::Biome)bP[j][i];
        if (h < SEA_LEVEL) b = game::server::Biome::Ocean;
        return tileForBiome(b);
    };
    auto pushV = [&](const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv) {
        Eng::client::Vertex v;
        v.position = p; v.normal = n; v.uv = uv; v.ao = 1.0f;
        outV.push_back(v);
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    };

    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            float ySelf = cellY(i, j);
            int   tile  = cellTile(i, j);
            float u = ((tile % ATLAS_COLS) + 0.5f) * TILE_UV;
            float v = ((tile / ATLAS_COLS) + 0.5f) * TILE_UV;

            float x0 = (float)(baseX + i * sampleStep);
            float x1 = (float)(baseX + (i + 1) * sampleStep);
            float z0 = (float)(baseZ + j * sampleStep);
            float z1 = (float)(baseZ + (j + 1) * sampleStep);

            // ---- 顶面 ----
            {
                uint32_t base = (uint32_t)outV.size();
                pushV({x0, ySelf, z0}, {0,1,0}, {u,v});
                pushV({x1, ySelf, z0}, {0,1,0}, {u,v});
                pushV({x1, ySelf, z1}, {0,1,0}, {u,v});
                pushV({x0, ySelf, z1}, {0,1,0}, {u,v});
                outI.push_back(base+0); outI.push_back(base+3); outI.push_back(base+2);
                outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+1);
            }

            // ---- 右壁 +X ----
            if (i + 1 < N) {
                float yNb = cellY(i + 1, j);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i + 1, j);
                    float nu = ((nt % ATLAS_COLS) + 0.5f) * TILE_UV;
                    float nv = ((nt / ATLAS_COLS) + 0.5f) * TILE_UV;
                    uint32_t base = (uint32_t)outV.size();
                    pushV({x1, yNb,   z0}, {1,0,0}, {nu,nv});
                    pushV({x1, ySelf, z0}, {1,0,0}, {nu,nv});
                    pushV({x1, ySelf, z1}, {1,0,0}, {nu,nv});
                    pushV({x1, yNb,   z1}, {1,0,0}, {nu,nv});
                    outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                }
            }

            // ---- 上壁 +Z ----
            if (j + 1 < N) {
                float yNb = cellY(i, j + 1);
                if (ySelf > yNb + 0.01f) {
                    int nt = cellTile(i, j + 1);
                    float nu = ((nt % ATLAS_COLS) + 0.5f) * TILE_UV;
                    float nv = ((nt / ATLAS_COLS) + 0.5f) * TILE_UV;
                    uint32_t base = (uint32_t)outV.size();
                    pushV({x0, yNb,   z1}, {0,0,1}, {nu,nv});
                    pushV({x1, yNb,   z1}, {0,0,1}, {nu,nv});
                    pushV({x1, ySelf, z1}, {0,0,1}, {nu,nv});
                    pushV({x0, ySelf, z1}, {0,0,1}, {nu,nv});
                    outI.push_back(base+0); outI.push_back(base+1); outI.push_back(base+2);
                    outI.push_back(base+0); outI.push_back(base+2); outI.push_back(base+3);
                }
            }
        }
    }
}

void MyGame::BuildLod1FromChunks(const LodKey& key) {
    constexpr int CELLS = 32;
    const int baseX = key.cx * CELLS;
    const int baseZ = key.cz * CELLS;

    auto getChunk = [this](int cx, int cz) -> const ClientChunk* {
        auto it = m_chunks.find(ChunkKey(cx, cz));
        return it == m_chunks.end() ? nullptr : &it->second;
    };
    const ClientChunk* cs[2][2] = {
        { getChunk(key.cx*2+0, key.cz*2+0), getChunk(key.cx*2+0, key.cz*2+1) },
        { getChunk(key.cx*2+1, key.cz*2+0), getChunk(key.cx*2+1, key.cz*2+1) },
    };

    std::vector<Eng::client::Vertex> outV;
    std::vector<uint32_t> outI;
    outV.reserve(CELLS * CELLS * 12);
    outI.reserve(CELLS * CELLS * 18);
    glm::vec3 aabbMin(1e30f), aabbMax(-1e30f);

    auto blockToTile = [](uint8_t b) -> int {
        using BT = BlockType;
        switch ((BT)b) {
            case BT::Stone:      return 0;
            case BT::GrassBlock: return 1;   // grass_top
            case BT::Dirt:       return 3;
            case BT::Sand:       return 4;
            case BT::Water:      return 5;
            case BT::Snow:       return 6;
            case BT::Sandstone:  return 8;
            case BT::CoalOre:    return 9;
            case BT::IronOre:    return 10;
            case BT::Wood:       return 11;
            case BT::Leaves:     return 12;
            default:             return 1;
        }
    };

    // 采样（含 LOD1 外边界兜底 terrain）
    auto sample = [&](int i, int j, float& y, int& tile) {
        int cxi = i >> 4, czi = j >> 4;
        int lx  = i & 15, lz  = j & 15;
        const ClientChunk* cc = cs[cxi][czi];

        int h;
        uint8_t tb;
        if (cc) {
            h  = cc->heights[lx * 16 + lz];
            tb = cc->topBlock[lx * 16 + lz];
        } else {
            int wx = baseX + i, wz = baseZ + j;
            h = m_lodTerrain ? m_lodTerrain->GetSurfaceY(wx, wz) : SEA_LEVEL;
            auto b = m_lodTerrain ? m_lodTerrain->GetBiome(wx, wz)
                                  : game::server::Biome::Plains;
            using B = game::server::Biome;
            switch (b) {
                case B::Ocean:    tb = (uint8_t)BlockType::Water;      break;
                case B::Beach:
                case B::Desert:   tb = (uint8_t)BlockType::Sand;       break;
                case B::Snow:     tb = (uint8_t)BlockType::Snow;       break;
                case B::Mountain: tb = (uint8_t)BlockType::Stone;      break;
                default:          tb = (uint8_t)BlockType::GrassBlock; break;
            }
        }

        y = std::max<float>((float)(h + 1), (float)SEA_LEVEL);
        tile = blockToTile(tb);
    };

    // 高度矩阵（32×32）
    static thread_local float hM[CELLS][CELLS];
    static thread_local int   tM[CELLS][CELLS];
    for (int j = 0; j < CELLS; ++j)
        for (int i = 0; i < CELLS; ++i)
            sample(i, j, hM[j][i], tM[j][i]);

    auto pushV = [&](glm::vec3 p, glm::vec3 n, glm::vec2 uv) {
        Eng::client::Vertex v;
        v.position = p; v.normal = n; v.uv = uv; v.ao = 1.0f;
        outV.push_back(v);
        aabbMin = glm::min(aabbMin, p);
        aabbMax = glm::max(aabbMax, p);
    };

    auto tileUV = [&](int t, float& u, float& v) {
        u = ((t % ATLAS_COLS) + 0.5f) * TILE_UV;
        v = ((t / ATLAS_COLS) + 0.5f) * TILE_UV;
    };

    for (int j = 0; j < CELLS; ++j) {
        for (int i = 0; i < CELLS; ++i) {
            float ySelf = hM[j][i];
            float u, v; tileUV(tM[j][i], u, v);

            float x0 = (float)(baseX + i);
            float x1 = x0 + 1.0f;
            float z0 = (float)(baseZ + j);
            float z1 = z0 + 1.0f;

            // 顶面
            {
                uint32_t b = (uint32_t)outV.size();
                pushV({x0, ySelf, z0}, {0,1,0}, {u,v});
                pushV({x1, ySelf, z0}, {0,1,0}, {u,v});
                pushV({x1, ySelf, z1}, {0,1,0}, {u,v});
                pushV({x0, ySelf, z1}, {0,1,0}, {u,v});
                outI.push_back(b+0); outI.push_back(b+3); outI.push_back(b+2);
                outI.push_back(b+0); outI.push_back(b+2); outI.push_back(b+1);
            }

            // 右壁
            if (i + 1 < CELLS) {
                float yNb = hM[j][i+1];
                if (ySelf > yNb + 0.01f) {
                    float nu, nv; tileUV(tM[j][i+1], nu, nv);
                    uint32_t b = (uint32_t)outV.size();
                    pushV({x1, yNb,   z0}, {1,0,0}, {nu,nv});
                    pushV({x1, ySelf, z0}, {1,0,0}, {nu,nv});
                    pushV({x1, ySelf, z1}, {1,0,0}, {nu,nv});
                    pushV({x1, yNb,   z1}, {1,0,0}, {nu,nv});
                    outI.push_back(b+0); outI.push_back(b+1); outI.push_back(b+2);
                    outI.push_back(b+0); outI.push_back(b+2); outI.push_back(b+3);
                }
            }
            // 上壁
            if (j + 1 < CELLS) {
                float yNb = hM[j+1][i];
                if (ySelf > yNb + 0.01f) {
                    float nu, nv; tileUV(tM[j+1][i], nu, nv);
                    uint32_t b = (uint32_t)outV.size();
                    pushV({x0, yNb,   z1}, {0,0,1}, {nu,nv});
                    pushV({x1, yNb,   z1}, {0,0,1}, {nu,nv});
                    pushV({x1, ySelf, z1}, {0,0,1}, {nu,nv});
                    pushV({x0, ySelf, z1}, {0,0,1}, {nu,nv});
                    outI.push_back(b+0); outI.push_back(b+1); outI.push_back(b+2);
                    outI.push_back(b+0); outI.push_back(b+2); outI.push_back(b+3);
                }
            }
        }
    }

    if (outV.empty()) return;

    Eng::client::MeshData data;
    data.vertices = std::move(outV);
    data.indices  = std::move(outI);
    auto mesh = renderer->CreateMesh(data);

    LodUnit u;
    u.level = 1;
    u.cx = key.cx; u.cz = key.cz;
    u.mesh = mesh;
    u.aabbMin = aabbMin; u.aabbMax = aabbMax;
    m_lodUnits[key] = u;
}

// ============================================================
// 每帧：根据相机位置更新 LOD 单元
// ============================================================
void MyGame::UpdateLodStreaming() {
    if (!m_lodTerrain) return;

    int pcx = static_cast<int>(std::floor(cameraPos.x)) >> 4;
    int pcz = static_cast<int>(std::floor(cameraPos.z)) >> 4;

    // ---- 只在跨 chunk 时重算 wanted / 卸载 / 重建 need 队列 ----
    if (pcx != m_lastLodPcx || pcz != m_lastLodPcz) {
        m_lastLodPcx = pcx;
        m_lastLodPcz = pcz;

        std::unordered_set<LodKey, LodKeyHash> wanted;
        struct Need { LodKey k; int d2; };
        std::vector<Need> need;

        for (int level = 1; level <= kLodLevels; ++level) {
            int unitChunks = 1 << level;
            constexpr int kRingInnerUnits = 8;
            constexpr int kRingOuterUnits = 16;
            int pu = pcx / unitChunks;
            int pv = pcz / unitChunks;

            for (int dv = -kRingOuterUnits; dv <= kRingOuterUnits; ++dv) {
                for (int du = -kRingOuterUnits; du <= kRingOuterUnits; ++du) {
                    int adu = std::abs(du), adv = std::abs(dv);
                    if (adu < kRingInnerUnits && adv < kRingInnerUnits) continue;
                    LodKey k{ (int8_t)level, pu + du, pv + dv };
                    wanted.insert(k);
                    if (m_lodUnits.count(k))    continue;
                    if (m_lodPending.count(k))  continue;
                    need.push_back({ k, adu*adu + adv*adv });
                }
            }
        }

        // 卸载
        for (auto it = m_lodUnits.begin(); it != m_lodUnits.end(); ) {
            if (!wanted.count(it->first)) {
                if (it->second.mesh != 0) renderer->DestroyMesh(it->second.mesh);
                it = m_lodUnits.erase(it);
            } else ++it;
        }

        std::sort(need.begin(), need.end(),
                  [](const Need& a, const Need& b){ return a.d2 < b.d2; });

        m_lodNeedQueue.clear();
        m_lodNeedQueue.reserve(need.size());
        for (auto& n : need) m_lodNeedQueue.push_back(n.k);
        m_lodNeedCursor = 0;
    }

    // 玩家静止时长
    const float moveDist = glm::length(cameraPos - m_lastCameraPos);
    if (moveDist < 0.02f) m_stationaryTime += 0.016f;   // 近似帧时间
    else                  m_stationaryTime = 0.0f;
    m_lastCameraPos = cameraPos;

    const bool stationary = (m_stationaryTime > 1.0f);   // 停稳 1 秒后才派 L2/L3

    constexpr int K_PER_FRAME = 6;
    int sent = 0;
    while (m_lodNeedCursor < m_lodNeedQueue.size() && sent < K_PER_FRAME) {
        LodKey k = m_lodNeedQueue[m_lodNeedCursor];
        if (m_lodUnits.count(k) || m_lodPending.count(k)) { ++m_lodNeedCursor; continue; }

        // ★ 移动中跳过 L2/L3 任务（但不推进游标——下次静止时从头扫）
        if (k.level > 1 && !stationary) {
            ++m_lodNeedCursor;
            continue;
        }

        m_lodPending.insert(k);
        {
            std::lock_guard lk(m_lodTaskMutex);
            m_lodTaskQueue.push({ k.level, k.cx, k.cz });
        }
        m_lodTaskCv.notify_one();

        ++m_lodNeedCursor;
        ++sent;
    }
}

// ============================================================
// 主线程收结果，创建 GPU mesh
// ============================================================
void MyGame::DrainLodResults() {
    constexpr int K = 4;
    int done = 0;
    for (;;) {
        LodResult r;
        {
            std::lock_guard lk(m_lodResultMutex);
            if (m_lodResultQueue.empty() || done >= K) break;
            r = std::move(m_lodResultQueue.front());
            m_lodResultQueue.pop();
        }

        LodKey k{ r.level, r.cx, r.cz };
        m_lodPending.erase(k);

        if (r.vertices.empty()) continue;

        Eng::client::MeshData data;
        data.vertices = std::move(r.vertices);
        data.indices  = std::move(r.indices);
        auto mesh = renderer->CreateMesh(data);

        LodUnit u;
        u.level = r.level;
        u.cx = r.cx;
        u.cz = r.cz;
        u.mesh = mesh;
        u.aabbMin = r.aabbMin;
        u.aabbMax = r.aabbMax;
        m_lodUnits[k] = u;

        ++done;
    }
}

void MyGame::UpdateFrustum(const glm::mat4& vp) {
    auto row = [&](int r) { return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]); };
    glm::vec4 r0=row(0), r1=row(1), r2=row(2), r3=row(3);

    m_frustum.planes[0] = r3 + r0;   // left
    m_frustum.planes[1] = r3 - r0;   // right
    m_frustum.planes[2] = r3 + r1;   // bottom
    m_frustum.planes[3] = r3 - r1;   // top
    m_frustum.planes[4] = r3 + r2;   // near
    m_frustum.planes[5] = r3 - r2;   // far

    for (auto& p : m_frustum.planes) {
        float len = glm::length(glm::vec3(p));
        if (len > 0.0f) p /= len;
    }
}

bool MyGame::AabbInFrustum(const glm::vec3& mn, const glm::vec3& mx) const {
    for (const auto& p : m_frustum.planes) {
        glm::vec3 n = glm::vec3(p);
        float   d = p.w;
        // 找 AABB 中"最靠平面"的顶点：沿法线方向最远的点
        glm::vec3 v(
            n.x >= 0 ? mx.x : mn.x,
            n.y >= 0 ? mx.y : mn.y,
            n.z >= 0 ? mx.z : mn.z
        );
        if (glm::dot(n, v) + d < 0.0f) return false;   // 全在平面外侧
    }
    return true;
}

}