#include "game/Game.h"

#include "Engine/Engine.h"
#include "Engine/service/AssetService.h"
#include "Engine/service/IRenderService.h"
#include "Engine/tool/Log.h"
#include "Engine/tool/MessageReader.h"
#include "Engine/tool/MessageWriter.h"
#include "Engine/tool/Transport.h"
#include "Engine/tool/MemoryTransport.h"
#include "Engine/tool/NetworkTransport.h"
#include "Engine/tool/Input.h"
#include "Engine/tool/Window.h"
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
#include <Engine/tool/FileTool.h>

using json = nlohmann::json;

namespace game {

namespace {
    void AppendPlayerBox(glm::vec3 c, glm::vec3 size, 
                        const glm::vec4& top, const glm::vec4& bottom,
                        const glm::vec4& right, const glm::vec4& left,
                        const glm::vec4& front, const glm::vec4& back,
                        std::vector<Eng::service::Vertex>& vertices,
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
                Eng::service::Vertex v;
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
    // 使用 GetWritablePath 获取跨平台的沙盒路径
    std::string path = Eng::tool::GetWritablePath("settings.json");
    auto data = Eng::tool::ReadFileToMemory(path);
    
    // 如果文件不存在（首次进入），直接返回，使用默认配置
    if (data.empty()) return; 

    try {
        // 直接从内存数据解析 JSON
        auto j = json::parse(data.begin(), data.end());
        
        auto& wcfg = engine.GetConfig().window;
        if (j.contains("apitype")) {
            int v = j["apitype"];
            if (v >= 0 && v <= 2) {
                wcfg.apitype = static_cast<Eng::tool::RenderAPItype>(v);
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

    std::string path = Eng::tool::GetWritablePath("settings.json");
    Eng::tool::WriteStringToFile(path, j.dump(4));
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
            m_client = std::make_unique<Eng::tool::MemoryClientTransport>(server->GetMemoryPipe());
            m_client->Connect("", 0);
            logInfo(logger, "[Game] Started memory transport for single player");
        } else {
            m_client = std::make_unique<Eng::tool::NetworkClientTransport>();
            m_client->Connect("127.0.0.1", 25565);
        }
    }

    m_server = std::move(server);
    return true;
}

bool MyGame::ConnectToServer(const std::string& host, uint16_t port) {
    m_client = std::make_unique<Eng::tool::NetworkClientTransport>();
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
    Eng::tool::WindowConfig windowConfig = {
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
        Eng::tool::Input::Get().ProcessEvent(e);
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

    Eng::tool::Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), false);
    m_relativeMode = false;
    return true;
}

void MyGame::RecreateFBOs(int w, int h) {
    if (w <= 0 || h <= 0) return;
    Eng::service::rhi::FramebufferCreateInfo info{
        .width = w, 
        .height = h, 
        .hasDepth = true, 
        .colorFormat = Eng::service::rhi::TextureFormat::RGBA8_UNORM
    };
    m_mainFbo = renderer->CreateFramebuffer(info);
}

bool MyGame::SetupRenderer(Eng::Engine& engine) {
    if (!engine.CreateRenderService()) {
        logError(logger, "[Game] Engine failed to create render service");
        return false;
    }
    renderer = engine.GetRenderService();
    if (!renderer) {
        logError(logger, "[Game] renderer is null");
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

    // 从 Engine 拿 AssetService（生命周期归 Engine）
    m_assetManager = engine.GetAssetService();
    if (!m_assetManager) {
        logError(logger, "[Game] AssetService is null");
        return false;
    }

    // 创建 SceneRenderer（构造会加载 'mesh' shader；失败抛异常）
    try {
        m_sceneRenderer = std::make_unique<Eng::client::SceneRenderer>(renderer, m_assetManager);
    } catch (const std::exception& e) {
        logError(logger, "[Game] SceneRenderer init failed: " << e.what());
        return false;
    }

    m_playerTexture = m_assetManager->LoadTexture("assets/textures/entity/player.png");

    const auto& pixels = client::AtlasPixels();
    int atlasW = client::AtlasCols() * client::ATLAS_TILE;
    int atlasH = client::AtlasRows() * client::ATLAS_TILE;
    Eng::service::rhi::TextureCreateInfo tci;
    tci.pixels = std::as_bytes(std::span(pixels));
    tci.width = atlasW;
    tci.height = atlasH;
    tci.format = Eng::service::rhi::TextureFormat::RGBA8_UNORM;
    // 给地形图集配置正确的采样器
    tci.requestedMips = 0; // 让引擎自动生成 Mipmap
    tci.magFilter = Eng::service::rhi::FilterMode::Nearest;                     // 近处保持方块像素风
    tci.minFilter = Eng::service::rhi::MinFilterMode::Nearest_Mipmap_Linear;    // 远处平滑过渡防摩尔纹
    tci.wrapU = Eng::service::rhi::WrapMode::ClampToEdge;
    tci.wrapV = Eng::service::rhi::WrapMode::ClampToEdge;
    tci.enableAnisotropy = true; // 倾斜看水面时用各向异性过滤
    m_atlasTexture = renderer->CreateTexture(tci);

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

    // 创建 GlobalState UBO 并一次性绑定到 set 0 / binding 0
    m_globalStateUBO = renderer->CreateBuffer({
        .size  = sizeof(GlobalState),
        .usage = Eng::service::rhi::BufferUsage::Uniform
    });
    if (!m_globalStateUBO) {
        logError(logger, "[Game] Failed to create GlobalState UBO");
        return false;
    }
    renderer->BindUniformBuffer(m_globalStateUBO, 0, 0);

    return true;
}

void MyGame::SetupShaders() {
    m_cubeShader = m_assetManager->LoadShader(
        m_assetManager->MakeShaderCreateInfo("mesh"));

    m_lodShader = m_assetManager->LoadShader(
        m_assetManager->MakeShaderCreateInfo("lod"));

    m_skyShader = m_assetManager->LoadShader(
        m_assetManager->MakeShaderCreateInfo("sky",
            Eng::service::rhi::GraphicsPipelineState::PresetSkybox(),
            Eng::service::rhi::VertexLayout::Skybox));

    m_entityShader = m_assetManager->LoadShader(
        m_assetManager->MakeShaderCreateInfo("entity",
            Eng::service::rhi::GraphicsPipelineState::PresetOpaque(),
            Eng::service::rhi::VertexLayout::Standard,
            sizeof(Eng::service::rhi::PerObjectData)));

    m_handShader = m_assetManager->LoadShader(
        m_assetManager->MakeShaderCreateInfo("entity",
            Eng::service::rhi::GraphicsPipelineState::PresetHand(),
            Eng::service::rhi::VertexLayout::Standard,
            sizeof(Eng::service::rhi::PerObjectData)));

    if (!m_cubeShader || !m_skyShader || !m_lodShader || !m_entityShader || !m_handShader) {
        std::string err = "核心着色器加载失败！请检查 assets/shaders/build/ 目录下是否存在对应的 SPIR-V (.spv) 文件。";
        logFatal(logger, "[MyGame] " << err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shader Load Error", err.c_str(), nullptr);
        throw std::runtime_error(err);
    }
}

void MyGame::EnterGame() {
    logInfo(logger, "[Game] Entering game session...");

    // ==================== 1. 后台线程 ====================
    m_meshManager.Start();

    // ==================== 2. 玩家物理与镜头 ====================
    cameraPos        = glm::vec3(0.0f, 40.0f, 0.0f);
    cameraFront      = glm::vec3(0.0f, 0.0f, -1.0f);
    yaw              = -90.0f;
    pitch            = 0.0f;
    m_playerVelocity = glm::vec3(0.0f);
    m_positionError  = glm::vec3(0.0f);
    m_lastMoveDir    = glm::vec3(0.0f);
    m_playerOnGround = false;
    
    // ==================== 3. 时间与动画 ====================
    m_timeOfDay      = 8.0f;
    m_animTime       = 0.0f;
    m_walkDist       = 0.0f;
    m_swingProgress  = 0.0f;

    // ==================== 4. 交互冷却与计时器 ====================
    m_jumpHoldTimer  = 0.0f;
    m_placeCooldown  = 0.0f;
    m_digCooldown    = 0.0f;

    // ==================== 5. FPS 与性能统计 ====================
    m_fpsTimer       = 0.0f;
    m_fpsFrameCount  = 0;
    m_fpsAccumMs     = 0.0f;
    m_fpsPeakMs      = 0.0f;
    m_fpsDisplay     = 0.0f;
    m_frameMsDisplay = 0.0f;
    m_frameMsAvgDisp = 0.0f;
    m_frameMsPeakDisp= 0.0f;
    m_serverTickMs   = 0.0f;
    m_serverTickAvg  = 0.0f;
    m_serverTickMax  = 0.0f;

    // ==================== 6. 背包与物品栏 ====================
    m_hotbarIndex    = 0;
    m_inventoryOpen  = false;

    // ==================== 7. 清理网络与实体数据 ====================
    m_itemEntities.clear();
    m_otherPlayers.clear();
    m_myClientId = 0;
    
    if (m_sceneRenderer) {
        m_sceneRenderer->Clear();
    }
    m_knownSceneChunks.clear();

    // ==================== 8. 切换状态 ====================
    m_state = GameState::Playing;
    m_menuOpen = false;
    m_statusText.clear();
    
    logInfo(logger, "[Game] Game session entered successfully.");
}

void MyGame::OnStart(Eng::Engine& engine) {
    if (!logger) logger = std::make_unique<Eng::tool::Log>();

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
        Eng::tool::Input::Get().SetRelativeMode(mainWin->GetSDLWindow(), wantRelative);
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
            Eng::tool::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkRequest));
            w.Write<int32_t>(cx);
            w.Write<int32_t>(cz);
            m_client->Send(w.GetBuffer());
        },
        // 卸载时拦截，存给 LOD
        [this](int cx, int cz, const game::client::ClientChunk& chunk) {
            m_lodManager.StoreRealChunk(cx, cz, chunk);
        }
    );

