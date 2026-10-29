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
#include <cmath>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace game {

namespace {
    void AppendBoxToMesh(glm::vec3 c, glm::vec3 size, int tile,
                        std::vector<Eng::client::Vertex>& vertices,
                        std::vector<uint32_t>& indices) {
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
                // 直接覆盖：旧 fbo 在赋值瞬间析构，无需手动 Destroy
                if (renderer) {
                    fbo = renderer->CreateFramebuffer({
                        .width  = e.window.data1,
                        .height = e.window.data2
                    });
                }
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

    static const ImWchar allGlyphs[] = {
        0x0020, 0xFFFF,
        0
    };

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;

    float scaledFontSize = 16.0f * m_uiScale;

    ImFont* unifont = io.Fonts->AddFontFromFileTTF(
        "./assets/fonts/unifont-18.0.01.otf",
        scaledFontSize,
        &fontConfig,
        allGlyphs
    );

    if (!unifont) {
        logError(logger, "[Game] Failed to load Unifont, falling back to default font.");
        io.Fonts->AddFontDefault();
    } else {
        logInfo(logger, "[Game] Loaded Unifont with all BMP glyphs at size " << scaledFontSize);
    }

    // —— 图集纹理（TexturePtr）——
    auto pixels = client::GenerateAtlasPixels();
    m_atlasTexture = renderer->CreateTexture(pixels.data(), client::ATLAS_W, client::ATLAS_H);
    if (!m_atlasTexture) {
        logError(logger, "[MyGame] Failed to create atlas texture");
        return false;
    }
    logInfo(logger, "[MyGame] atlas texture created");

    // —— FBO（FramebufferPtr）——
    fbo = renderer->CreateFramebuffer({
        .width  = mainWin->GetConfigs().windowWidth,
        .height = mainWin->GetConfigs().windowHeight
    });
    if (!fbo || !fbo->GetColorTexture()) {
        logError(logger, "[Game] FBO creation failed!");
        return false;
    }

    return true;
}

// ============================================================
// Shader
// ============================================================
void MyGame::SetupShaders() {
    m_cubeShader = CreateShaderByName("mesh", false);
    m_skyShader  = CreateShaderByName("sky",  true);
    m_lodShader  = CreateShaderByName("lod",  false);
}

Eng::client::ShaderPtr MyGame::CreateShaderByName(const std::string& name, bool isSky) {
    const std::string ext  = m_useVulkan ? ".vk.spv" : ".gl.spv";
    const std::string base = "./assets/shaders/build/" + name + "/";

    Eng::client::ShaderCreateInfo ci;
    ci.vertPath = base + "vert" + ext;
    ci.fragPath = base + "frag" + ext;
    ci.isSky    = isSky;

    if (isSky) {
        // 天空盒：不写深度，避免污染后续手部渲染
        ci.depthTest    = true;
        ci.depthWrite   = false;
        ci.blendEnabled = false;
    } else {
        // 地形 / LOD / 手部：标准深度配置
        ci.depthTest    = true;
        ci.depthWrite   = true;
        ci.blendEnabled = false;
    }
    return renderer->CreateShader(ci);
}

// ============================================================
// OnStart
// ============================================================
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

    m_meshManager.Start();

    m_state = GameState::MainMenu;
}

// ============================================================
// OnUpdate
// ============================================================
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

    // —— 时间 & 光照（写入 GlobalState，OnRender 之前生效）——
    float hoursPerSec = 24.0f / kDayLengthSec;
    m_timeOfDay = std::fmod(m_timeOfDay + hoursPerSec * deltaTime, 24.0f);

    float angle = (m_timeOfDay / 24.0f) * 6.2831853f - 1.5707963f;
    glm::vec3 sunDir = glm::normalize(glm::vec3(cos(angle), sin(angle), 0.3f));
    float sunH = sunDir.y;

    float sunI = glm::clamp(sunH * 4.0f, 0.0f, 1.0f);
    float warm = 1.0f - glm::clamp(sunH * 3.0f, 0.0f, 1.0f);
    glm::vec3 sunColor = glm::mix(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.5f, 0.2f), warm);
    glm::vec3 ambient  = glm::mix(glm::vec3(0.10f, 0.12f, 0.20f), glm::vec3(0.55f, 0.60f, 0.70f),
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
    // 注意：新版 UpdateStreaming 不再需要 destroy 回调，ClientWorld 内部靠 MeshPtr 自动释放
    m_world.UpdateStreaming(cameraPos, m_renderDistance,
        [this](int cx, int cz) {
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkRequest));
            w.Write<int32_t>(cx);
            w.Write<int32_t>(cz);
            m_client->Send(w.GetBuffer());
        }
    );

    auto dirtyKeys = m_world.FetchDirtyChunks(2);
    for (uint64_t key : dirtyKeys) {
        auto* cc = m_world.GetChunk(key);
        if (cc) QueueChunkMeshTask(key);
    }

    m_lodManager.Update(cameraPos, m_world, renderer);
    DrainMeshResults();

    return false;
}

