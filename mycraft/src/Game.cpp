#include "game/Game.h"

#include "Engine.h"
#include "core/Log.h"
#include "core/MessageWriter.h"
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

void MyGame::OnStart(Eng::Engine& engine) {
    if (!logger) {
        logger = std::make_unique<Eng::Log>();
    }
    m_server = std::make_unique<game::server::GameServer>();
    if (!m_server->Start(25565)) {              // ← 这里
        logError(logger, "[Game] Failed to start server");
        return;
    }
    if (!m_client.Connect("localhost", 25565)) {
        logError(logger, "[Game] Failed to connect to server");
        return;
    }
    logInfo(logger, "[Game] Connected to server");


    const Eng::client::RenderAPItype apiType = Eng::client::RenderAPItype::VULKAN;
    const bool useVulkan = (apiType == Eng::client::RenderAPItype::VULKAN);
    Eng::client::WindowConfig windowConfig = {
        .apitype = apiType,
        .title = "Mycraft v0.0.0",
        .windowWidth = 1280,
        .windowHeight = 720
    };
    winMgr = engine.GetWindowManager();
    m_win = winMgr->CreateWindow(windowConfig);
    mainWin = winMgr->GetWindow(m_win);
    if (!mainWin) return;
    mainWin->onEvent = [&](const SDL_Event& e) {
        ImGui_ImplSDL3_ProcessEvent(&e);
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard || io.WantCaptureMouse) {
        }
        Eng::client::Input::Get().ProcessEvent(e);
        switch(e.type) {
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                fbo = renderer->CreateFramebuffer(e.window.data1, e.window.data2);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                //m_paused = true;   //这东西只有单人模式有用
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                m_minimized = true;
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                m_minimized = false;
                break;
        }
    };

    renderer = mainWin->GetAPI();
    renderer->Initialize(mainWin->GetConfigs().windowWidth, mainWin->GetConfigs().windowHeight, mainWin);
    m_deviceInfo = renderer->GetDeviceInfo();
    logInfo(logger, "[RenderAPI] Backend:     " << m_deviceInfo.backend);
    logInfo(logger, "[RenderAPI] Device:      " << m_deviceInfo.deviceName);
    logInfo(logger, "[RenderAPI] Vendor:      " << m_deviceInfo.vendor);
    logInfo(logger, "[RenderAPI] API version: " << m_deviceInfo.apiVersion);
    logInfo(logger, "[RenderAPI] Driver:      " << m_deviceInfo.driverVersion);
    logInfo(logger, "[RenderAPI] Shader lang: " << m_deviceInfo.shadingLanguage);
    renderer->SetClearColor(0.125f,0.125f,0.125f,0.0f);

    Eng::client::Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), true);

    if (!renderer->InitImGuiBackend()) {
        logError(logger, "ImGui backend init failed");
        return;
    }

    auto pixels = GenerateAtlasPixels();
    m_atlasTexture = renderer->CreateTextureFromPixels(
        pixels.data(), ATLAS_W, ATLAS_H);
    logInfo(logger, "[MyGame] atlas texture handle=" << m_atlasTexture);

    if (useVulkan) {
        m_cubeShader = renderer->CreateShader(
            "./assets/shaders/build/mesh/vert.vk.spv",
            "./assets/shaders/build/mesh/frag.vk.spv");
    } else {
        m_cubeShader = renderer->CreateShader(
            "./assets/shaders/build/mesh/vert.gl.spv",
            "./assets/shaders/build/mesh/frag.gl.spv");
    }

    fbo = renderer->CreateFramebuffer(mainWin->GetConfigs().windowWidth, mainWin->GetConfigs().windowHeight);
    if (!fbo.isValid()) {
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
    // m_skyShader 段替换（改成 CreateSkybox）
    if (useVulkan) {
        m_skyShader = renderer->CreateSkybox(
            "./assets/shaders/build/sky/vert.vk.spv",
            "./assets/shaders/build/sky/frag.vk.spv");
    } else {
        m_skyShader = renderer->CreateSkybox(
            "./assets/shaders/build/sky/vert.gl.spv",
            "./assets/shaders/build/sky/frag.gl.spv");
    }
}