    for (auto it = m_knownSceneChunks.begin(); it != m_knownSceneChunks.end(); ) {
        if (m_world.GetChunk(*it) == nullptr) {
            if (m_sceneRenderer) m_sceneRenderer->OnChunkUnloaded(*it);
            it = m_knownSceneChunks.erase(it);
        } else {
            ++it;
        }
    }

    // 每帧最多提取 8 个脏区块去建 Mesh，不一次性全塞进去
    auto dirtyKeys = m_world.FetchDirtyChunks(8); 
    for (uint64_t key : dirtyKeys) {
        auto* cc = m_world.GetChunk(key);
        if (cc) QueueChunkMeshTask(key);
    }

    int actualLodDistance = m_enableLod ? m_lodRenderDistance : 0;
    m_lodManager.Update(cameraPos, m_world, m_renderDistance, actualLodDistance, renderer);
    
    // 每帧最多从结果队列拿 16 个出来给 GPU，不把显存撑爆
    DrainMeshResults(16); 

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
        projection = renderer->MakePerspectiveMatrix(
            glm::radians(engine.GetConfig().render.fov),
            mainWin->GetAspectRatio(),
            engine.GetConfig().render.zNear,
            engine.GetConfig().render.zFar);
    }

    m_globalState.view       = view;
    m_globalState.projection = projection;
    m_globalState.viewPos    = cameraPos;
    m_globalState.aoStrength = engine.GetConfig().render.m_aoStrength;

    renderer->UpdateBuffer(
        m_globalStateUBO, 0,
        std::as_bytes(std::span<const GlobalState>(&m_globalState, 1)));

    const glm::vec4 clearColor = (m_state == GameState::Playing)
        ? glm::vec4(0.5f, 0.7f, 1.0f, 1.0f)
        : glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    renderer->Clear(Eng::service::rhi::ClearFlag::All, clearColor);

    renderer->BeginFrame();

    if (m_state == GameState::Playing) {
        // ★ 1. 统一在这一帧计算一次视锥体
        glm::mat4 viewProj = projection * view;
        Eng::client::Frustum frameFrustum;
        frameFrustum.UpdatePlanes(viewProj);

        m_sceneRenderer->BeginScene();

        // 2. LOD（走 MDI 超级合批，不经过 SceneRenderer 的队列）
        // 1) 根据视锥体剔除并生成 Indirect 指令
        m_lodManager.PrepareDraw(renderer, frameFrustum);
        // 2) 直接向底层发送 1 次 DrawIndexedIndirect 绘制全部远景
        m_lodManager.Draw(renderer, m_cubeShader, m_atlasTexture);

        // 3. 其他玩家
        if (!m_playerMesh.vbo) m_playerMesh = BuildPlayerMesh();
        for (auto& [id, ps] : m_otherPlayers) {
            Eng::service::rhi::PerObjectData data;
            data.model = glm::translate(glm::mat4(1.0f), ps.position);
            data.model = glm::rotate(data.model, ps.yaw, glm::vec3(0, 1, 0));
            m_sceneRenderer->SubmitMesh(m_playerMesh, m_entityShader, m_playerTexture, data);
        }

        // 4. 掉落物
        if (!m_itemEntities.empty()) {
            std::unordered_map<BlockId, std::vector<Eng::service::rhi::PerObjectData>> instances;
            for (const auto& [id, e] : m_itemEntities) {
                Eng::service::rhi::PerObjectData data;
                data.model = glm::translate(glm::mat4(1.0f), e.renderPos);
                data.model = glm::rotate(data.model, m_animTime * 2.0f, glm::vec3(0, 1, 0));
                data.model = glm::translate(data.model, glm::vec3(0.0f, std::sin(m_animTime * 3.0f + e.id * 0.5f) * 0.1f + 0.1f, 0.0f));
                data.model = glm::scale(data.model, glm::vec3(0.25f));
                instances[e.itemType].push_back(data);
            }

            for (const auto& [type, perObjects] : instances) {
                const auto& mesh = GetOrCreateItemMesh(type);
                if (mesh.vbo) {
                    m_sceneRenderer->SubmitInstancedMesh(mesh, m_entityShader, m_atlasTexture, perObjects);
                }
            }
        }

        // 5. 天空盒
        if (!m_skyboxMesh.vbo) m_skyboxMesh = BuildSkyboxMesh();
        m_sceneRenderer->SubmitMesh(m_skyboxMesh, m_skyShader, m_atlasTexture);

        // 6. 第一人称手部
        if (!m_handMesh.vbo) m_handMesh = BuildHandMesh();

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
            m_sceneRenderer->SubmitMesh(m_handMesh, m_handShader, m_playerTexture, { .model = handModel });

            auto& curSlot = m_hotbar[m_hotbarIndex];
            if (!curSlot.isEmpty()) {
                const auto& itemMesh = GetOrCreateItemMesh(curSlot.type);
                if (itemMesh.vbo) {
                    glm::mat4 itemLocalModel = handLocalModel;
                    itemLocalModel = glm::translate(itemLocalModel, glm::vec3(0.0f, 0.2f, -0.5f));
                    itemLocalModel = glm::scale(itemLocalModel, glm::vec3(0.5f));
                    glm::mat4 itemModel = glm::inverse(view) * itemLocalModel;
                    m_sceneRenderer->SubmitMesh(itemMesh, m_handShader, m_atlasTexture, { .model = itemModel });
                }
            }
        }

        // 7. 把计算好的 frameFrustum 传给 EndScene
        m_sceneRenderer->EndScene(frameFrustum, m_cubeShader, m_atlasTexture);
    }

    m_ui.Draw(engine, *this);
    renderer->EndFrame();
}