// ============================================================
// OnRender
// ============================================================
void MyGame::OnRender(Eng::Engine& engine) {
    if (m_minimized) return;

    if (m_applyPending) {
        renderer->ApplySettings(mainWin->GetConfigs(), engine.GetConfig().render);
        m_applyPending = false;
    }

    // 1) 计算 view / projection（仅 Playing 状态更新）
    if (m_state == GameState::Playing) {
        glm::vec3 eye = cameraPos + glm::vec3(0.0f, PLAYER_EYE, 0.0f);
        view = glm::lookAt(eye, eye + cameraFront, cameraUp);
        projection = glm::perspective(glm::radians(engine.GetConfig().render.fov),
                                mainWin->GetAspectRatio(),
                                engine.GetConfig().render.zNear,
                                engine.GetConfig().render.zFar);
        UpdateFrustum(projection * view);
    }

    // 2) 组装 GlobalState（必须在 BeginFrame 之前）
    m_globalState.view        = view;
    m_globalState.projection  = projection;
    m_globalState.viewPos     = cameraPos;
    m_globalState.aoStrength  = engine.GetConfig().render.m_aoStrength;
    renderer->SetGlobalState(m_globalState);

    // 3) 帧开始
    renderer->BeginFrame();

    // ==========================================
    // Pass 1: 主世界 → FBO
    // ==========================================
    renderer->BindFramebuffer(fbo);
    renderer->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    renderer->Clear();

    if (m_state == GameState::Playing) {
        Eng::client::Material mat; mat.diffuse = m_atlasTexture;

        m_world.CollectVisibleMeshes(m_frustum, m_visibleChunkMeshes);
        if (!m_visibleChunkMeshes.empty())
            renderer->DrawMeshBatch(m_visibleChunkMeshes, m_cubeShader, mat);

        Eng::client::Material lodMat; lodMat.diffuse = m_atlasTexture;
        m_lodManager.CollectVisibleMeshes(m_frustum, m_visibleLodMeshes);
        if (!m_visibleLodMeshes.empty())
            renderer->DrawMeshBatch(m_visibleLodMeshes, m_lodShader, lodMat);

        // 其他玩家
        if (!m_playerMesh) m_playerMesh = BuildPlayerMesh();
        Eng::client::Material playerMat; playerMat.diffuse = m_atlasTexture;
        for (auto& [id, ps] : m_otherPlayers) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), ps.position);
            model = glm::rotate(model, ps.yaw, glm::vec3(0, 1, 0));
            renderer->DrawMesh(m_playerMesh, m_cubeShader, playerMat, model);
        }

        // 实例化渲染掉落物
        if (!m_itemEntities.empty()) {
            std::unordered_map<BlockType, std::vector<glm::mat4>> instances;
            for (const auto& [id, e] : m_itemEntities) {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), e.renderPos);
                // 缓慢旋转
                model = glm::rotate(model, m_animTime * 2.0f, glm::vec3(0, 1, 0));
                // 上下浮动 + 一个基础偏移（防止贴着地面）
                model = glm::translate(model, glm::vec3(0.0f, std::sin(m_animTime * 3.0f + e.id * 0.5f) * 0.1f + 0.1f, 0.0f));
                // 掉落物缩放
                model = glm::scale(model, glm::vec3(0.25f));
                
                instances[e.itemType].push_back(model);
            }

            Eng::client::Material itemMat; itemMat.diffuse = m_atlasTexture;
            for (const auto& [type, transforms] : instances) {
                auto mesh = GetOrCreateItemMesh(type);
                if (mesh) {
                    renderer->DrawMeshInstanced(mesh, m_cubeShader, itemMat, transforms);
                }
            }
        }

        // 天空盒
        renderer->DrawSkybox(m_skyShader);

        // 手部
        renderer->Clear(Eng::client::ClearFlag::Depth);
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
        handLocalModel = glm::rotate(handLocalModel, glm::radians(15.0f), glm::vec3(0, 1, 0));
        handLocalModel = glm::rotate(handLocalModel, glm::radians(-20.0f), glm::vec3(1, 0, 0));
        handLocalModel = glm::scale(handLocalModel, glm::vec3(0.5f));

        // inverse(view) * localModel：把手从相机空间拉回世界空间，绕开 Vulkan Y 翻转
        glm::mat4 handModel = glm::inverse(view) * handLocalModel;

        Eng::client::Material handMat; handMat.diffuse = m_atlasTexture;
        renderer->DrawMesh(m_handMesh, m_cubeShader, handMat, handModel);

        // 手持物品
        auto& curSlot = m_hotbar[m_hotbarIndex];
        if (!curSlot.isEmpty()) {
            auto itemMesh = GetOrCreateItemMesh(curSlot.type);
            if (itemMesh) {
                glm::mat4 itemLocalModel = handLocalModel;
                itemLocalModel = glm::translate(itemLocalModel, glm::vec3(0.0f, 0.2f, -0.5f));
                itemLocalModel = glm::scale(itemLocalModel, glm::vec3(0.5f));
                glm::mat4 itemModel = glm::inverse(view) * itemLocalModel;
                renderer->DrawMesh(itemMesh, m_cubeShader, handMat, itemModel);
            }
        }
    }

    renderer->UnbindFramebuffer();

    // ==========================================
    // Pass 2: 后处理 & UI → 屏幕
    // ==========================================
    renderer->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    renderer->Clear(Eng::client::ClearFlag::Color | Eng::client::ClearFlag::Depth);

    renderer->DrawFullscreenQuad(fbo->GetColorTexture());

    m_ui.Draw(engine, *this);

    renderer->EndFrame();
}

