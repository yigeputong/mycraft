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
#include "game/client/TextureAtlas.h"
#include "game/client/MeshBuilderUtils.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_messagebox.h>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace game {

// ============================================================
// OnStart 分阶段
// ============================================================
void MyGame::LoadSettings(Eng::Engine& engine) {
    std::ifstream f("settings.json");
    if (!f.is_open()) return;
    try {
        json j;
        f >> j;
        auto& wcfg = engine.GetConfig().window;
        
        if (j.contains("apitype")) {
            int v = j["apitype"];
            if (v >= 0 && v <= 2) {
                wcfg.apitype = static_cast<Eng::client::RenderAPItype>(v);
            }
        }
        
        auto& rcfg = engine.GetConfig().render;
        if (j.contains("renderDistance")) m_renderDistance = j["renderDistance"];
        if (j.contains("fov")) rcfg.fov = j["fov"];
        if (j.contains("aoStrength")) rcfg.m_aoStrength = j["aoStrength"];
    } catch (const std::exception& e) {
        logWarning(logger, "[Game] Failed to parse settings.json: " << e.what());
    }
}

void MyGame::SaveSettings(Eng::Engine& engine) {
    json j;
    j["apitype"] = static_cast<int>(engine.GetConfig().window.apitype);
    
    auto& rcfg = engine.GetConfig().render;
    j["renderDistance"] = m_renderDistance;
    j["fov"] = rcfg.fov;
    j["aoStrength"] = rcfg.m_aoStrength;
    
    std::ofstream f("settings.json");
    if (f.is_open()) f << j.dump(4);
}

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
        .apitype      = engine.GetConfig().window.apitype,
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
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                m_shouldQuit = true;
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
    
    // ImGui 全局样式设置保留在此处（引擎级 UI 初始化）
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(10, 10);
    
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]       = ImVec4(0.08f, 0.08f, 0.09f, 0.95f);
    colors[ImGuiCol_ChildBg]        = ImVec4(0.12f, 0.12f, 0.13f, 0.60f);
    colors[ImGuiCol_Text]           = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_CheckMark]      = ImVec4(0.20f, 0.80f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrab]     = ImVec4(0.20f, 0.80f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_Header]         = ImVec4(0.20f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.20f, 0.80f, 0.80f, 0.30f);

    float dpiScale = SDL_GetWindowDisplayScale(mainWin->GetSDLWindow());
    if (dpiScale <= 0.0f) dpiScale = 1.0f;
    m_uiScale = dpiScale;

    style.ScaleAllSizes(m_uiScale);
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = m_uiScale;

    // 图集纹理从 TextureAtlas.h 获取
    auto pixels = client::GenerateAtlasPixels();
    m_atlasTexture = renderer->CreateTextureFromPixels(pixels.data(), client::ATLAS_W, client::ATLAS_H);
    logInfo(logger, "[MyGame] atlas texture created");

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

    if (engine.IsDedicatedServer()) {
        if (!StartLocalServer(false, false)) {
            logError(logger, "[Game] Failed to start dedicated server");
            m_shouldQuit = true;
            return;
        }
        logInfo(logger, "[Game] Running as dedicated server");
        return;
    }

    LoadSettings(engine);

    try {
        if (!SetupWindow(engine))   return;
        if (!mainWin->GetFallbackReason().empty()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "图形 API 降级", mainWin->GetFallbackReason().c_str(), nullptr);
        }
    } catch (const std::exception& e) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "无法启动渲染器", e.what(), nullptr);
        m_shouldQuit = true;
        return;
    }

    if (!SetupRenderer(engine)) return;
    SetupShaders();

    m_meshManager.Start();

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
        return false;
    }

    const bool wantRelative = (m_state == GameState::Playing) && !m_menuOpen;
    if (wantRelative != m_relativeMode) {
        Eng::client::Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), wantRelative);
        m_relativeMode = wantRelative;
    }

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

    if (m_state != GameState::Playing) {
        return m_shouldQuit;
    }

    game::PlayerMotion m;
    m.position = cameraPos;
    m.velocity = m_playerVelocity;
    m.onGround = m_playerOnGround;
    m.flyMode  = m_flyMode;

    game::StepPlayer(m, m_lastMoveDir, m_jumpHoldTimer > 0.0f, deltaTime,
        [this](int x, int y, int z) { return IsSolidAt(x, y, z); });

    cameraPos        = m.position;
    m_playerVelocity = m.velocity;
    m_playerOnGround = m.onGround;

    constexpr float CORRECTION_RATE = 8.0f;
    float t = 1.0f - std::exp(-CORRECTION_RATE * deltaTime);
    cameraPos      += m_positionError * t;
    m_positionError *= (1.0f - t);

    float hoursPerSec = 24.0f / kDayLengthSec;
    m_timeOfDay = std::fmod(m_timeOfDay + hoursPerSec * deltaTime, 24.0f);

    float angle = (m_timeOfDay / 24.0f) * 6.2831853f - 1.5707963f;
    glm::vec3 sunDir = glm::normalize(glm::vec3(cos(angle), sin(angle), 0.3f));
    float sunH = sunDir.y;

    float sunI = glm::clamp(sunH * 4.0f, 0.0f, 1.0f);
    float warm = 1.0f - glm::clamp(sunH * 3.0f, 0.0f, 1.0f);
    glm::vec3 sunColor = glm::mix(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.5f, 0.2f), warm);
    glm::vec3 ambient = glm::mix(glm::vec3(0.10f, 0.12f, 0.20f), glm::vec3(0.55f, 0.60f, 0.70f), glm::clamp(sunH * 2.0f, 0.0f, 1.0f));

    renderer->SetLightPosition(sunDir);
    renderer->SetLightColor(sunColor, sunI);
    renderer->SetLightAmbient(ambient);

    if (GetInput(deltaTime)) return true;

    std::vector<uint8_t> data;
    while (m_client->Receive(data)) {
        HandleServerMessage(data);
    }

    constexpr float PICKUP_TIME = 0.2f;
    for (auto it = m_itemEntities.begin(); it != m_itemEntities.end(); ) {
        auto& e = it->second;

        if (e.pickingUp) {
            e.pickupT += deltaTime / PICKUP_TIME;
            if (e.pickupT >= 1.0f) {
                if (e.pickerId == m_myClientId) {
                    AddToInventory(e.itemType, 1);
                }
                it = m_itemEntities.erase(it);
                continue;
            }
            glm::vec3 target = cameraPos + glm::vec3(0.0f, PLAYER_EYE / 2, 0.0f);
            float pickupT = e.pickupT;
            pickupT = pickupT * pickupT;
            e.renderPos = glm::mix(e.pickupStart, target, pickupT);
            ++it;
            continue;
        }

        if (e.interpT < 1.0f) {
            e.interpT += deltaTime * 20.0f;
            if (e.interpT > 1.0f) e.interpT = 1.0f;
            e.renderPos = glm::mix(e.prevPos, e.targetPos, e.interpT);
        }
        ++it;
    }

    m_world.UpdateStreaming(cameraPos, m_renderDistance, 
        [this](int cx, int cz) { 
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkRequest));
            w.Write<int32_t>(cx);
            w.Write<int32_t>(cz);
            m_client->Send(w.GetBuffer());
        },
        [this](Eng::client::MeshHandle mesh) { 
            renderer->DestroyMesh(mesh);
        }
    );

    auto dirtyKeys = m_world.FetchDirtyChunks(2);
    for (uint64_t key : dirtyKeys) {
        auto* cc = m_world.GetChunk(key);
        if (cc) QueueChunkMeshTask(key, cc->chunk);
    }

    m_lodManager.Update(cameraPos, m_world, renderer);
    DrainMeshResults();

    return false;
}

