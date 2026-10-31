#include "game/Game.h"

#include "Engine.h"
#include "client/AssetManager.h"
#include "client/Render/RenderAPI.h"
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
#include <cmath>
#include <memory>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace game {

namespace {
    void AppendPlayerBox(glm::vec3 c, glm::vec3 size, 
                        const glm::vec4& top, const glm::vec4& bottom,
                        const glm::vec4& right, const glm::vec4& left,
                        const glm::vec4& front, const glm::vec4& back,
                        std::vector<Eng::client::Vertex>& vertices,
                        std::vector<uint32_t>& indices) {
        const float x0 = c.x - size.x*0.5f, x1 = c.x + size.x*0.5f;
        const float y0 = c.y - size.y*0.5f, y1 = c.y + size.y*0.5f;
        const float z0 = c.z - size.z*0.5f, z1 = c.z + size.z*0.5f;

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
        
        static constexpr float fuv[4][2] = {{0,1},{1,1},{1,0},{0,0}}; 
        auto toUV = [](float px, float py) { return glm::vec2(px / 64.0f, py / 64.0f); };
        glm::vec4 faceUVs[6] = { right, left, top, bottom, front, back };

        for (int f = 0; f < 6; ++f) {
            auto base = (uint32_t)vertices.size();
            glm::vec4 uvRect = faceUVs[f];
            
            for (int k = 0; k < 4; ++k) {
                Eng::client::Vertex v;
                v.position = corners[faces[f][k]];
                v.normal   = normals[f];
                float u = uvRect.x + fuv[k][0] * uvRect.z;
                float v_coord = uvRect.y + fuv[k][1] * uvRect.w;
                v.uv = toUV(u, v_coord);
                v.ao = 1.0f;
                v.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                vertices.push_back(v);
            }
            indices.push_back(base+0); indices.push_back(base+1); indices.push_back(base+2);
            indices.push_back(base+2); indices.push_back(base+3); indices.push_back(base+0);
        }
    }
}

// ============================================================
// 设置持久化
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

// ============================================================
// 网络启动
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

// ============================================================
// 窗口 & 渲染器
// ============================================================
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
                if (renderer) RecreateFBOs(e.window.data1, e.window.data2);
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

void MyGame::RecreateFBOs(int w, int h) {
    if (w <= 0 || h <= 0) return;
    Eng::client::FramebufferCreateInfo info{
        .width = w, 
        .height = h, 
        .hasDepth = true, 
        .colorFormat = Eng::client::rhi::TextureFormat::RGBA8_UNORM
    };
    m_mainFbo = renderer->CreateFramebuffer(info);
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
    
    // 初始化现代化管线（包含了 DSA VAO、MDI Buffer 等）
    renderer->InitPipelines();

    if (!renderer->InitImGuiBackend()) {
        logError(logger, "[Game] ImGui backend init failed");
        return false;
    }

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

    static const ImWchar allGlyphs[] = { 0x0020, 0xFFFF, 0 };

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;

    float scaledFontSize = 16.0f * m_uiScale;

    ImFont* unifont = io.Fonts->AddFontFromFileTTF(
        "./assets/fonts/unifont-18.0.01.otf", scaledFontSize, &fontConfig, allGlyphs);

    if (!unifont) {
        logError(logger, "[Game] Failed to load Unifont, falling back to default font.");
        io.Fonts->AddFontDefault();
    } else {
        logInfo(logger, "[Game] Loaded Unifont with all BMP glyphs at size " << scaledFontSize);
    }

    m_assetManager = std::make_unique<Eng::client::AssetManager>(renderer);
    m_playerTexture = m_assetManager->LoadTexture("assets/textures/entity/player.png");

    const auto& pixels = client::AtlasPixels();
    int atlasW = client::AtlasCols() * client::ATLAS_TILE;
    int atlasH = client::AtlasRows() * client::ATLAS_TILE;
    m_atlasTexture = renderer->CreateTexture(pixels.data(), atlasW, atlasH);
    if (!m_atlasTexture) {
        logError(logger, "[MyGame] Failed to create atlas texture");
        return false;
    }
    logInfo(logger, "[MyGame] atlas texture created");

    RecreateFBOs(mainWin->GetConfigs().windowPixelWidth, mainWin->GetConfigs().windowPixelHeight);
    if (!m_mainFbo) {
        logError(logger, "[Game] FBO creation failed!");
        return false;
    }

    return true;
}