// ============================================================
// 返回主菜单
// ============================================================
void MyGame::ReturnToMainMenu() {
    if (renderer) renderer->WaitIdle();

    if (m_client) m_client.reset();
    if (m_server) { m_server->Stop(); m_server.reset(); }

    m_lodManager.Stop();
    m_meshManager.Stop();

    // 智能指针自动释放，无需手动 Destroy
    m_playerMesh.reset();
    m_handMesh.reset();
    m_itemMeshCache.clear();

    m_world.Clear();     // ClientWorld 内部 reset 所有 chunk mesh
    m_itemEntities.clear();
    m_otherPlayers.clear();
    m_myClientId = 0;

    m_lodManager.Clear();   // 若 LodManager 已内部改为 MeshPtr，可去掉 renderer 参数

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

// ============================================================
// OnShutdown
// ============================================================
void MyGame::OnShutdown(Eng::Engine& /*engine*/) {
    if (m_client) { m_client->Disconnect(); m_client.reset(); }
    if (m_server) { m_server->Stop(); m_server.reset(); }

    auto* safeRenderer = mainWin ? mainWin->GetAPI() : nullptr;
    if (safeRenderer) {
        m_lodManager.Stop();
        m_lodManager.Clear();
        safeRenderer->ShutdownImGuiBackend();
    }

    // 必须在 renderer 销毁前，强制清理所有 MeshPtr
    // 否则 MyGame 析构时会触发悬空指针的 DestroyMeshInternal
    m_visibleChunkMeshes.clear();
    m_visibleLodMeshes.clear();
    m_playerMesh.reset();
    m_handMesh.reset();
    m_itemMeshCache.clear();
    m_world.Clear();         // 清理 ClientWorld 内的 chunk->mesh
    m_itemEntities.clear();
    m_otherPlayers.clear();
    m_meshManager.Stop();

    // 清理其他资源
    m_cubeShader.reset();
    m_skyShader.reset();
    m_lodShader.reset();
    m_atlasTexture.reset();
    fbo.reset();

    renderer = nullptr;
    mainWin = nullptr;
}

// ============================================================
// 输入
// ============================================================
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

        // 直接覆盖：旧 FBO 在赋值瞬间析构
        fbo = renderer->CreateFramebuffer({
            .width  = mainWin->GetConfigs().windowPixelWidth,
            .height = mainWin->GetConfigs().windowPixelHeight
        });
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
    if (glm::length(moveDir) > 0.001f) moveDir = glm::normalize(moveDir);

    game::net::PlayerInput input;
    input.moveDir = moveDir;
    m_lastMoveDir = moveDir;
    input.look = glm::vec2(glm::radians(yaw), glm::radians(pitch));

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

    if (m_hotbarIndex < 0 || m_hotbarIndex >= kHotbarSize) m_hotbarIndex = 0;

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

            auto bytes = r.ReadBytes(CHUNK_VOLUME);
            std::vector<uint8_t> blockData(bytes.begin(), bytes.end());

            uint64_t key = ChunkKey(cx, cz);
            m_world.AddChunk(cx, cz, blockData, 0);

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

// ============================================================
// Mesh 构建
// ============================================================
Eng::client::MeshPtr MyGame::GetOrCreateItemMesh(BlockType type) {
    auto it = m_itemMeshCache.find(type);
    if (it != m_itemMeshCache.end()) return it->second;

    auto mesh = BuildItemCubeMesh(type);
    m_itemMeshCache[type] = mesh;
    return mesh;
}

Eng::client::MeshPtr MyGame::BuildItemCubeMesh(BlockType type) {
    Eng::client::MeshData data = client::MeshBuilderUtils::BuildItemMeshData(type);
    return renderer->CreateMesh(data);
}

Eng::client::MeshPtr MyGame::BuildPlayerMesh() {
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t> indices;

    AppendBoxToMesh({0.0f, 1.55f, 0.0f}, {0.5f, 0.5f, 0.5f}, 13, vertices, indices);       // 头
    AppendBoxToMesh({0.0f, 1.05f, 0.0f}, {0.5f, 0.5f, 0.25f}, 14, vertices, indices);      // 身体
    AppendBoxToMesh({-0.375f, 1.05f, 0.0f}, {0.25f, 0.5f, 0.25f}, 14, vertices, indices);  // 左臂
    AppendBoxToMesh({ 0.375f, 1.05f, 0.0f}, {0.25f, 0.5f, 0.25f}, 14, vertices, indices);  // 右臂
    AppendBoxToMesh({-0.125f, 0.4f, 0.0f}, {0.25f, 0.8f, 0.25f}, 15, vertices, indices);   // 左腿
    AppendBoxToMesh({ 0.125f, 0.4f, 0.0f}, {0.25f, 0.8f, 0.25f}, 15, vertices, indices);   // 右腿

    Eng::client::MeshData data;
    data.vertices = std::move(vertices);
    data.indices  = std::move(indices);
    return renderer->CreateMesh(data);
}

Eng::client::MeshPtr MyGame::BuildHandMesh() {
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t> indices;

    AppendBoxToMesh({0.0f, 0.0f, 0.0f}, {0.25f, 0.25f, 0.6f}, 14, vertices, indices);
    AppendBoxToMesh({0.0f, 0.0f, -0.35f}, {0.25f, 0.25f, 0.2f}, 13, vertices, indices);

    Eng::client::MeshData data;
    data.vertices = std::move(vertices);
    data.indices  = std::move(indices);
    return renderer->CreateMesh(data);
}

// ============================================================
// 世界查询
// ============================================================
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

// ============================================================
// 背包
// ============================================================
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

// ============================================================
// Mesh 结果汇聚
// ============================================================
void MyGame::DrainMeshResults() {
    m_meshManager.DrainResults([this](game::client::MeshResult&& r) {
        auto* cc = m_world.GetChunk(r.chunkKey);
        if (!cc || !cc->chunk) return;

        cc->chunk->sky   = r.sky;
        cc->chunk->block = r.block;

        if (r.vertices.empty()) {
            cc->mesh.reset();
        } else {
            Eng::client::MeshData data;
            data.vertices = std::move(r.vertices);
            data.indices  = std::move(r.indices);
            cc->mesh = renderer->CreateMesh(data);
        }
    });
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

    m_meshManager.Submit(
        key,
        pC->chunk,
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