void MyGame::OnRender(Eng::Engine& engine) {
    if (m_minimized) return;

    if (m_applyPending) {
        renderer->ApplySettings(mainWin->GetConfigs(), engine.GetConfig().render);
        m_applyPending = false;
    }
    renderer->BeginFrame();

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
        glm::mat4 vp = projection * view;
        UpdateFrustum(vp);

        renderer->SetViewMatrix(view);
        renderer->SetProjectionMatrix(projection);
        renderer->SetViewPosition(cameraPos);

        Eng::client::Material mat;
        mat.diffuse = m_atlasTexture;

        renderer->SetUniform(m_cubeShader, "uAOStrength", engine.GetConfig().render.m_aoStrength);
        
        m_world.CollectVisibleMeshes(m_frustum, m_visibleChunkMeshes);
        if (!m_visibleChunkMeshes.empty()) {
            renderer->DrawMeshBatch(m_visibleChunkMeshes, m_cubeShader, mat);
        }

        Eng::client::Material lodMat;
        lodMat.diffuse = m_atlasTexture;

        m_lodManager.CollectVisibleMeshes(m_frustum, m_visibleLodMeshes);
        if (!m_visibleLodMeshes.empty()) {
            renderer->DrawMeshBatch(m_visibleLodMeshes, m_lodShader, lodMat);
        }

        if (m_playerMesh == 0) m_playerMesh = BuildPlayerMesh();
        Eng::client::Material playerMat;
        playerMat.diffuse = m_atlasTexture;
        for (auto& [id, ps] : m_otherPlayers) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, ps.position);
            model = glm::rotate(model, ps.yaw, glm::vec3(0, 1, 0));
            renderer->SetModelMatrix(model);
            renderer->DrawMesh(m_playerMesh, m_cubeShader, playerMat);
        }

        Eng::client::Material itemMat;
        itemMat.diffuse = m_atlasTexture;

        constexpr float BOUNCE_FREQ = 1.0f / 0.4f;
        constexpr float BOUNCE_AMP  = 0.45f;
        constexpr float XZ_RATIO    = 0.8f;
        float t = m_animTime * BOUNCE_FREQ * 6.2831853f;
        float sY  = 1.0f + BOUNCE_AMP * std::sin(t);
        float sXZ = 1.0f - BOUNCE_AMP * XZ_RATIO * std::sin(t);

        std::unordered_map<BlockType, std::vector<glm::mat4>> itemTransforms;
        itemTransforms.reserve(m_itemEntities.size());

        for (auto& [id, e] : m_itemEntities) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, e.renderPos);
            model = model * glm::rotate(glm::mat4(1.0f), m_animTime * 2.0f, glm::vec3(0, 1, 0));
            model = glm::scale(model, glm::vec3(sXZ * 0.3f, sY * 0.3f, sXZ * 0.3f));
            itemTransforms[e.itemType].push_back(model);
        }

        for (auto& [type, transforms] : itemTransforms) {
            if (transforms.empty()) continue;
            auto mesh = GetOrCreateItemMesh(type);
            if (mesh != 0) {
                renderer->DrawMeshInstanced(mesh, m_cubeShader, itemMat, transforms);
            }
        }

        renderer->SetUniform(m_skyShader, "uTimeOfDay", m_timeOfDay);
        renderer->DrawSkybox(m_skyShader);
    }

    renderer->UnbindFramebuffer();
    
    renderer->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    renderer->Clear();

    renderer->DrawFullscreenQuad(renderer->GetFramebufferTexture(fbo));

    // ★ 完全交由 ClientUI 接管渲染
    m_ui.Draw(engine, *this);

    renderer->EndFrame();
}