void MyGame::ExitGame() {
    logInfo(logger, "[Game] Exiting game session...");

    // 保证 GPU 处理完毕
    if (renderer) renderer->WaitIdle();

    // ==================== 1. 停掉后台线程 ====================
    m_lodManager.Stop();
    m_meshManager.Stop();

    // ==================== 2. 断开网络 ====================
    if (m_client) m_client.reset();
    if (m_server) { m_server->Stop(); m_server.reset(); }

    // ==================== 3. 清理世界数据 ====================
    m_world.Clear();
    m_lodManager.Clear();
    m_itemEntities.clear();
    m_otherPlayers.clear();
    
    // ==================== 4. 清理渲染动态资源 ====================
    m_playerMesh = {};
    m_handMesh = {};
    m_skyboxMesh = {};
    m_itemMeshCache.clear();

    if (m_sceneRenderer) {
        m_sceneRenderer->Clear();
    }
    m_knownSceneChunks.clear();

    // ==================== 5. 切换回主菜单 ====================
    m_state = GameState::MainMenu;
    m_menuOpen = false;
    m_statusText.clear();

    logInfo(logger, "[Game] Game session exited.");
}

void MyGame::ReturnToMainMenu() {
    ExitGame();
    logInfo(logger, "[Game] Returned to main menu");
}