// ============================================================
// Shader
// ============================================================
void MyGame::SetupShaders() {
    // mesh 和 lod 默认就是 Opaque + Standard，利用默认参数即可
    m_cubeShader = m_assetManager->LoadShader(
        m_assetManager->MakeShaderCreateInfo("mesh"));
        
    m_lodShader = m_assetManager->LoadShader(
        m_assetManager->MakeShaderCreateInfo("lod"));
        
    // 天空盒必须显式指定 Skybox 管线（关闭深度写入与背面剔除）
    // 以及 Skybox 顶点布局（只读取 Position 属性）
    m_skyShader = m_assetManager->LoadShader(
        m_assetManager->MakeShaderCreateInfo("sky", 
            Eng::client::rhi::PipelineType::Skybox, 
            Eng::client::rhi::VertexLayout::Skybox));

    // SetupShaders
    m_entityShader = m_assetManager->LoadShader([&]{
        auto ci = m_assetManager->MakeShaderCreateInfo("entity");
        ci.pipelineType   = Eng::client::rhi::PipelineType::Opaque;
        ci.perObjectMode  = Eng::client::rhi::PerObjectMode::Default;
        return ci;
    }());

    m_handShader = m_assetManager->LoadShader([&]{
        auto ci = m_assetManager->MakeShaderCreateInfo("entity");
        ci.pipelineType   = Eng::client::rhi::PipelineType::Hand;
        ci.perObjectMode  = Eng::client::rhi::PerObjectMode::Default;
        return ci;
    }());
            
    // 顺手检查一下 Shader 是否加载成功，避免后续绑定空指针
    if (!m_cubeShader || !m_skyShader || !m_lodShader || !m_entityShader || !m_handShader) {
        std::string err = "核心着色器加载失败！请检查 assets/shaders/build/ 目录下是否存在对应的 SPIR-V (.spv) 文件。";
        logFatal(logger, "[MyGame] " << err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shader Load Error", err.c_str(), nullptr);
        throw std::runtime_error(err); // 抛出异常，让引擎捕获并退出
    }
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

    // ★ 1) 先加载方块注册表并生成图集纹理（SetupRenderer 里面要用）
    m_blockRegistry.LoadAll("assets/block");
    {
        auto tileTints = m_blockRegistry.CollectTileTints();
        client::BuildAtlas(tileTints);
    }

    // ★ 2) 窗口与渲染器
    try {
        if (!SetupWindow(engine)) return;
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

    // ★ 3) 注入注册表指针
    m_lodManager.SetRegistry(&m_blockRegistry);
    m_meshManager.SetRegistry(&m_blockRegistry);
    m_world.SetRegistry(&m_blockRegistry);
    m_meshManager.Start();

    // ★ 4) 热栏初始化（此时注册表已就绪）
    m_hotbar[0] = game::MakeItemStack(m_blockRegistry.Get("mycraft:torch").id,       64);
    m_hotbar[1] = game::MakeItemStack(m_blockRegistry.Get("mycraft:dirt").id,        64);
    m_hotbar[2] = game::MakeItemStack(m_blockRegistry.Get("mycraft:grass_block").id, 64);
    m_hotbar[3] = game::MakeItemStack(m_blockRegistry.Get("mycraft:sand").id,        64);

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

    // —— 物理步进 ——
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

    // —— 时间 & 光照 ——
    float hoursPerSec = 24.0f / kDayLengthSec;
    m_timeOfDay = std::fmod(m_timeOfDay + hoursPerSec * deltaTime, 24.0f);

    float angle = (m_timeOfDay / 24.0f) * 6.2831853f - 1.5707963f;
    glm::vec3 sunDir = glm::normalize(glm::vec3(cos(angle), sin(angle), 0.3f));
    float sunH = sunDir.y;

    float sunI = glm::clamp(sunH * 2.5f, 0.0f, 0.85f);
    float warm = 1.0f - glm::clamp(sunH * 3.0f, 0.0f, 1.0f);
    glm::vec3 sunColor = glm::mix(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.5f, 0.2f), warm);
    glm::vec3 ambient  = glm::mix(glm::vec3(0.1f, 0.12f, 0.2f), glm::vec3(0.35f, 0.4f, 0.45f), 
                                  glm::clamp(sunH * 2.0f, 0.0f, 1.0f));

    m_globalState.lightDir       = sunDir;
    m_globalState.lightColor     = sunColor;
    m_globalState.lightIntensity = sunI;
    m_globalState.lightAmbient   = ambient;
    m_globalState.timeOfDay      = m_timeOfDay;

    // —— 输入 ——
    if (GetInput(deltaTime)) return true;

    // —— 网络收包 ——
    std::vector<uint8_t> data;
    while (m_client->Receive(data)) {
        HandleServerMessage(data);
    }

    // —— 掉落物插值 / 拾取动画 ——
    constexpr float PICKUP_TIME = 0.2f;
    for (auto it = m_itemEntities.begin(); it != m_itemEntities.end(); ) {
        auto& e = it->second;

        if (e.pickingUp) {
            e.pickupT += deltaTime / PICKUP_TIME;
            if (e.pickupT >= 1.0f) {
                if (e.pickerId == m_myClientId) AddToInventory(e.itemType, 1);
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

    // —— 区块流式 ——
    m_world.UpdateStreaming(cameraPos, m_renderDistance,
        [this](int cx, int cz) {
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkRequest));
            w.Write<int32_t>(cx);
            w.Write<int32_t>(cz);
            m_client->Send(w.GetBuffer());
        }
    );

    for (auto it = m_allMeshes.begin(); it != m_allMeshes.end(); ) {
        if (m_world.GetChunk(it->first) == nullptr) {
            it = m_allMeshes.erase(it);
            m_globalGeometryDirty = true; // 需要重新构建大 Buffer
        } else {
            ++it;
        }
    }

    auto dirtyKeys = m_world.FetchDirtyChunks(std::numeric_limits<size_t>::max()); 
    for (uint64_t key : dirtyKeys) {
        auto* cc = m_world.GetChunk(key);
        if (cc) QueueChunkMeshTask(key);
    }

    m_lodManager.Update(cameraPos, m_world, m_renderDistance, renderer);
    DrainMeshResults();

    return false;
}

void MyGame::OnRender(Eng::Engine& engine) {
    if (m_minimized) return;

    if (m_applyPending) {
        renderer->ApplySettings(mainWin->GetConfigs(), engine.GetConfig().render);
        m_applyPending = false;
    }

    if (m_state == GameState::Playing) {
        glm::vec3 eye = cameraPos + glm::vec3(0.0f, PLAYER_EYE, 0.0f);
        view = glm::lookAt(eye, eye + cameraFront, cameraUp);
        projection = glm::perspective(glm::radians(engine.GetConfig().render.fov),
                                mainWin->GetAspectRatio(),
                                engine.GetConfig().render.zNear,
                                engine.GetConfig().render.zFar);
        UpdateFrustum(projection * view);
    }

    m_globalState.view        = view;
    m_globalState.projection  = projection;
    m_globalState.viewPos     = cameraPos;
    m_globalState.aoStrength  = engine.GetConfig().render.m_aoStrength;
    renderer->SetGlobalState(m_globalState);

    // ★ 清屏颜色必须在 BeginFrame 之前设置（Vulkan 的 BeginFrame 会把它写入 clearValue）
    if (m_state == GameState::Playing) {
        renderer->SetClearColor(0.5f, 0.7f, 1.0f, 1.0f);
    } else {
        renderer->SetClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    }

    renderer->BeginFrame();

    if (m_state == GameState::Playing) {
        Eng::client::Material mat; mat.diffuse = m_atlasTexture;

        // ==========================================================
        // 1. 地形 MDI
        // ==========================================================
        if (m_globalGeometryDirty) {
            RebuildGlobalGeometry();
            m_globalGeometryDirty = false;
        }

        m_indirectCommands.clear();
        for (const auto& [key, cc] : m_world.GetChunks()) {
            if (!AabbInFrustum(cc.aabbMin, cc.aabbMax)) continue;
            auto it = m_chunkMeshes.find(key);
            if (it == m_chunkMeshes.end() || it->second.indexCount == 0) continue;

            Eng::client::rhi::DrawIndexedIndirectCommand cmd{};
            cmd.indexCount    = it->second.indexCount;
            cmd.instanceCount = 1;
            cmd.firstIndex    = it->second.firstIndex;
            
            // ★ 核心修复：因为 DrainMeshResults 已经全局化了索引，这里的偏移必须是 0！
            cmd.vertexOffset  = 0; 
            
            m_indirectCommands.push_back(cmd);
        }

        if (!m_indirectCommands.empty()) {
            renderer->UploadIndirectCommands(m_indirectCommands);
            renderer->DrawMDI(m_cubeShader, mat, static_cast<uint32_t>(m_indirectCommands.size()));
        }

        // ==========================================================
        // 2. LOD
        // ==========================================================
        Eng::client::Material lodMat; lodMat.diffuse = m_atlasTexture;
        m_lodManager.CollectVisibleMeshes(m_frustum, m_visibleLodMeshes);
        if (!m_visibleLodMeshes.empty()) {
            for (const auto& mesh : m_visibleLodMeshes) {
                renderer->DrawMesh(mesh, m_lodShader, lodMat); // 不用 PerObject，用默认单位矩阵
            }
        }

        // ==========================================================
        // 3. 其他玩家（entity shader）
        // ==========================================================
        if (!m_playerMesh) m_playerMesh = BuildPlayerMesh();
        Eng::client::Material playerMat; playerMat.diffuse = m_playerTexture;
        for (auto& [id, ps] : m_otherPlayers) {
            Eng::client::rhi::PerObjectData data;
            data.model = glm::translate(glm::mat4(1.0f), ps.position);
            data.model = glm::rotate(data.model, ps.yaw, glm::vec3(0, 1, 0));
            renderer->DrawMesh(m_playerMesh, m_entityShader, playerMat, data);
        }

        // ==========================================================
        // 4. 掉落物（entity shader）
        // ==========================================================
        if (!m_itemEntities.empty()) {
            std::unordered_map<BlockId, std::vector<Eng::client::rhi::PerObjectData>> instances;
            for (const auto& [id, e] : m_itemEntities) {
                Eng::client::rhi::PerObjectData data;
                data.model = glm::translate(glm::mat4(1.0f), e.renderPos);
                data.model = glm::rotate(data.model, m_animTime * 2.0f, glm::vec3(0, 1, 0));
                data.model = glm::translate(data.model,
                    glm::vec3(0.0f, std::sin(m_animTime * 3.0f + e.id * 0.5f) * 0.1f + 0.1f, 0.0f));
                data.model = glm::scale(data.model, glm::vec3(0.25f));
                instances[e.itemType].push_back(data);
            }

            Eng::client::Material itemMat; itemMat.diffuse = m_atlasTexture;
            for (const auto& [type, perObjects] : instances) {
                auto mesh = GetOrCreateItemMesh(type);
                if (mesh) {
                    renderer->DrawMeshInstanced(mesh, m_entityShader, itemMat, perObjects);
                }
            }
        }

        // ==========================================================
        // 5. 天空盒
        // ==========================================================
        renderer->DrawSkybox(m_skyShader);

        // ==========================================================
        // 6. 第一人称手部 / 手持物品（hand shader）
        // ==========================================================
        if (!m_handMesh) m_handMesh = BuildHandMesh();

        glm::vec3 basePos(0.5f, -0.5f, -1.0f);
        if (m_walkDist > 0.0f) {
            basePos.x += std::sin(m_walkDist) * 0.03f;
            basePos.y -= std::abs(std::cos(m_walkDist)) * 0.03f;
        }
        if (m_swingProgress > 0.0f) {
            float swing = std::sin(m_swingProgress * 3.14159f);
            basePos.x -= swing * 0.15f;
            basePos.y -= swing * 0.15f;
            basePos.z += swing * 0.1f;
        }

        glm::mat4 handLocalModel = glm::mat4(1.0f);
        handLocalModel = glm::translate(handLocalModel, basePos);
        handLocalModel = glm::rotate(handLocalModel, glm::radians(15.0f),  glm::vec3(0, 1, 0));
        handLocalModel = glm::rotate(handLocalModel, glm::radians(-20.0f), glm::vec3(1, 0, 0));
        handLocalModel = glm::scale(handLocalModel, glm::vec3(0.5f));
        glm::mat4 handModel = glm::inverse(view) * handLocalModel;

        if (m_handShader) {
            Eng::client::Material handMat; handMat.diffuse = m_playerTexture;
            renderer->DrawMesh(m_handMesh, m_handShader, handMat, { .model = handModel });

            auto& curSlot = m_hotbar[m_hotbarIndex];
            if (!curSlot.isEmpty()) {
                auto itemMesh = GetOrCreateItemMesh(curSlot.type);
                if (itemMesh) {
                    glm::mat4 itemLocalModel = handLocalModel;
                    itemLocalModel = glm::translate(itemLocalModel, glm::vec3(0.0f, 0.2f, -0.5f));
                    itemLocalModel = glm::scale(itemLocalModel, glm::vec3(0.5f));
                    glm::mat4 itemModel = glm::inverse(view) * itemLocalModel;
                    renderer->DrawMesh(itemMesh, m_handShader, mat, { .model = itemModel });
                }
            }
        }
    }

    m_ui.Draw(engine, *this);
    renderer->EndFrame();
}

void MyGame::ReturnToMainMenu() {
    if (renderer) renderer->WaitIdle();

    if (m_client) m_client.reset();
    if (m_server) { m_server->Stop(); m_server.reset(); }

    m_lodManager.Stop();
    m_meshManager.Stop();

    m_playerMesh.reset();
    m_handMesh.reset();
    m_itemMeshCache.clear();

    m_world.Clear();
    m_itemEntities.clear();
    m_otherPlayers.clear();
    m_myClientId = 0;

    m_lodManager.Clear();

    // ★ 清理 MDI 数据
    m_chunkMeshes.clear();
    m_globalVertices.clear();
    m_globalIndices.clear();
    m_globalGeometryDirty = true;
    m_indirectCommands.clear();
    m_globalVertexCursor = 0;
    m_globalIndexCursor = 0;

    cameraPos   = glm::vec3(0.0f, 40.0f, 0.0f);
    cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    yaw   = -90.0f;
    pitch = 0.0f;
    m_playerVelocity = glm::vec3(0.0f);
    m_positionError  = glm::vec3(0.0f);

    m_menuOpen = false;
    m_state    = GameState::MainMenu;
    m_statusText.clear();

    logInfo(logger, "[Game] Returned to main menu");
}

void MyGame::OnShutdown(Eng::Engine& /*engine*/) {
    if (m_client) { m_client->Disconnect(); m_client.reset(); }
    if (m_server) { m_server->Stop(); m_server.reset(); }

    m_lodManager.Stop();
    m_meshManager.Stop();

    m_lodManager.Clear();
    m_visibleLodMeshes.clear();
    m_playerMesh.reset();
    m_handMesh.reset();
    m_itemMeshCache.clear();
    m_world.Clear();
    m_itemEntities.clear();
    m_otherPlayers.clear();
    
    // ★ 清理 MDI 数据
    m_chunkMeshes.clear();
    m_globalVertices.clear();
    m_globalIndices.clear();
    m_globalGeometryDirty = true;
    m_indirectCommands.clear();
    m_globalVertexCursor = 0;
    m_globalIndexCursor = 0;

    m_mainFbo.reset();
    m_cubeShader.reset();
    m_skyShader.reset();
    m_lodShader.reset();
    m_entityShader.reset();
    m_handShader.reset();
    m_atlasTexture.reset();
    m_playerTexture.reset();
    m_playerMesh.reset();
    m_handMesh.reset();
    m_itemMeshCache.clear();
    
    if (m_assetManager) {
        m_assetManager.reset(); // 必须在这里 reset 掉 AssetManager
    }

    auto* safeRenderer = mainWin ? mainWin->GetAPI() : nullptr;
    if (safeRenderer) {
        safeRenderer->ShutdownImGuiBackend();
    }

    renderer = nullptr;
    winMgr->DestroyWindow(m_win);
    mainWin = nullptr;
}

// ============================================================
// 输入
// ============================================================
bool MyGame::GetInput(float dt) {
    using namespace Eng::client;

    if (Input::IsKeyPressed(KeyCode::Escape)) m_menuOpen = !m_menuOpen;
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
        RecreateFBOs(mainWin->GetConfigs().windowPixelWidth, mainWin->GetConfigs().windowPixelHeight);
    }

    if (Input::IsKeyPressed(KeyCode::F3)) m_showDebug = !m_showDebug;

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
    if (glm::length(moveDir) > 0.001f) moveDir = glm::normalize(moveDir);

    game::net::PlayerInput input;
    input.moveDir = moveDir;
    m_lastMoveDir = moveDir;
    input.look = glm::vec2(glm::radians(yaw), glm::radians(pitch));

    if (Input::IsKeyDown(KeyCode::Space)) m_jumpHoldTimer = 0.2f;
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

    if (m_hotbarIndex < 0 || m_hotbarIndex >= kHotbarSize) m_hotbarIndex = 0;

    auto& curSlot = m_hotbar[m_hotbarIndex];
    if (curSlot.isEmpty()) {
        input.placeBlock = AIR_ID;
    } else {
        input.placeBlock = curSlot.type;
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

    if (glm::length(moveDir) > 0.1f && m_playerOnGround) {
        m_walkDist += dt * 10.0f;
    } else {
        m_walkDist = 0.0f;
    }

    if (wantDig && m_digCooldown > 0.0f) {
        m_swingProgress += dt * 4.0f;
        if (m_swingProgress > 1.0f) m_swingProgress = 0.0f;
    } else {
        m_swingProgress = 0.0f;
    }

    return m_shouldQuit;
}

// ============================================================
// 服务端消息处理
// ============================================================
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

            auto bytes = r.ReadBytes(CHUNK_VOLUME * sizeof(BlockId));
            std::vector<BlockId> blockData(reinterpret_cast<const BlockId*>(bytes.data()),
                                           reinterpret_cast<const BlockId*>(bytes.data() + bytes.size()));

            auto skyBytes = r.ReadBytes(CHUNK_VOLUME);
            std::array<uint8_t, CHUNK_VOLUME> skyData;
            std::memcpy(skyData.data(), skyBytes.data(), CHUNK_VOLUME);

            auto blockLightBytes = r.ReadBytes(CHUNK_VOLUME * sizeof(LightRGB));
            std::array<LightRGB, CHUNK_VOLUME> blockLightData;
            std::memcpy(blockLightData.data(), blockLightBytes.data(), CHUNK_VOLUME * sizeof(LightRGB));

            uint64_t key = ChunkKey(cx, cz);
            m_world.AddChunk(cx, cz, blockData, skyData, blockLightData, nullptr);

            m_world.MarkDirty(key);
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
            BlockId t = r.Read<uint16_t>(); 
            m_world.SetBlock(bx, by, bz, t);
            break;
        }

        case game::net::MessageType::EntitySpawn: {
            uint32_t  id   = r.Read<uint32_t>();
            glm::vec3 pos  = r.ReadVec3();
            BlockId   bt   = r.Read<uint16_t>(); 

            ClientItemEntity e;
            e.id        = id;
            e.itemType  = bt;
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

// ============================================================
// Mesh 构建
// ============================================================
Eng::client::MeshPtr MyGame::GetOrCreateItemMesh(BlockId id) { // ★
    auto it = m_itemMeshCache.find(id);
    if (it != m_itemMeshCache.end()) return it->second;

    auto mesh = BuildItemCubeMesh(id);
    m_itemMeshCache[id] = mesh;
    return mesh;
}

Eng::client::MeshPtr MyGame::BuildItemCubeMesh(BlockId id) { // ★
    Eng::client::MeshData data = client::MeshBuilderUtils::BuildItemMeshData(m_blockRegistry, id);
    return renderer->CreateMesh(data);
}

Eng::client::MeshPtr MyGame::BuildPlayerMesh() {
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t> indices;

    glm::vec4 h_top(8,0,8,8), h_bot(16,0,8,8), h_right(0,8,8,8), h_left(16,8,8,8), h_front(8,8,8,8), h_back(24,8,8,8);
    glm::vec4 b_top(20,16,8,4), b_bot(28,16,8,4), b_right(16,20,4,12), b_left(28,20,4,12), b_front(20,20,8,12), b_back(32,20,8,12);
    glm::vec4 ra_top(44,16,4,4), ra_bot(48,16,4,4), ra_right(40,20,4,12), ra_left(48,20,4,12), ra_front(44,20,4,12), ra_back(52,20,4,12);
    glm::vec4 rl_top(4,16,4,4), rl_bot(8,16,4,4), rl_right(0,20,4,12), rl_left(8,20,4,12), rl_front(4,20,4,12), rl_back(12,20,4,12);

    AppendPlayerBox({0.0f, 1.55f, 0.0f}, {0.5f, 0.5f, 0.5f}, h_top, h_bot, h_right, h_left, h_front, h_back, vertices, indices);
    AppendPlayerBox({0.0f, 1.05f, 0.0f}, {0.5f, 0.5f, 0.25f}, b_top, b_bot, b_right, b_left, b_front, b_back, vertices, indices);
    AppendPlayerBox({-0.375f, 1.05f, 0.0f}, {0.25f, 0.5f, 0.25f}, ra_top, ra_bot, ra_right, ra_left, ra_front, ra_back, vertices, indices);
    AppendPlayerBox({ 0.375f, 1.05f, 0.0f}, {0.25f, 0.5f, 0.25f}, ra_top, ra_bot, ra_right, ra_left, ra_front, ra_back, vertices, indices);
    AppendPlayerBox({-0.125f, 0.4f, 0.0f}, {0.25f, 0.8f, 0.25f}, rl_top, rl_bot, rl_right, rl_left, rl_front, rl_back, vertices, indices);
    AppendPlayerBox({ 0.125f, 0.4f, 0.0f}, {0.25f, 0.8f, 0.25f}, rl_top, rl_bot, rl_right, rl_left, rl_front, rl_back, vertices, indices);

    Eng::client::MeshData data;
    data.vertices = std::move(vertices);
    data.indices  = std::move(indices);
    return renderer->CreateMesh(data);
}

Eng::client::MeshPtr MyGame::BuildHandMesh() {
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t> indices;

    glm::vec4 ra_top(44,16,4,4), ra_bot(48,16,4,4), ra_right(40,20,4,12), ra_left(48,20,4,12), ra_front(44,20,4,12), ra_back(52,20,4,12);
    AppendPlayerBox({0.0f, 0.0f, 0.0f}, {0.25f, 0.25f, 0.6f}, ra_top, ra_bot, ra_right, ra_left, ra_front, ra_back, vertices, indices);

    Eng::client::MeshData data;
    data.vertices = std::move(vertices);
    data.indices  = std::move(indices);
    return renderer->CreateMesh(data);
}

// ============================================================
// 世界查询
// ============================================================
BlockId MyGame::GetBlockAt(int wx, int wy, int wz) const { // ★
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

// ============================================================
// 背包
// ============================================================
void MyGame::AddToInventory(BlockId id, int count) { // ★
    if (id == AIR_ID || count <= 0) return;

    for (auto& slot : m_hotbar) {
        if (slot.type == id) {
            slot.count += count;
            return;
        }
    }
    for (auto& slot : m_hotbar) {
        if (slot.isEmpty()) {
            slot.type  = id;
            slot.count = count;
            return;
        }
    }
}

// ============================================================
// Mesh 结果汇聚
// ============================================================
void MyGame::DrainMeshResults() {
    std::vector<uint64_t> dirtyKeys;
    size_t incomingVerts = 0, incomingInds = 0;

    m_meshManager.DrainResults([this, &dirtyKeys, &incomingVerts, &incomingInds](game::client::MeshResult&& r) {
        if (r.vertices.empty() || r.indices.empty()) {
            auto it = m_chunkMeshes.find(r.chunkKey);
            if (it != m_chunkMeshes.end() && it->second.indexCount > 0) {
                m_garbageVertices += it->second.vertexCount;
                m_garbageIndices  += it->second.indexCount;
                it->second.indexCount = 0; 
                it->second.vertexCount = 0;
            }
            m_allMeshes.erase(r.chunkKey);
            return;
        }
        
        incomingVerts += r.vertices.size();
        incomingInds += r.indices.size();

        auto oldIt = m_allMeshes.find(r.chunkKey);
        if (oldIt != m_allMeshes.end()) {
            m_garbageVertices += oldIt->second.vertices.size();
            m_garbageIndices  += oldIt->second.indices.size();
        }
        
        m_allMeshes[r.chunkKey] = std::move(r);
        dirtyKeys.push_back(r.chunkKey);
    });

    if (dirtyKeys.empty()) return;

    size_t maxVertices = m_globalVertexCapacity / sizeof(Eng::client::Vertex);
    size_t maxIndices  = m_globalIndexCapacity / sizeof(uint32_t);

    bool outOfCapacity = (m_globalVertexCursor + incomingVerts > maxVertices) ||
                         (m_globalIndexCursor + incomingInds > maxIndices);
                         
    // 提高阈值，避免频繁卡顿
    bool tooMuchGarbage = (m_garbageVertices > m_globalVertexCursor * 0.8) && (m_garbageVertices > 2000000);

    if (outOfCapacity || tooMuchGarbage || m_globalVertexCursor == 0) {
        // 走全量重建（注意：这里会同步更新 m_globalVertexCapacity）
        RebuildGlobalGeometry();
    } 
    else {
        // 增量追加
        for (uint64_t key : dirtyKeys) {
            const auto& meshResult = m_allMeshes[key];
            if (meshResult.vertices.empty()) continue;
            
            uint32_t vOffset = m_globalVertexCursor;
            uint32_t iOffset = m_globalIndexCursor;

            // 生成全局索引
            std::vector<uint32_t> globalizedIndices;
            globalizedIndices.reserve(meshResult.indices.size());
            for (uint32_t idx : meshResult.indices) {
                globalizedIndices.push_back(idx + vOffset);
            }

            try {
                renderer->UpdateGlobalGeometry(vOffset, meshResult.vertices, iOffset, globalizedIndices);
            } catch (const std::out_of_range& e) {
                logWarning(logger, "[MDI] Incremental failed: " << e.what() << ". Triggering rebuild.");
                m_globalGeometryDirty = true; 
                // ★ 绝对不要在这里重置游标，直接 return，由 OnRender 的 Rebuild 全量覆盖
                return; 
            }

            // 只有 GPU 写入成功，才同步 CPU 端的数据
            m_globalVertices.insert(m_globalVertices.end(), meshResult.vertices.begin(), meshResult.vertices.end());
            m_globalIndices.insert(m_globalIndices.end(), globalizedIndices.begin(), globalizedIndices.end());
            
            m_chunkMeshes[key] = ChunkMeshData{
                .firstIndex = iOffset,
                .vertexOffset = 0,
                .indexCount = static_cast<uint32_t>(meshResult.indices.size()),
                .vertexCount = static_cast<uint32_t>(meshResult.vertices.size())
            };
            
            m_globalVertexCursor += meshResult.vertices.size();
            m_globalIndexCursor  += meshResult.indices.size();
        }
    }
}

void MyGame::RebuildGlobalGeometry() {
    m_globalVertices.clear();
    m_globalIndices.clear();
    m_chunkMeshes.clear();
    
    m_globalVertexCursor = 0;
    m_globalIndexCursor = 0;
    m_garbageVertices = 0;
    m_garbageIndices = 0;

    for (auto& [key, meshResult] : m_allMeshes) {
        if (meshResult.vertices.empty()) continue;

        ChunkMeshData data;
        data.firstIndex = m_globalIndexCursor;
        data.vertexOffset = 0; 
        data.indexCount = static_cast<uint32_t>(meshResult.indices.size());
        data.vertexCount = static_cast<uint32_t>(meshResult.vertices.size());

        uint32_t vOffset = m_globalVertexCursor;
        
        m_globalVertices.insert(m_globalVertices.end(), meshResult.vertices.begin(), meshResult.vertices.end());
        
        for (uint32_t idx : meshResult.indices) {
            m_globalIndices.push_back(idx + vOffset);
        }
        
        m_globalVertexCursor += meshResult.vertices.size();
        m_globalIndexCursor  += meshResult.indices.size();
        m_chunkMeshes[key] = data;
    }

    // ★ 核心修复：让 RHI 重新分配足够大的显存，并返回真实容量
    renderer->UploadGlobalGeometry(m_globalVertices, m_globalIndices, m_globalVertexCapacity, m_globalIndexCapacity);
    
    // ★ 强制锁死主内存 Vector 的容量，绝对不允许后续增量插入时发生重分配
    m_globalVertices.reserve(m_globalVertexCapacity / sizeof(Eng::client::Vertex));
    m_globalIndices.reserve(m_globalIndexCapacity / sizeof(uint32_t));

    logInfo(logger, "[MDI] Compacted. Active Verts: " << m_globalVertexCursor 
            << ", VBO: " << m_globalVertexCapacity / 1024 / 1024 << " MB");
}

void MyGame::QueueChunkMeshTask(uint64_t key) {
    auto* pC = m_world.GetChunk(key);
    if (!pC || !pC->chunk) return;

    int cx = pC->chunk->chunk_x;
    int cz = pC->chunk->chunk_z;

    auto* cXm = m_world.GetChunk(ChunkKey(cx - 1, cz));
    auto* cXp = m_world.GetChunk(ChunkKey(cx + 1, cz));
    auto* cZm = m_world.GetChunk(ChunkKey(cx, cz - 1));
    auto* cZp = m_world.GetChunk(ChunkKey(cx, cz + 1));

    // 邻居不存在就传 nullptr
    m_meshManager.Submit(
        key, pC->chunk,
        cXm ? cXm->chunk : nullptr,
        cXp ? cXp->chunk : nullptr,
        cZm ? cZm->chunk : nullptr,
        cZp ? cZp->chunk : nullptr
    );
}

// ============================================================
// 视锥
// ============================================================
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