void MyGame::ReturnToMainMenu() {
    if (renderer) renderer->WaitIdle();

    if (m_client) m_client.reset();
    if (m_server) {
        m_server->Stop();
        m_server.reset();
    }

    m_lodManager.Stop();
    m_meshManager.Stop();

    if (renderer) {
        for (auto& [key, cc] : m_world.GetChunks()) {
            if (cc.mesh) renderer->DestroyMesh(cc.mesh);
        }
        for (auto& [type, mesh] : m_itemMeshCache) {
            if (mesh) renderer->DestroyMesh(mesh);
        }
        if (m_playerMesh != 0) { renderer->DestroyMesh(m_playerMesh); m_playerMesh = 0; }
        m_lodManager.Clear(renderer);
    }
    
    m_world.Clear();
    m_itemMeshCache.clear();
    m_itemEntities.clear();
    m_otherPlayers.clear();
    m_myClientId = 0;

    cameraPos   = glm::vec3(0.0f, 40.0f, 0.0f);
    cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    yaw   = -90.0f;
    pitch = 0.0f;
    m_playerVelocity   = glm::vec3(0.0f);
    m_positionError    = glm::vec3(0.0f);

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

    auto* safeRenderer = mainWin ? mainWin->GetAPI() : nullptr;
    if (safeRenderer) {
        m_lodManager.Stop();
        m_lodManager.Clear(safeRenderer);
        if (m_cubeShader) safeRenderer->DestroyShader(m_cubeShader);
        if (m_skyShader)  safeRenderer->DestroyShader(m_skyShader);
        if (m_lodShader)  safeRenderer->DestroyShader(m_lodShader);
        if (m_playerMesh != 0) { safeRenderer->DestroyMesh(m_playerMesh); m_playerMesh = 0; }
        safeRenderer->ShutdownImGuiBackend();
    }
    renderer = nullptr;
    mainWin = nullptr; 
}