void MyGame::OnShutdown(Eng::Engine& /*engine*/) {
    // 统一通过 ExitGame 清理所有会话级资源
    ExitGame();

    // 只有引擎级别的清理留在这里
    m_globalStateUBO.reset();
    m_mainFbo.reset();
    m_skyShader.reset();
    m_lodShader.reset();
    m_entityShader.reset();
    m_handShader.reset();
    m_cubeShader.reset();
    m_atlasTexture.reset();
    m_playerTexture.reset();
    m_sceneRenderer.reset();

    m_assetManager = nullptr;
    renderer       = nullptr;
    mainWin        = nullptr;
    winMgr         = nullptr;
    m_win          = 0;
}

// ============================================================
// 输入
// ============================================================
bool MyGame::GetInput(float dt) {
    using namespace Eng::tool;

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

    float scroll = Input::GetScrollDelta();
    if (scroll > 0) m_hotbarIndex = (m_hotbarIndex - 1 + kHotbarSize) % kHotbarSize;
    if (scroll < 0) m_hotbarIndex = (m_hotbarIndex + 1) % kHotbarSize;

    if (Input::IsKeyPressed(KeyCode::Num1)) m_hotbarIndex = 0;
    if (Input::IsKeyPressed(KeyCode::Num2)) m_hotbarIndex = 1;
    if (Input::IsKeyPressed(KeyCode::Num3)) m_hotbarIndex = 2;
    if (Input::IsKeyPressed(KeyCode::Num4)) m_hotbarIndex = 3;

    if (m_hotbarIndex < 0 || m_hotbarIndex >= kHotbarSize) m_hotbarIndex = 0;

    // —— 拾取/挖掘/放置 ——
    bool wantDig   = Input::IsKeyDown(KeyCode::MouseLeft);
    bool wantPlace = Input::IsKeyDown(KeyCode::MouseRight);

    // ★ 1. 本地射线检测（距离 5 格）
    glm::vec3 eyePos = cameraPos + glm::vec3(0.0f, PLAYER_EYE, 0.0f);
    m_currentRayHit = game::RayCast(
        m_blockRegistry, 
        eyePos, 
        cameraFront, 
        5.0f, 
        [this](int wx, int wy, int wz) { return this->GetBlockAt(wx, wy, wz); }
    );

    input.dig   = (wantDig   && m_digCooldown   <= 0.0f);
    input.place = (wantPlace && m_placeCooldown <= 0.0f);

    if (input.dig)   m_digCooldown   = kDigInterval;
    if (input.place) m_placeCooldown = kPlaceInterval;

    // 原有逻辑：扣除物品和设置 input.placeBlock
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

    Eng::tool::MessageWriter w;
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
    Eng::tool::MessageReader r(std::span<const uint8_t>(data).subspan(1));

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
            std::span<const BlockId> blockSpan(reinterpret_cast<const BlockId*>(bytes.data()), CHUNK_VOLUME);

            auto skyBytes = r.ReadBytes(CHUNK_VOLUME);
            std::array<uint8_t, CHUNK_VOLUME> skyData;
            std::memcpy(skyData.data(), skyBytes.data(), CHUNK_VOLUME);

            auto blockLightBytes = r.ReadBytes(CHUNK_VOLUME * sizeof(LightRGB));
            std::array<LightRGB, CHUNK_VOLUME> blockLightData;
            std::memcpy(blockLightData.data(), blockLightBytes.data(), CHUNK_VOLUME * sizeof(LightRGB));

            uint64_t key = ChunkKey(cx, cz);
            m_world.AddChunk(cx, cz, blockSpan, skyData, blockLightData);

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
const Eng::client::RenderMesh& MyGame::GetOrCreateItemMesh(BlockId id) {
    auto it = m_itemMeshCache.find(id);
    if (it != m_itemMeshCache.end()) return it->second;

    auto res = m_itemMeshCache.emplace(id, BuildItemCubeMesh(id));
    return res.first->second;
}

Eng::client::RenderMesh MyGame::BuildItemCubeMesh(BlockId id) {
    game::client::RawMeshData data = client::MeshBuilderUtils::BuildItemMeshData(m_blockRegistry, id);
    Eng::client::RenderMesh mesh;
    if (!data.vertices.empty()) {
        mesh.vbo = renderer->CreateBuffer({ .size = data.vertices.size() * sizeof(Eng::service::Vertex), .usage = Eng::service::rhi::BufferUsage::Vertex });
        renderer->UpdateBuffer(mesh.vbo, 0, std::as_bytes(std::span(data.vertices)));
        mesh.ibo = renderer->CreateBuffer({ .size = data.indices.size() * sizeof(uint32_t), .usage = Eng::service::rhi::BufferUsage::Index });
        renderer->UpdateBuffer(mesh.ibo, 0, std::as_bytes(std::span(data.indices)));
        mesh.indexCount = static_cast<uint32_t>(data.indices.size());
        mesh.vertexCount = static_cast<uint32_t>(data.vertices.size());
        mesh.indexed = true;
    }
    return mesh;
}

Eng::client::RenderMesh MyGame::BuildPlayerMesh() {
    std::vector<Eng::service::Vertex> vertices;
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

    Eng::client::RenderMesh mesh;
    mesh.vbo = renderer->CreateBuffer({ .size = vertices.size() * sizeof(Eng::service::Vertex), .usage = Eng::service::rhi::BufferUsage::Vertex });
    renderer->UpdateBuffer(mesh.vbo, 0, std::as_bytes(std::span(vertices)));
    mesh.ibo = renderer->CreateBuffer({ .size = indices.size() * sizeof(uint32_t), .usage = Eng::service::rhi::BufferUsage::Index });
    renderer->UpdateBuffer(mesh.ibo, 0, std::as_bytes(std::span(indices)));
    mesh.indexCount = static_cast<uint32_t>(indices.size());
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexed = true;
    return mesh;
}

Eng::client::RenderMesh MyGame::BuildHandMesh() {
    std::vector<Eng::service::Vertex> vertices;
    std::vector<uint32_t> indices;

    glm::vec4 ra_top(44,16,4,4), ra_bot(48,16,4,4), ra_right(40,20,4,12), ra_left(48,20,4,12), ra_front(44,20,4,12), ra_back(52,20,4,12);
    AppendPlayerBox({0.0f, 0.0f, 0.0f}, {0.25f, 0.25f, 0.6f}, ra_top, ra_bot, ra_right, ra_left, ra_front, ra_back, vertices, indices);

    Eng::client::RenderMesh mesh;
    mesh.vbo = renderer->CreateBuffer({ .size = vertices.size() * sizeof(Eng::service::Vertex), .usage = Eng::service::rhi::BufferUsage::Vertex });
    renderer->UpdateBuffer(mesh.vbo, 0, std::as_bytes(std::span(vertices)));
    mesh.ibo = renderer->CreateBuffer({ .size = indices.size() * sizeof(uint32_t), .usage = Eng::service::rhi::BufferUsage::Index });
    renderer->UpdateBuffer(mesh.ibo, 0, std::as_bytes(std::span(indices)));
    mesh.indexCount = static_cast<uint32_t>(indices.size());
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexed = true;
    return mesh;
}

Eng::client::RenderMesh MyGame::BuildSkyboxMesh() {
    const float s = 1.0f;
    float verts[] = {
         s,-s,-s,  s,-s, s,  s, s, s,   s, s, s,  s, s,-s,  s,-s,-s,
        -s,-s, s, -s,-s,-s, -s, s,-s,  -s, s,-s, -s, s, s, -s,-s, s,
        -s, s,-s,  s, s,-s,  s, s, s,   s, s, s, -s, s, s, -s, s,-s,
        -s,-s, s,  s,-s, s,  s,-s,-s,   s,-s,-s, -s,-s,-s, -s,-s, s,
        -s,-s, s,  s,-s, s,  s, s, s,   s, s, s, -s, s, s, -s,-s, s,
         s,-s,-s, -s,-s,-s, -s, s,-s,  -s, s,-s,  s, s,-s,  s,-s,-s,
    };
    std::vector<Eng::service::Vertex> v(36);
    for (size_t i = 0; i < 36; ++i) {
        v[i].position = { verts[i*3], verts[i*3+1], verts[i*3+2] };
        v[i].normal   = { 0, 0, 0 };
        v[i].uv       = { 0, 0 };
        v[i].ao       = 1.0f;
        v[i].light    = glm::vec4(1.0f);
    }

    Eng::client::RenderMesh mesh;
    mesh.vbo = renderer->CreateBuffer({ .size = v.size() * sizeof(Eng::service::Vertex), .usage = Eng::service::rhi::BufferUsage::Vertex });
    renderer->UpdateBuffer(mesh.vbo, 0, std::as_bytes(std::span(v)));
    mesh.vertexCount = static_cast<uint32_t>(v.size());
    mesh.indexed = false;
    return mesh;
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

void MyGame::DrainMeshResults(size_t maxCount) {
    if (!m_sceneRenderer) return;

    size_t processed = 0;
    m_meshManager.DrainResults([this, &processed, maxCount](game::client::MeshResult&& r) {
        if (processed >= maxCount) return; // 达到本帧上限就丢弃（底层其实还在队列里等下一帧）
        processed++;

        auto* cc = m_world.GetChunk(r.chunkKey);
        if (!cc) return;

        bool hasOpaque = !r.vertices.empty() && !r.indices.empty();
        bool hasWater  = !r.waterVertices.empty() && !r.waterIndices.empty();

        if (hasOpaque) {
            m_sceneRenderer->OnChunkMeshReady(
                r.chunkKey,
                std::move(r.vertices),
                std::move(r.indices),
                cc->aabbMin,
                cc->aabbMax);
            
            m_knownSceneChunks.insert(r.chunkKey);
        } else {
            m_sceneRenderer->OnChunkUnloaded(r.chunkKey);
            m_knownSceneChunks.erase(r.chunkKey);
        }

        if (hasWater) {
            m_sceneRenderer->SubmitWaterChunk(
                r.chunkKey,
                std::move(r.waterVertices),
                std::move(r.waterIndices),
                cc->aabbMin,
                cc->aabbMax);
        } else {
            m_sceneRenderer->RemoveWaterChunk(r.chunkKey);
        }
    });
}

} // namespace game