bool MyGame::OnUpdate(Eng::Engine&, float deltaTime) {
    if (m_minimized) {
        SDL_Delay(16);
        return false;
    }
    float frameMs = deltaTime * 1000.0f;
    m_lastFrameMs = frameMs;
    // 累加本秒的统计
    m_fpsTimer      += deltaTime;
    m_fpsFrameCount += 1;
    m_fpsAccumMs    += frameMs;
    m_fpsPeakMs      = std::max(m_fpsPeakMs, frameMs);
    // 每秒结算一次
    if (m_fpsTimer >= 1.0f) {
        m_fpsDisplay      = m_fpsFrameCount / m_fpsTimer;
        m_frameMsDisplay  = frameMs;
        m_frameMsAvgDisp  = m_fpsAccumMs / m_fpsFrameCount;
        m_frameMsPeakDisp = m_fpsPeakMs;

        m_fpsTimer      = 0.0f;
        m_fpsFrameCount = 0;
        m_fpsAccumMs    = 0.0f;
        m_fpsPeakMs     = 0.0f;
    }

    m_animTime += deltaTime;

    PlayerMotion me;
    me.position = cameraPos;
    me.velocity = m_playerVelocity;
    me.onGround = m_playerOnGround;

    game::StepPlayer(me, m_lastMoveDir, m_jumpHoldTimer > 0.0f, deltaTime,
        [this](int x, int y, int z) { return IsSolidAt(x, y, z); });

    cameraPos        = me.position;
    m_playerVelocity = me.velocity;
    m_playerOnGround = me.onGround;

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
    while (m_client.Receive(data)) {
        HandleServerMessage(data);
    }

    UpdateChunkStreaming();

    if (!m_meshQueue.empty()) {
        auto pm = std::move(m_meshQueue.front());
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

    // ============ Pass 1: 场景 → FBO ============
    renderer->BindFramebuffer(fbo);
    renderer->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    renderer->Clear();

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

        Eng::client::Material itemMat;
        itemMat.diffuse = m_atlasTexture;

        for (auto& [id, e] : m_itemEntities) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, e.pos);
            model = glm::rotate(model, m_animTime * 2.0f, glm::vec3(0, 1, 0));
            model = glm::scale(model, glm::vec3(0.3f));

            renderer->SetModelMatrix(model);
            auto mesh = GetOrCreateItemMesh(e.type);
            renderer->DrawMesh(mesh, m_cubeShader, itemMat);
        }

        renderer->SetUniform(m_skyShader, "uTimeOfDay", m_timeOfDay);
        renderer->DrawSkybox(m_skyShader);

    renderer->UnbindFramebuffer();
    
    // ============ Pass 2: 后处理 → 屏幕 ============
    renderer->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    renderer->Clear();

        renderer->DrawFullscreenQuad(renderer->GetFramebufferTexture(fbo));

        DrawUI(engine);

    renderer->EndFrame();
}

void MyGame::DrawUI(Eng::Engine& engine) {
    renderer->ImGuiNewFrame();
    ImGui::NewFrame();

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
                SDL_GL_SetSwapInterval(wcfg.vsync ? 1 : 0);
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
                m_shouldQuit = true;
            }
        }
        ImGui::End();
    }
}