bool MyGame::GetInput(float dt) {
    using namespace Eng::client;

    if (Input::IsKeyPressed(KeyCode::Escape)) {
        m_menuOpen = !m_menuOpen;
    }
    if (m_menuOpen) return m_shouldQuit;

    if (Input::IsKeyPressed(KeyCode::F)) {
        m_flyMode = !m_flyMode;
        m_positionError = glm::vec3(0.0f);
        m_playerVelocity = glm::vec3(0.0f);
    }

    if (Input::IsKeyPressed(KeyCode::F11)) {
        auto& configs = mainWin->GetConfigs();
        configs.fullscreen = !configs.fullscreen;
        mainWin->SetFullscreen(configs.fullscreen);
        renderer->DestroyFramebuffer(fbo);
        fbo = renderer->CreateFramebuffer(
            mainWin->GetConfigs().windowPixelWidth,
            mainWin->GetConfigs().windowPixelHeight);
    }

    if (Input::IsKeyPressed(KeyCode::F3)) {
        m_showDebug = !m_showDebug;
    }

    glm::vec2 mouseDelta = Input::GetMouseDelta();
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
        yaw   += mouseDelta.x * 0.1f;
        pitch -= mouseDelta.y * 0.1f;
        pitch = glm::clamp(pitch, -89.0f, 89.0f);
    }
    
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
    m_lastMoveDir = moveDir;
    input.look    = glm::vec2(glm::radians(yaw), glm::radians(pitch));
    
    if (Input::IsKeyDown(KeyCode::Space)) {
        m_jumpHoldTimer = 0.2f;
    }
    m_jumpHoldTimer = std::max(0.0f, m_jumpHoldTimer - dt);
    input.jump = (m_jumpHoldTimer > 0.0f);

    m_placeCooldown = std::max(0.0f, m_placeCooldown - dt);
    m_digCooldown   = std::max(0.0f, m_digCooldown   - dt);

    bool wantDig   = Input::IsKeyDown(KeyCode::MouseLeft);
    bool wantPlace = Input::IsKeyDown(KeyCode::MouseRight);

    input.dig   = (wantDig   && m_digCooldown   <= 0.0f);
    input.place = (wantPlace && m_placeCooldown <= 0.0f);

    if (input.dig)   m_digCooldown   = kDigInterval;
    if (input.place) m_placeCooldown = kPlaceInterval;

    float scroll = Input::GetScrollDelta();
    if (scroll > 0) m_hotbarIndex = (m_hotbarIndex - 1 + kHotbarSize) % kHotbarSize;
    if (scroll < 0) m_hotbarIndex = (m_hotbarIndex + 1) % kHotbarSize;
    
    if (Input::IsKeyPressed(KeyCode::Num1)) m_hotbarIndex = 0;
    if (Input::IsKeyPressed(KeyCode::Num2)) m_hotbarIndex = 1;
    if (Input::IsKeyPressed(KeyCode::Num3)) m_hotbarIndex = 2;
    if (Input::IsKeyPressed(KeyCode::Num4)) m_hotbarIndex = 3;
    
    if (m_hotbarIndex < 0 || m_hotbarIndex >= kHotbarSize) {
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
    w.WriteVec3(input.moveDir);
    w.WriteVec2(input.look);
    w.Write(static_cast<uint8_t>(input.jump  ? 1 : 0));
    w.Write(static_cast<uint8_t>(input.dig   ? 1 : 0));
    w.Write(static_cast<uint8_t>(input.place ? 1 : 0));
    w.Write<uint16_t>(input.placeBlock);
    w.Write(static_cast<uint8_t>(input.fly ? 1 : 0));
    m_client->Send(w.GetBuffer());

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

        case net::MessageType::PlayerId: {
            m_myClientId = r.ReadU32BE();
            logInfo(logger, "[Net] My client ID: " << m_myClientId);
            break;
        }

        case game::net::MessageType::ChunkData: {
            int cx = r.Read<int>();
            int cz = r.Read<int>();
            uint16_t sy = r.Read<uint16_t>();
            if (sy != CHUNK_SIZE_Y) return;

            auto bytes = r.ReadBytes(CHUNK_VOLUME);
            std::vector<uint8_t> blockData(bytes.begin(), bytes.end());

            uint64_t key = ChunkKey(cx, cz);
            m_world.AddChunk(cx, cz, blockData, 0);

            m_world.MarkDirty(key);
            
            // 邻居也打上脏标记
            m_world.MarkDirty(ChunkKey(cx - 1, cz));
            m_world.MarkDirty(ChunkKey(cx + 1, cz));
            m_world.MarkDirty(ChunkKey(cx, cz - 1));
            m_world.MarkDirty(ChunkKey(cx, cz + 1));

            break;
        }

        case game::net::MessageType::BlockChange: {
            int bx = r.Read<int32_t>();
            int by = r.Read<int32_t>();
            int bz = r.Read<int32_t>();
            auto t = static_cast<BlockType>(r.Read<uint16_t>());

            m_world.SetBlock(bx, by, bz, t);
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
                it->second.prevPos   = it->second.renderPos;
                it->second.targetPos = pos;
                it->second.interpT   = 0.0f;
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
            m_lodManager.Start(seed);
            logInfo(logger, "[LOD] Received world seed: " << seed
                    << " spawn: " << spawn.x << "," << spawn.y << "," << spawn.z);
            break;
        }
        
        case game::net::MessageType::PlayerInput:
        case game::net::MessageType::ChunkRequest:
        case game::net::MessageType::ClientHello:
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
    Eng::client::MeshData data = client::MeshBuilderUtils::BuildItemMeshData(type);
    return renderer->CreateMesh(data);
}

Eng::client::MeshHandle MyGame::BuildPlayerMesh() {
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t> indices;

    auto addBox = [&](glm::vec3 c, glm::vec3 size, int tile) {
        const float x0 = c.x - size.x*0.5f, x1 = c.x + size.x*0.5f;
        const float y0 = c.y - size.y*0.5f, y1 = c.y + size.y*0.5f;
        const float z0 = c.z - size.z*0.5f, z1 = c.z + size.z*0.5f;

        int tileRow = tile / client::ATLAS_COLS;
        int tileCol = tile % client::ATLAS_COLS;
        float u0 = tileCol * client::TILE_UV;
        float v0 = tileRow * client::TILE_UV;

        const glm::vec3 corners[8] = {
            {x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},
            {x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1},
        };
        const int faces[6][4] = {
            {1,5,6,2}, {3,7,4,0}, {7,6,5,4}, {0,1,2,3}, {3,2,6,7}, {0,4,5,1}
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
                v.uv = { u0 + fuv[k][0]*client::TILE_UV,
                         v0 + (1.0f - fuv[k][1])*client::TILE_UV };
                v.ao = 1.0f;
                v.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                vertices.push_back(v);
            }
            indices.push_back(base+0); indices.push_back(base+1); indices.push_back(base+2);
            indices.push_back(base+2); indices.push_back(base+3); indices.push_back(base+0);
        }
    };

    addBox({0.0f, 1.55f, 0.0f}, {0.5f, 0.5f, 0.5f}, 13);
    addBox({0.0f, 1.05f, 0.0f}, {0.5f, 0.5f, 0.25f}, 14);
    addBox({-0.375f, 1.05f, 0.0f}, {0.25f, 0.5f, 0.25f}, 14);
    addBox({ 0.375f, 1.05f, 0.0f}, {0.25f, 0.5f, 0.25f}, 14);
    addBox({-0.125f, 0.4f, 0.0f}, {0.25f, 0.8f, 0.25f}, 15);
    addBox({ 0.125f, 0.4f, 0.0f}, {0.25f, 0.8f, 0.25f}, 15);

    Eng::client::MeshData data;
    data.vertices = std::move(vertices);
    data.indices  = std::move(indices);
    return renderer->CreateMesh(data);
}