bool MyGame::GetInput(float dt) {
    using namespace Eng::client;

    // ==================== ESC 开关菜单 ====================
    if (Input::IsKeyPressed(KeyCode::Escape)) {
        m_menuOpen = !m_menuOpen;
    }
    if (m_menuOpen) { // 菜单打开时冻结游戏输入
        // 解除相对模式，让鼠标能点 UI
        Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), false);
        return m_shouldQuit;
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

    // ==================== F3 调试面板 ====================
    if (Input::IsKeyPressed(KeyCode::F3)) {
        m_showDebug = !m_showDebug;
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
    // if (Input::IsKeyDown(KeyCode::Space))  moveDir.y += 1.0f;    // 不飞了
    // if (Input::IsKeyDown(KeyCode::LShift)) moveDir.y -= 1.0f;
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

    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::PlayerInput));
    w.WriteVec3(input.moveDir);   // ★ vec3
    w.WriteVec2(input.look);
    w.Write(static_cast<uint8_t>(input.jump  ? 1 : 0));
    w.Write(static_cast<uint8_t>(input.dig   ? 1 : 0));
    w.Write(static_cast<uint8_t>(input.place ? 1 : 0));
    w.Write<uint16_t>(input.placeBlock);
    m_client.Send(w.GetBuffer());
    // 本地预测
    // float speed = 5.0f * dt;
    // cameraPos += input.moveDir * speed;

    return m_shouldQuit;
}

void MyGame::OnShutdown(Eng::Engine&) {
    m_client.Disconnect();
    if (m_server) {
        m_server->Stop();
        m_server.reset();
    }

    renderer->DestroyShader(m_cubeShader);
    // renderer->DestroyTexture(m_cubeTexture);
    renderer->DestroyShader(m_fbShader);
    renderer->DestroyShader(m_skyShader);

    // ★ 统一走后端接口
    renderer->ShutdownImGuiBackend();

    winMgr->DestroyWindow(m_win);
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
                ps.id       = r.ReadU32BE();
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
            auto bytes = r.ReadBytes(CHUNK_VOLUME);
            std::memcpy(cc.chunk.blocks.data(), bytes.data(), CHUNK_VOLUME);
            cc.mesh = 0;

            uint64_t key = ChunkKey(cx, cz);
            m_chunks[key] = std::move(cc);
            m_pending.erase(key);

            PendingMesh pm;
            pm.chunkKey = key;
            pm.chunk = m_chunks[key].chunk;
            m_meshQueue.push(std::move(pm));

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
            uint32_t id = r.Read<uint32_t>();
            glm::vec3 pos = r.ReadVec3();
            BlockType bt = static_cast<BlockType>(r.Read<uint16_t>());

            ClientItemEntity e;
            e.id   = id;
            e.pos  = pos;
            e.type = bt;
            m_itemEntities[id] = e;
            break;
        }

        case game::net::MessageType::EntityDestroy: {
            uint32_t id = r.Read<uint32_t>();
            uint8_t reason = r.Read<uint8_t>();

            auto it = m_itemEntities.find(id);
            if (it != m_itemEntities.end()) {
                if (reason == 1) {
                    // 被捡走 → 进背包
                    AddToInventory(it->second.type, 1);
                }
                m_itemEntities.erase(it);
            }
            break;
        }
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

    auto isSolidAt = [&](int lx, int ly, int lz) -> bool {
        BlockType b = getBlock(lx, ly, lz);
        return b != BlockType::Air && b != BlockType::Water;
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

                for (int f = 0; f < 6; ++f) {
                    int nx = lx + faceNormals[f][0];
                    int ny = ly + faceNormals[f][1];
                    int nz = lz + faceNormals[f][2];

                    BlockType neighbor = getBlock(nx, ny, nz);
                    if (IsSolid(neighbor)) continue;

                    // 法线方向决定两个切向轴
                    int N_axis = (faceNormals[f][0] != 0) ? 0
                               : (faceNormals[f][1] != 0) ? 1 : 2;
                    int U_axis = (N_axis + 1) % 3;
                    int V_axis = (N_axis + 2) % 3;

                    glm::ivec3 N(faceNormals[f][0], faceNormals[f][1], faceNormals[f][2]);

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
        m_client.Send(w.GetBuffer());

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

}