BlockType MyGame::GetBlockAt(int wx, int wy, int wz) const {
    return m_world.GetBlockAt(wx, wy, wz);
}

bool MyGame::IsSolidAt(int wx, int wy, int wz) const {
    return m_world.IsSolidAt(wx, wy, wz);
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

    for (auto& slot : m_hotbar) {
        if (slot.type == type) {
            slot.count += count;
            return;
        }
    }

    for (auto& slot : m_hotbar) {
        if (slot.isEmpty()) {
            slot.type  = type;
            slot.count = count;
            return;
        }
    }
}

void MyGame::DrainMeshResults() {
    m_meshManager.DrainResults([this](game::client::MeshResult&& r) {
        auto* cc = m_world.GetChunk(r.chunkKey);
        if (cc) {
            cc->chunk.sky = r.sky;
            cc->chunk.block = r.block;

            if (r.vertices.empty()) {
                if (cc->mesh != 0) {
                    renderer->DestroyMesh(cc->mesh);
                    cc->mesh = 0;
                }
            } else {
                Eng::client::MeshData data;
                data.vertices = std::move(r.vertices);
                data.indices  = std::move(r.indices);

                auto newMesh = renderer->CreateMesh(data);
                if (cc->mesh != 0) {
                    renderer->DestroyMesh(cc->mesh);
                }
                cc->mesh = newMesh;
            }
        }
    });
}

void MyGame::QueueChunkMeshTask(uint64_t key, const Chunk& chunk) {
    int cx = chunk.chunk_x;
    int cz = chunk.chunk_z;

    const Chunk* nXm = nullptr;
    const Chunk* nXp = nullptr;
    const Chunk* nZm = nullptr;
    const Chunk* nZp = nullptr;

    auto* cXm = m_world.GetChunk(ChunkKey(cx - 1, cz));
    if (cXm) nXm = &cXm->chunk;
    auto* cXp = m_world.GetChunk(ChunkKey(cx + 1, cz));
    if (cXp) nXp = &cXp->chunk;
    auto* cZm = m_world.GetChunk(ChunkKey(cx, cz - 1));
    if (cZm) nZm = &cZm->chunk;
    auto* cZp = m_world.GetChunk(ChunkKey(cx, cz + 1));
    if (cZp) nZp = &cZp->chunk;

    m_meshManager.Submit(key, chunk, nXm, nXp, nZm, nZp);
}

void MyGame::UpdateFrustum(const glm::mat4& vp) {
    auto row = [&](int r) { return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]); };
    glm::vec4 r0=row(0), r1=row(1), r2=row(2), r3=row(3);

    m_frustum.planes[0] = r3 + r0;
    m_frustum.planes[1] = r3 - r0;
    m_frustum.planes[2] = r3 + r1;
    m_frustum.planes[3] = r3 - r1;
    m_frustum.planes[4] = r3 + r2;
    m_frustum.planes[5] = r3 - r2;

    for (auto& p : m_frustum.planes) {
        float len = glm::length(glm::vec3(p));
        if (len > 0.0f) p /= len;
    }
}

bool MyGame::AabbInFrustum(const glm::vec3& mn, const glm::vec3& mx) const {
    for (const auto& p : m_frustum.planes) {
        glm::vec3 n = glm::vec3(p);
        float   d = p.w;
        glm::vec3 v(
            n.x >= 0 ? mx.x : mn.x,
            n.y >= 0 ? mx.y : mn.y,
            n.z >= 0 ? mx.z : mn.z
        );
        if (glm::dot(n, v) + d < 0.0f) return false;
    }
    return true;
}

} // namespace game