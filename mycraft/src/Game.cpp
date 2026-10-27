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
#include "game/core/LightEngine.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_messagebox.h>
#include <algorithm>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

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

    // 用字符串画出 16x16 的图案 (0=透明, 1=茎, 2=花瓣)
    static const char* flowerPattern[16] = {
        "................",
        "................",
        "................",
        ".....222222.....",
        "....22222222....",
        "....22222222....",
        "....22222222....",
        ".....222222.....",
        "......11........",
        "......11........",
        "......11........",
        "......11........",
        "......11........",
        "......11........",
        "......11........",
        "......11........",
    };

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

                if (t == 2 || t == 7) {   // 草侧面 / 雪侧面
                    bool top = (y < 4) || (y == 4 && ((x + HashNoise(x, 0, 99)) % 3 != 0));
                    if (!top) { r = 110 + n; g = 75 + n; b = 50 + n; }   // 下半土色
                }
                if (t == 11) {
                    if ((x + HashNoise(0, y, 42)) % 4 == 0) {
                        r = 110 + n; g = 70 + n; b = 30 + n;   // 深色条纹
                    }
                }

                int i = ((oy + y) * ATLAS_W + (ox + x)) * 4;

                // ================= 处理透明植物 =================
                if (t >= 16) {
                    px[i + 0] = 0; px[i + 1] = 0; px[i + 2] = 0; px[i + 3] = 0; // 默认透明

                    if (t == 16) { 
                        // 从底部开始长，保证根部连着地，越往上越稀疏且带点弯曲
                        if (y >= 4) {
                            // 用 x 和 (15-y) 作为噪声，生成类似植物生长的弯曲感
                            bool isBlade = (HashNoise(x * 3, 15 - y, 42) % 4 == 0);
                            // 强制在底部生成几根主根
                            if (y > 12 && (x == 4 || x == 7 || x == 11)) isBlade = true;

                            if (isBlade) {
                                px[i + 0] = 88; px[i + 1] = 160; px[i + 2] = 70; px[i + 3] = 255;
                            }
                        }
                    } else if (t == 17 || t == 18) { 
                        // 使用上面定义的字符画图案
                        char p = flowerPattern[y][x];
                        if (p == '1') { // 茎
                            px[i + 0] = 42; px[i + 1] = 100; px[i + 2] = 45; px[i + 3] = 255;
                        } else if (p == '2') { // 花瓣
                            px[i + 0] = 220; 
                            px[i + 1] = (t == 17) ? 40 : 200; 
                            px[i + 2] = 40; 
                            px[i + 3] = 255;
                        }
                    }
                    continue; // 跳过下面的默认不透明赋值
                }
                // =================================================

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

// ==========================================
// UI 辅助函数
// ==========================================
// 绘制带黑色阴影的文字
void DrawTextWithShadow(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text) {
    dl->AddText(ImVec2(pos.x + 2, pos.y + 2), IM_COL32(30, 30, 30, 255), text); // 阴影
    dl->AddText(pos, color, text);                                              // 正文
}

// 绘制 MC 风格的按钮
bool DrawButton(const char* label, ImVec2 size) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 使用 InvisibleButton 接管点击，不负责渲染
    bool clicked = ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered();

    // 按钮背景色（悬停时变亮）
    ImU32 bgColor = hovered ? IM_COL32(120, 120, 120, 255) : IM_COL32(100, 100, 100, 255);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor);

    // 画双层边框（黑底，悬停时内边框变白，平时是深灰）
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, 255), 0.0f, 0, 2.0f);
    ImU32 innerColor = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 80, 80, 255);
    dl->AddRect(ImVec2(pos.x + 1, pos.y + 1), ImVec2(pos.x + size.x - 1, pos.y + size.y - 1), innerColor, 0.0f, 0, 2.0f);

    // 文字居中并带阴影
    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 textPos = ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f);
    DrawTextWithShadow(dl, textPos, IM_COL32(255, 255, 255, 255), label);

    return clicked;
}

// 绘制旋转文字（用于 Splash Text）
void DrawRotatedText(ImDrawList* dl, ImVec2 center, const char* text, float angleRad, ImU32 color) {
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 pos = ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
    
    // 记录当前的顶点索引
    int vtxStart = dl->VtxBuffer.Size;
    
    // 先正常画一遍带阴影的字
    dl->AddText(ImVec2(pos.x + 2, pos.y + 2), IM_COL32(30, 30, 30, 255), text);
    dl->AddText(pos, color, text);
    
    int vtxEnd = dl->VtxBuffer.Size;

    // 遍历这几个字的顶点，围绕中心点做旋转
    float cosA = std::cos(angleRad);
    float sinA = std::sin(angleRad);

    for (int i = vtxStart; i < vtxEnd; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        float dx = v.pos.x - center.x;
        float dy = v.pos.y - center.y;
        v.pos.x = center.x + (dx * cosA - dy * sinA);
        v.pos.y = center.y + (dx * sinA + dy * cosA);
    }
}

} // namespace

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
        
        // ★ 改这里
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
    // ★ 改这里：使用 window.apitype
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
    // 注意：如果底层探测失败，CreateWindow 会 throw，异常会被 OnStart 的 catch 接住
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
    // 设置 ImGui 极简风格
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;     // 去掉窗口圆角
    style.FrameRounding = 0.0f;      // 去掉按钮/输入框圆角
    style.ScrollbarRounding = 0.0f;
    style.WindowBorderSize = 0.0f;   // 去掉边框
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(10, 10);
    
    // 配色（深色 + 青色强调）
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]       = ImVec4(0.08f, 0.08f, 0.09f, 0.95f); // 深灰半透明
    colors[ImGuiCol_ChildBg]        = ImVec4(0.12f, 0.12f, 0.13f, 0.60f); // 列表底色稍亮
    colors[ImGuiCol_Text]           = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_CheckMark]      = ImVec4(0.20f, 0.80f, 0.80f, 1.00f); // 青色勾选
    colors[ImGuiCol_SliderGrab]     = ImVec4(0.20f, 0.80f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_Header]         = ImVec4(0.20f, 0.80f, 0.80f, 0.20f); // 选中项背景
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.20f, 0.80f, 0.80f, 0.30f);

    // 获取系统的 DPI 缩放比例
    float dpiScale = SDL_GetWindowDisplayScale(mainWin->GetSDLWindow());
    if (dpiScale <= 0.0f) dpiScale = 1.0f;
    m_uiScale = dpiScale;

    // 让 ImGui 全局缩放
    style.ScaleAllSizes(m_uiScale); // 自动放大所有边距、圆角、滚动条宽度
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = m_uiScale; // 自动放大字体

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

    // ============ Dedicated server 模式 ============
    if (engine.IsDedicatedServer()) {
        if (!StartLocalServer(false, false)) {
            logError(logger, "[Game] Failed to start dedicated server");
            m_shouldQuit = true;
            return;
        }
        logInfo(logger, "[Game] Running as dedicated server");
        return;
    }

    // ============ 交互模式 ============
    
    // 1. 读取持久化配置 (包含图形API偏好)
    LoadSettings(engine);

    // 2. 建窗口和初始化渲染器 (完全交给引擎内部去探测和降级)
    try {
        // SetupWindow 内部如果探测失败且无降级空间，会抛出异常
        if (!SetupWindow(engine))   return;
        
        // 检查引擎是否发生了降级（从 Window 里读原因）
        if (!mainWin->GetFallbackReason().empty()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, 
                "图形 API 降级", mainWin->GetFallbackReason().c_str(), nullptr);
        }
    } catch (const std::exception& e) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, 
            "无法启动渲染器", e.what(), nullptr);
        m_shouldQuit = true;
        return;
    }

    // 3. 初始化渲染器
    if (!SetupRenderer(engine)) return;
    SetupShaders();

    StartMeshWorker();

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

    // 如果渲染距离变了，清空旧的缓存强制重新计算
    static int s_lastRenderDistance = m_renderDistance;
    if (m_renderDistance != s_lastRenderDistance) {
        s_lastRenderDistance = m_renderDistance;
        
        // 强制让 LOD 和区块流式加载重新评估
        m_lastLodPcx = INT_MIN;
        m_lastLodPcz = INT_MIN;
        m_lastPlayerChunkX = INT_MIN;
        m_lastPlayerChunkZ = INT_MIN;
        
        // 如果你想立刻卸载超过新半径的区块，可以在这里触发 m_pending 的清理
    }

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
    DrainMeshResults();

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
        // ---- 近景 chunk：视锥剔除 + 合批 ----
        m_visibleChunkMeshes.clear();
        m_visibleChunkMeshes.reserve(m_chunks.size());
        for (auto& [key, cc] : m_chunks) {
            if (cc.mesh == 0) continue;

            const int cx = cc.chunk.chunk_x;
            const int cz = cc.chunk.chunk_z;
            const glm::vec3 mn(static_cast<float>(cx * CHUNK_SIZE_X), 0.0f,
                               static_cast<float>(cz * CHUNK_SIZE_Z));
            const glm::vec3 mx = mn + glm::vec3(static_cast<float>(CHUNK_SIZE_X),
                                                static_cast<float>(CHUNK_SIZE_Y),
                                                static_cast<float>(CHUNK_SIZE_Z));
            if (!AabbInFrustum(mn, mx)) continue;

            m_visibleChunkMeshes.push_back(cc.mesh);
        }
        if (!m_visibleChunkMeshes.empty()) {
            // model 已经是 identity，batch 里会设一次
            renderer->DrawMeshBatch(m_visibleChunkMeshes, m_cubeShader, mat);
        }

        // ---- 远景 LOD：已有剔除，收集后合批 ----
        Eng::client::Material lodMat;
        lodMat.diffuse = m_atlasTexture;

        m_visibleLodMeshes.clear();
        m_visibleLodMeshes.reserve(m_lodUnits.size());
        for (auto& [key, u] : m_lodUnits) {
            if (u.mesh == 0) continue;
            if (!AabbInFrustum(u.aabbMin, u.aabbMax)) continue;
            m_visibleLodMeshes.push_back(u.mesh);
        }
        if (!m_visibleLodMeshes.empty()) {
            renderer->DrawMeshBatch(m_visibleLodMeshes, m_lodShader, lodMat);
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

        // ---- 实体（掉落物） ---- 
        Eng::client::Material itemMat;
        itemMat.diffuse = m_atlasTexture;

        // Q 弹动画计算
        constexpr float BOUNCE_FREQ = 1.0f / 0.4f;
        constexpr float BOUNCE_AMP  = 0.45f;
        constexpr float XZ_RATIO    = 0.8f;
        float t = m_animTime * BOUNCE_FREQ * 6.2831853f;
        float sY  = 1.0f + BOUNCE_AMP * std::sin(t);
        float sXZ = 1.0f - BOUNCE_AMP * XZ_RATIO * std::sin(t);

        // 按类型分组：unordered_map<BlockType, std::vector<glm::mat4>>
        std::unordered_map<BlockType, std::vector<glm::mat4>> itemTransforms;
        itemTransforms.reserve(m_itemEntities.size());

        for (auto& [id, e] : m_itemEntities) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, e.renderPos);
            model = model * glm::rotate(glm::mat4(1.0f), m_animTime * 2.0f, glm::vec3(0, 1, 0));
            model = glm::scale(model, glm::vec3(sXZ * 0.3f, sY * 0.3f, sXZ * 0.3f));

            itemTransforms[e.itemType].push_back(model);
        }

        // 按类型进行实例化绘制
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
    
    // ============ Pass 2: 后处理 → 屏幕 ============
    renderer->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    renderer->Clear();

        renderer->DrawFullscreenQuad(renderer->GetFramebufferTexture(fbo));

        DrawUI(engine);

    renderer->EndFrame();
}

// ==========================================
// 主菜单渲染
// ==========================================
void MyGame::DrawMainMenu(Eng::Engine& /*engine*/) {
    ImGuiIO& io = ImGui::GetIO();
    
    // ★ 修复：使用 BackgroundDrawList 防止遮挡弹窗
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 160));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##MainMenuRoot", nullptr, 
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
        ImGuiWindowFlags_NoBackground);

    float menuWidth = 400.0f;
    float buttonHeight = 40.0f;
    float startX = (io.DisplaySize.x - menuWidth) * 0.5f;
    float startY = io.DisplaySize.y * 0.3f;

    ImGui::SetWindowFontScale(3.0f);
    const char* title = "MYCRAFT";
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImVec2 titlePos = ImVec2((io.DisplaySize.x - titleSize.x * 3.0f) * 0.5f, startY - 120);
    DrawTextWithShadow(ImGui::GetWindowDrawList(), titlePos, IM_COL32(200, 200, 200, 255), title);
    ImGui::SetWindowFontScale(1.0f);

    const char* splash = "Only in Mycraft!";
    DrawRotatedText(ImGui::GetWindowDrawList(), ImVec2(startX + menuWidth - 50, startY - 80), splash, -0.3f, IM_COL32(255, 255, 0, 255));

    ImGui::SetCursorScreenPos(ImVec2(startX, startY));
    if (DrawButton("Single Player", ImVec2(menuWidth, buttonHeight))) {
        if (StartLocalServer(true, true)) {
            m_state = GameState::Playing;
            m_statusText.clear();
        } else {
            m_statusText = "Failed to start local server";
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(startX, startY + 50));
    if (DrawButton("Multiplayer", ImVec2(menuWidth, buttonHeight))) {
        m_showMultiplayerDialog = true;
    }

    ImGui::SetCursorScreenPos(ImVec2(startX, startY + 110));
    if (DrawButton("Options...", ImVec2(menuWidth * 0.48f, buttonHeight))) {
        m_menuOpen = true;
    }
    ImGui::SetCursorScreenPos(ImVec2(startX + menuWidth * 0.52f, startY + 110));
    if (DrawButton("Quit Game", ImVec2(menuWidth * 0.48f, buttonHeight))) {
        m_shouldQuit = true;
    }

    // 底部信息
    ImGui::GetWindowDrawList()->AddText(ImVec2(10, io.DisplaySize.y - 30), IM_COL32(200, 200, 200, 255), "Mycraft v0.0.1");
    const char* copyright = "Copyright Mycraft Team. Open Source Project.";
    ImVec2 copySize = ImGui::CalcTextSize(copyright);
    ImGui::GetWindowDrawList()->AddText(ImVec2(io.DisplaySize.x - copySize.x - 10, io.DisplaySize.y - 30), IM_COL32(200, 200, 200, 255), copyright);

    if (!m_statusText.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(startX, startY + 170));
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_statusText.c_str());
    }
    ImGui::End();

    // ---- 多人连接对话框 ----
    if (m_showMultiplayerDialog) ImGui::OpenPopup("Connect to Server");
    
    // ★ 修复：强制弹窗居中
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Connect to Server", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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

void MyGame::DrawSettingsMenu(Eng::Engine& engine) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.09f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.22f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.28f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.17f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.20f, 0.80f, 0.80f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.20f, 0.80f, 0.80f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.30f, 0.90f, 0.90f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.80f, 0.80f, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.80f, 0.80f, 0.30f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##SettingsRoot", nullptr, 
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | 
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

    // --- 顶部栏 ---
    ImGui::SetCursorPos(ImVec2(20, 20));
    ImGui::SetNextItemWidth(300);
    static char searchBuf[128] = "";
    ImGui::InputTextWithHint("##Search", "Search options...", searchBuf, sizeof(searchBuf));

    ImGui::SameLine(io.DisplaySize.x - 50);
    if (ImGui::Button("X", ImVec2(30, 30))) {
        m_menuOpen = false;
    }

    ImGui::Separator();
    ImGui::Spacing();

    float leftWidth = 250.0f;
    float contentHeight = io.DisplaySize.y - 140.0f;

    // ================= 左侧导航 =================
    ImGui::BeginChild("##LeftPanel", ImVec2(leftWidth, contentHeight), true, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Spacing();
        if (m_settingsTab == 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.8f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        if (ImGui::Selectable("    Video", m_settingsTab == 0)) m_settingsTab = 0;
        ImGui::PopStyleColor();

        if (m_settingsTab == 1) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.8f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        if (ImGui::Selectable("    Audio", m_settingsTab == 1)) m_settingsTab = 1;
        ImGui::PopStyleColor();

        if (m_settingsTab == 2) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.8f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        if (ImGui::Selectable("    Controls", m_settingsTab == 2)) m_settingsTab = 2;
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ================= 右侧面板 =================
    ImGui::BeginChild("##RightPanel", ImVec2(0, contentHeight), true, ImGuiWindowFlags_NoScrollbar);
    {
        auto& wcfg = mainWin->GetConfigs();
        auto& rcfg = engine.GetConfig().render;

        if (m_settingsTab == 0) {
            ImGui::Spacing();
            ImGui::Text("Render Distance"); ImGui::SameLine(leftWidth * 1.2f);
            ImGui::SetNextItemWidth(150);
            ImGui::SliderInt("##rd", &m_renderDistance, 2, 32, "%d chunks");

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Fullscreen Mode"); ImGui::SameLine(leftWidth * 1.2f);
            bool fs = wcfg.fullscreen;
            if (ImGui::Checkbox("##fs", &fs)) {
                wcfg.fullscreen = fs;
                mainWin->SetFullscreen(fs);
                renderer->WaitIdle();
                fbo = renderer->CreateFramebuffer(
                    mainWin->GetConfigs().windowPixelWidth,
                    mainWin->GetConfigs().windowPixelHeight);
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Vertical Sync"); ImGui::SameLine(leftWidth * 1.2f);
            if (ImGui::Checkbox("##vsync", &wcfg.vsync)) {
                m_applyPending = true;
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Ambient Occlusion"); ImGui::SameLine(leftWidth * 1.2f);
            bool ao = rcfg.m_aoStrength > 0.5f;
            if (ImGui::Checkbox("##ao", &ao)) {
                rcfg.m_aoStrength = ao ? 1.0f : 0.0f;
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Field of View"); ImGui::SameLine(leftWidth * 1.2f);
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("##fov", &rcfg.fov, 30.0f, 120.0f, "%.0f");

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // ================= 图形 API 设置 =================
            const char* apiDesc = "选择渲染使用的图形API。除非有特殊需求需要更改，否则请维持此设置为默认不变！";
            ImVec4 descColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); 

            // ★ 改为 wcfg.apitype 和 RenderAPItype
            if (wcfg.apitype == Eng::client::RenderAPItype::VULKAN) {
                apiDesc = "实验性设置。在部分系统上使用可能会降低性能或导致不稳定。";
                descColor = ImVec4(1.0f, 0.6f, 0.2f, 1.0f); // 橙色警示
            }

            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(descColor, "%s", apiDesc);
            ImGui::PopTextWrapPos();
            ImGui::Spacing();

            ImGui::Text("图形 API"); 
            ImGui::SameLine(leftWidth * 1.2f);
            ImGui::SetNextItemWidth(200.0f);
            
            int currentApi = static_cast<int>(wcfg.apitype);
            const char* apiItems[] = { "默认", "优先使用 OpenGL", "优先使用 Vulkan" };
            if (ImGui::Combo("##api", &currentApi, apiItems, IM_ARRAYSIZE(apiItems))) {
                wcfg.apitype = static_cast<Eng::client::RenderAPItype>(currentApi);
                m_apiChanged = true; // 记录修改
            }

            if (m_apiChanged) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "需要重启游戏以应用新的图形 API！");
            }
            // =======================================================

        } else if (m_settingsTab == 1) {
            ImGui::Spacing();
            ImGui::TextDisabled("Audio settings coming soon...");
        } else if (m_settingsTab == 2) {
            ImGui::Spacing();
            ImGui::TextDisabled("Controls settings coming soon...");
        }
    }
    ImGui::EndChild();

    // ================= 底部按钮区域 =================
    float bottomY = io.DisplaySize.y - 60.0f;

    // 左下角：自适应宽度的按钮
    ImGui::SetCursorPos(ImVec2(20.0f, bottomY));
    
    const char* leftBtnText = (m_state == GameState::Playing) ? "Save & Quit to Title" : "Back to Menu";
    float leftBtnWidth = ImGui::CalcTextSize(leftBtnText).x + 40.0f;
    if (leftBtnWidth < 150.0f) leftBtnWidth = 150.0f; 

    if (ImGui::Button(leftBtnText, ImVec2(leftBtnWidth, 40))) {
        if (m_state == GameState::Playing) {
            m_returnToMenuPending = true;
        } else {
            m_menuOpen = false;
        }
    }

    // 右下角：Apply / Done (动态宽度 + 整体右对齐)
    const char* btnApplyText = "Apply";
    const char* btnDoneText = "Done";
    
    float applyWidth = ImGui::CalcTextSize(btnApplyText).x + 40.0f;
    float doneWidth  = ImGui::CalcTextSize(btnDoneText).x + 40.0f;
    if (applyWidth < 100.0f) applyWidth = 100.0f;
    if (doneWidth < 100.0f)  doneWidth = 100.0f;

    float btnSpacing = ImGui::GetStyle().ItemSpacing.x;
    float totalRightWidth = applyWidth + btnSpacing + doneWidth;

    float rightStartX = io.DisplaySize.x - 20.0f - totalRightWidth;
    
    ImGui::SetCursorPos(ImVec2(rightStartX, bottomY));
    if (ImGui::Button(btnApplyText, ImVec2(applyWidth, 40))) {
        m_applyPending = true;
        SaveSettings(engine);
    }
    ImGui::SameLine();
    if (ImGui::Button(btnDoneText, ImVec2(doneWidth, 40))) {
        m_applyPending = true;
        m_menuOpen = false;
        SaveSettings(engine);
    }

    ImGui::End();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(11);
}

void MyGame::DrawUI(Eng::Engine& engine) {
    renderer->ImGuiNewFrame();
    ImGui::NewFrame();

    // 全局暗色遮罩：只在主菜单或打开设置菜单时出现
    if (m_menuOpen || m_state == GameState::MainMenu) {
        ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
        bgDrawList->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(10, 10, 10, 160));
    }

    // 1. 非菜单状态：渲染游戏 HUD 或 主菜单
    if (!m_menuOpen) {
        if (m_state == GameState::MainMenu) {
            DrawMainMenu(engine);
        } else if (m_state == GameState::Playing) {
            ImGuiIO& io = ImGui::GetIO(); // ★ 统一获取一次，下面所有的块都能用
            
            // ---- 准心 ----
            {
                ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
                ImDrawList* dl = ImGui::GetForegroundDrawList();

                constexpr float LEN    = 8.0f;
                constexpr float THICK  = 2.0f;
                constexpr float GAP    = 2.0f;
                ImU32 color = IM_COL32(255, 255, 255, 200);

                dl->AddLine(ImVec2(center.x - GAP - LEN, center.y), ImVec2(center.x - GAP, center.y), color, THICK);
                dl->AddLine(ImVec2(center.x + GAP, center.y), ImVec2(center.x + GAP + LEN, center.y), color, THICK);
                dl->AddLine(ImVec2(center.x, center.y - GAP - LEN), ImVec2(center.x, center.y - GAP), color, THICK);
                dl->AddLine(ImVec2(center.x, center.y + GAP), ImVec2(center.x, center.y + GAP + LEN), color, THICK);
            }

            // ---- 物品栏 ----
            {
                constexpr float SLOT = 50.0f;
                constexpr float GAP  = 4.0f;
                float totalW = kHotbarSize * SLOT + (kHotbarSize - 1) * GAP;
                float startX = (io.DisplaySize.x - totalW) * 0.5f;
                float y      = io.DisplaySize.y - SLOT - 20.0f;

                ImDrawList* dl = ImGui::GetForegroundDrawList();
                for (int i = 0; i < kHotbarSize; ++i) {
                    float x = startX + i * (SLOT + GAP);
                    ImVec2 p0(x, y), p1(x + SLOT, y + SLOT);

                    ImU32 bg = (i == m_hotbarIndex) ? IM_COL32(255, 255, 255, 200) : IM_COL32(0, 0, 0, 120);
                    dl->AddRectFilled(p0, p1, bg);
                    dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 0, 0, 2.0f);

                    const auto& slot = m_hotbar[i];
                    if (slot.isEmpty()) continue;

                    dl->AddText(ImVec2(x + 4, y + SLOT - 18), IM_COL32(255, 255, 255, 255), BlockName(slot.type));

                    if (slot.count > 1) {
                        char cnt[8];
                        std::snprintf(cnt, sizeof(cnt), "%d", slot.count);
                        dl->AddText(ImVec2(x + SLOT - 20, y + 4), IM_COL32(255, 255, 255, 255), cnt);
                    }
                }
            }

            // ---- 调试面板 ----
            if (m_showDebug) {
                ImDrawList* dl = ImGui::GetForegroundDrawList();
                
                // 辅助函数：左侧对齐，带单行暗底
                auto DrawLineBgLeft = [&](float x, float y, ImU32 color, const char* fmt, ...) {
                    char buf[512];
                    va_list args; va_start(args, fmt);
                    vsnprintf(buf, sizeof(buf), fmt, args);
                    va_end(args);

                    ImVec2 textSize = ImGui::CalcTextSize(buf);
                    float padX = 4.0f;
                    float padY = 1.0f;

                    // 1. 画黑色半透明背景
                    dl->AddRectFilled(ImVec2(x - padX, y - padY), 
                                      ImVec2(x + textSize.x + padX, y + textSize.y + padY), 
                                      IM_COL32(0, 0, 0, 130));
                    // 2. 画文字
                    dl->AddText(ImVec2(x, y), color, buf);
                };

                // 辅助函数：右侧对齐，带单行暗底
                auto DrawLineBgRight = [&](float rightX, float y, ImU32 color, const char* fmt, ...) {
                    char buf[512];
                    va_list args; va_start(args, fmt);
                    vsnprintf(buf, sizeof(buf), fmt, args);
                    va_end(args);

                    ImVec2 textSize = ImGui::CalcTextSize(buf);
                    float x = rightX - textSize.x;
                    float padX = 4.0f;
                    float padY = 1.0f;

                    // 1. 画黑色半透明背景
                    dl->AddRectFilled(ImVec2(x - padX, y - padY), 
                                      ImVec2(rightX + padX, y + textSize.y + padY), 
                                      IM_COL32(0, 0, 0, 130));
                    // 2. 画文字
                    dl->AddText(ImVec2(x, y), color, buf);
                };

                float y = 10.0f;
                // 稍微拉开一点行距，防止上下两行的黑底黏在一起变成一大坨
                float lineHeight = ImGui::GetTextLineHeight() + 4.0f; 
                ImU32 white = IM_COL32(255, 255, 255, 255);
                ImU32 green = IM_COL32(85, 255, 85, 255);

                // ================= 左侧信息 =================
                float leftX = 14.0f; // 稍微留出点左边界
                
                DrawLineBgLeft(leftX, y, green, "Mycraft v0.0.1"); y += lineHeight;
                DrawLineBgLeft(leftX, y, white, "%.0f fps | Frame: %.2f ms", m_fpsDisplay, m_frameMsDisplay); y += lineHeight;
                DrawLineBgLeft(leftX, y, white, "Server tick: %.2f ms", m_serverTickMs); y += lineHeight;
                
                y += lineHeight * 0.5f; // 组与组之间额外空半行

                DrawLineBgLeft(leftX, y, white, "XYZ: %.3f / %.3f / %.3f", cameraPos.x, cameraPos.y, cameraPos.z); y += lineHeight;
                DrawLineBgLeft(leftX, y, white, "Yaw/Pitch: %.1f / %.1f", yaw, pitch); y += lineHeight;
                
                int pcx = static_cast<int>(std::floor(cameraPos.x)) >> 4;
                int pcz = static_cast<int>(std::floor(cameraPos.z)) >> 4;
                DrawLineBgLeft(leftX, y, white, "Chunk: %d %d", pcx, pcz); y += lineHeight;
                
                y += lineHeight * 0.5f;

                DrawLineBgLeft(leftX, y, white, "Chunks loaded: %zu", m_chunks.size()); y += lineHeight;
                DrawLineBgLeft(leftX, y, white, "Pending: %zu", m_pending.size()); y += lineHeight;
                DrawLineBgLeft(leftX, y, white, "Render Distance: %d chunks", m_renderDistance); y += lineHeight;
                DrawLineBgLeft(leftX, y, white, "Player ID: %d", m_myClientId); y += lineHeight;
                
                y += lineHeight * 0.5f;

                int hh = (int)m_timeOfDay;
                int mm = (int)((m_timeOfDay - hh) * 60);
                DrawLineBgLeft(leftX, y, white, "Time: %02d:%02d", hh, mm);

                // ================= 右侧信息 =================
                float rightX = io.DisplaySize.x - 14.0f; // 距离右边缘 14 像素
                y = 10.0f;

                DrawLineBgRight(rightX, y, white, "Backend: %s", m_deviceInfo.backend.c_str()); y += lineHeight;
                DrawLineBgRight(rightX, y, white, "GPU: %s", m_deviceInfo.deviceName.c_str()); y += lineHeight;
                DrawLineBgRight(rightX, y, white, "Vendor: %s", m_deviceInfo.vendor.c_str()); y += lineHeight;
                DrawLineBgRight(rightX, y, white, "API: %s", m_deviceInfo.apiVersion.c_str()); y += lineHeight;
                DrawLineBgRight(rightX, y, white, "Driver: %s", m_deviceInfo.driverVersion.c_str()); y += lineHeight;
                DrawLineBgRight(rightX, y, white, "Shader: %s", m_deviceInfo.shadingLanguage.c_str());
            }
        }
    }

    // 2. 菜单打开状态：独占全屏设置界面
    if (m_menuOpen) {
        DrawSettingsMenu(engine);
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

    // 2. 先停 worker
    StopMeshWorker();
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
        StopMeshWorker();
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
        case net::MessageType::PlayerId: {
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

            QueueChunkMeshTask(key, m_chunks[key].chunk);

            m_pending.erase(ChunkKey(cx, cz));
            break;
        }
        case game::net::MessageType::BlockChange: {
            int bx = r.Read<int32_t>();
            int by = r.Read<int32_t>();
            int bz = r.Read<int32_t>();
            auto t = static_cast<BlockType>(r.Read<uint16_t>());

            int cx = bx >> 4, cz = bz >> 4;
            int lx = bx & 15, lz = bz & 15;

            auto it = m_chunks.find(ChunkKey(cx, cz));
            if (it == m_chunks.end()) break;

            BlockType oldBlock = ChunkGet(it->second.chunk, lx, by, lz);
            if (oldBlock == t) break; // 没变化就跳过

            ChunkSet(it->second.chunk, lx, by, lz, t);

            QueueChunkMeshTask(ChunkKey(cx, cz), it->second.chunk);

            if (lx == 0 || lx == 15 || lz == 0 || lz == 15) {
                auto rebuild = [&](int ncx, int ncz) {
                    auto nit = m_chunks.find(ChunkKey(ncx, ncz));
                    if (nit == m_chunks.end()) return;
                    
                    // 注意：目前没有做“跨 Chunk 的光照 BFS 传播”，
                    // 所以邻居 Chunk 的光照数据暂时无需重算。等以后支持跨边界光照时，
                    // 这里也要加上 game::LightEngine::OnBlockChanged(nit->second.chunk, ...)
                    
                    QueueChunkMeshTask(ChunkKey(ncx, ncz), nit->second.chunk);
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
            vert.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
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

void MyGame::BuildChunkMeshData(MeshTask& task, MeshResult& result) {
    result.vertices.clear();
    result.indices.clear();
    result.vertices.reserve(static_cast<size_t>(2000) * 4);
    result.indices.reserve(static_cast<size_t>(2000) * 6);

    // 1. 统一获取指针（可修改），避免后续重复定义
    Chunk* pC = &task.chunk;
    Chunk* pXm = task.hasN[0] ? &task.nXm : nullptr;
    Chunk* pXp = task.hasN[1] ? &task.nXp : nullptr;
    Chunk* pZm = task.hasN[2] ? &task.nZm : nullptr;
    Chunk* pZp = task.hasN[3] ? &task.nZp : nullptr;

    // 2. 统一初始化和传播光照
    LightEngine::InitRegion(*pC, pXm, pXp, pZm, pZp);

    // 3. 将光照结果带回主线程
    result.sky = pC->sky;
    result.block = pC->block;

    const Chunk& chunk = *pC; // 复用指针
    const int cx = chunk.chunk_x;
    const int cz = chunk.chunk_z;
    const int baseX = cx * CHUNK_SIZE_X;
    const int baseZ = cz * CHUNK_SIZE_Z;

    // 4. 获取方块
    auto getBlock = [&](int lx, int ly, int lz) -> BlockType {
        if (ly < 0 || ly >= CHUNK_SIZE_Y) return BlockType::Air;
        if (lx >= 0 && lx < CHUNK_SIZE_X && lz >= 0 && lz < CHUNK_SIZE_Z)
            return ChunkGet(chunk, lx, ly, lz);
        
        const Chunk* n = nullptr;
        int nlx = lx, nlz = lz;
        if      (lx < 0)             { n = pXm; nlx = lx + CHUNK_SIZE_X; }
        else if (lx >= CHUNK_SIZE_X) { n = pXp; nlx = lx - CHUNK_SIZE_X; }
        else if (lz < 0)             { n = pZm; nlz = lz + CHUNK_SIZE_Z; }
        else if (lz >= CHUNK_SIZE_Z) { n = pZp; nlz = lz - CHUNK_SIZE_Z; }
        
        if (!n) return BlockType::Air;
        if (nlx < 0 || nlx >= CHUNK_SIZE_X || nlz < 0 || nlz >= CHUNK_SIZE_Z) return BlockType::Air;
        return ChunkGet(*n, nlx, ly, nlz);
    };

    auto isSolidAt = [&](int lx, int ly, int lz) -> bool {
        BlockType b = getBlock(lx, ly, lz);
        return b != BlockType::Air && b != BlockType::Water && b != BlockType::Void &&
               b != BlockType::TallGrass && b != BlockType::FlowerRed && b != BlockType::FlowerYellow;
    };

    static constexpr int faceOffsets[6][4][3] = {
        {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},
        {{0,0,1},{0,1,1},{0,1,0},{0,0,0}},
        {{0,1,1},{1,1,1},{1,1,0},{0,1,0}},
        {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
        {{1,0,1},{1,1,1},{0,1,1},{0,0,1}},
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

                if (type == BlockType::Air || type == BlockType::Void) continue;

                // 十字交叉植物
                if (type == BlockType::TallGrass || type == BlockType::FlowerRed || type == BlockType::FlowerYellow) {
                    int tile = TileForBlock(type, 0);
                    float u0 = (tile % ATLAS_COLS) * TILE_UV;
                    float v0 = (tile / static_cast<float>(ATLAS_COLS)) * TILE_UV;
                    float u1 = u0 + TILE_UV;
                    float v1 = v0 + TILE_UV;
                    float x0 = baseX + lx + 0.15f;
                    float x1 = baseX + lx + 0.85f;
                    float z0 = baseZ + lz + 0.15f;
                    float z1 = baseZ + lz + 0.85f;
                    float y0 = (float)ly;
                    float y1 = (float)ly + 1.0f;

                    auto addQuad = [&](glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, glm::vec3 p4) {
                        uint32_t baseIndex = (uint32_t)result.vertices.size();
                        Eng::client::Vertex v;
                        v.ao = 1.0f;
                        v.normal = {0, 1, 0};
                        v.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                        v.position = p1; v.uv = {u0, v1}; result.vertices.push_back(v);
                        v.position = p2; v.uv = {u1, v1}; result.vertices.push_back(v);
                        v.position = p3; v.uv = {u1, v0}; result.vertices.push_back(v);
                        v.position = p4; v.uv = {u0, v0}; result.vertices.push_back(v);
                        result.indices.push_back(baseIndex + 0); result.indices.push_back(baseIndex + 1); result.indices.push_back(baseIndex + 2);
                        result.indices.push_back(baseIndex + 2); result.indices.push_back(baseIndex + 3); result.indices.push_back(baseIndex + 0);
                        result.indices.push_back(baseIndex + 0); result.indices.push_back(baseIndex + 2); result.indices.push_back(baseIndex + 1);
                        result.indices.push_back(baseIndex + 3); result.indices.push_back(baseIndex + 2); result.indices.push_back(baseIndex + 0);
                    };
                    addQuad({x0, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z0});
                    addQuad({x0, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z1});
                    continue; 
                }

                // 6 个面
                for (int f = 0; f < 6; ++f) {
                    int nx = lx + faceNormals[f][0];
                    int ny = ly + faceNormals[f][1];
                    int nz = lz + faceNormals[f][2];

                    BlockType neighbor = getBlock(nx, ny, nz);
                    
                    if (IsSolid(neighbor)) continue;
                    if (type == BlockType::Water && neighbor == BlockType::Water) continue;

                    int N_axis = (faceNormals[f][0] != 0) ? 0 : (faceNormals[f][1] != 0) ? 1 : 2;
                    int U_axis = (N_axis + 1) % 3;
                    int V_axis = (N_axis + 2) % 3;

                    glm::ivec3 N(faceNormals[f][0], faceNormals[f][1], faceNormals[f][2]);

                    uint32_t baseIndex = (uint32_t)result.vertices.size();

                    for (int v = 0; v < 4; ++v) {
                        Eng::client::Vertex vert;
                        
                        // 水面高度 0.9f
                        float yPos = (float)ly + faceOffsets[f][v][1];
                        if (type == BlockType::Water && faceOffsets[f][v][1] == 1) {
                            yPos = (float)ly + 0.9f; 
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

                        vert.ao = (s1 && s2) ? 0.4f : 1.0f - 0.2f * static_cast<float>(s1 + s2 + sc);
                        if (type == BlockType::Water) vert.ao = 1.0f;

                                                // 获取光照（如果越界且邻居没加载，默认给满天空光防黑边）
                        auto getLightAt = [&](int wx, int wy, int wz, glm::vec4& outLight) -> void {
                            if (wy < 0 || wy >= CHUNK_SIZE_Y) { outLight = glm::vec4(0.0f); return; }
                            int idx;
                            if (wx >= 0 && wx < CHUNK_SIZE_X && wz >= 0 && wz < CHUNK_SIZE_Z) {
                                idx = ChunkIndex(wx, wy, wz);
                                outLight = {chunk.sky[idx] / 15.0f, chunk.block[idx].r / 15.0f, chunk.block[idx].g / 15.0f, chunk.block[idx].b / 15.0f};
                                return;
                            }
                            const Chunk* n = nullptr; int nlx = wx, nlz = wz;
                            if (wx < 0) { n = pXm; nlx = wx + CHUNK_SIZE_X; }
                            else if (wx >= CHUNK_SIZE_X) { n = pXp; nlx = wx - CHUNK_SIZE_X; }
                            else if (wz < 0) { n = pZm; nlz = wz + CHUNK_SIZE_Z; }
                            else if (wz >= CHUNK_SIZE_Z) { n = pZp; nlz = wz - CHUNK_SIZE_Z; }
                            
                            if (n && nlx >= 0 && nlx < CHUNK_SIZE_X && nlz >= 0 && nlz < CHUNK_SIZE_Z) {
                                idx = ChunkIndex(nlx, wy, nlz);
                                outLight = {n->sky[idx] / 15.0f, n->block[idx].r / 15.0f, n->block[idx].g / 15.0f, n->block[idx].b / 15.0f};
                            } else {
                                // 邻居没加载，当作空气处理（满天空光），避免边界缩水发黑
                                outLight = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f); 
                            }
                        };

                        // 采样点的 4 个位置（全部位于面的外侧）
                        glm::ivec3 P_outer = {lx + N.x, ly + N.y, lz + N.z};
                        glm::ivec3 P_U     = {lx + N.x + U.x, ly + N.y + U.y, lz + N.z + U.z};
                        glm::ivec3 P_V     = {lx + N.x + V.x, ly + N.y + V.y, lz + N.z + V.z};
                        glm::ivec3 P_UV    = {lx + N.x + U.x + V.x, ly + N.y + U.y + V.y, lz + N.z + U.z + V.z};

                        glm::vec4 sumLight(0.0f);
                        glm::vec4 l;
                        int count = 0;

                        // 采样外侧 4 个点
                        getLightAt(P_outer.x, P_outer.y, P_outer.z, l); sumLight += l; count++;
                        getLightAt(P_U.x, P_U.y, P_U.z, l); sumLight += l; count++;
                        getLightAt(P_V.x, P_V.y, P_V.z, l); sumLight += l; count++;
                        getLightAt(P_UV.x, P_UV.y, P_UV.z, l); sumLight += l; count++;

                        vert.light = sumLight / static_cast<float>(count);

                        // 如果是水，给保底值
                        if (type == BlockType::Water) vert.light = glm::vec4(1.0f);

                        result.vertices.push_back(vert);
                    }

                    result.indices.push_back(baseIndex + 0);
                    result.indices.push_back(baseIndex + 1);
                    result.indices.push_back(baseIndex + 2);
                    result.indices.push_back(baseIndex + 2);
                    result.indices.push_back(baseIndex + 3);
                    result.indices.push_back(baseIndex + 0);
                }
            }
        }
    }
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
                v.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
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

// Mesh worker
void MyGame::StartMeshWorker() {
    unsigned hw = std::thread::hardware_concurrency();
    // Mesh 构建比较耗 CPU，分配多一点线程
    unsigned n  = std::clamp(hw > 2 ? hw - 2 : 2u, 2u, 8u); 

    m_meshWorkerStop = false;
    for (unsigned i = 0; i < n; ++i) {
        m_meshWorkers.emplace_back([this] { MeshWorkerLoop(); });
    }
    logInfo(logger, "[MeshWorker] Started " << n << " workers");
}

void MyGame::StopMeshWorker() {
    if (m_meshWorkers.empty()) return;

    {
        std::lock_guard lk(m_meshTaskMutex);
        m_meshWorkerStop = true;
        while (!m_meshTaskQueue.empty()) m_meshTaskQueue.pop();
    }
    m_meshTaskCv.notify_all();
    for (auto& t : m_meshWorkers) {
        if (t.joinable()) t.join();
    }
    m_meshWorkers.clear();
}

void MyGame::MeshWorkerLoop() {
    while (true) {
        std::shared_ptr<MeshTask> task; // 这里只是个指针，不占栈空间
        {
            std::unique_lock lk(m_meshTaskMutex);
            m_meshTaskCv.wait(lk, [this] {
                return m_meshWorkerStop.load() || !m_meshTaskQueue.empty();
            });
            if (m_meshWorkerStop.load()) return;
            task = std::move(m_meshTaskQueue.front());
            m_meshTaskQueue.pop();
        }

        MeshResult result;
        result.chunkKey = task->chunkKey;
        // 传引用 *task
        BuildChunkMeshData(*task, result);

        {
            std::lock_guard lk(m_meshResultMutex);
            m_meshResultQueue.push(std::move(result));
        }
    }
}

void MyGame::DrainMeshResults() {
    constexpr int K = 4; 
    int done = 0;
    for (;;) {
        MeshResult r;
        {
            std::lock_guard lk(m_meshResultMutex);
            if (m_meshResultQueue.empty() || done >= K) break;
            r = std::move(m_meshResultQueue.front());
            m_meshResultQueue.pop();
        }

        auto it = m_chunks.find(r.chunkKey);
        if (it != m_chunks.end()) {
            it->second.chunk.sky = r.sky;
            it->second.chunk.block = r.block;

            if (r.vertices.empty()) {
                if (it->second.mesh != 0) {
                    renderer->DestroyMesh(it->second.mesh);
                    it->second.mesh = 0;
                }
            } else {
                Eng::client::MeshData data;
                data.vertices = std::move(r.vertices);
                data.indices  = std::move(r.indices);
                
                auto newMesh = renderer->CreateMesh(data);
                if (it->second.mesh != 0) {
                    renderer->DestroyMesh(it->second.mesh);
                }
                it->second.mesh = newMesh;
            }
        }
        ++done;
    }
}

void MyGame::QueueChunkMeshTask(uint64_t key, const Chunk& chunk) {
    auto task = std::make_shared<MeshTask>(); // 创建在堆上
    task->chunkKey = key;
    task->chunk = chunk;

    int cx = chunk.chunk_x;
    int cz = chunk.chunk_z;

    auto fetchNeighbor = [&](int ncx, int ncz, Chunk& outChunk, bool& has) {
        auto it = m_chunks.find(ChunkKey(ncx, ncz));
        if (it != m_chunks.end()) {
            outChunk = it->second.chunk;
            has = true;
        } else {
            has = false;
        }
    };

    fetchNeighbor(cx - 1, cz, task->nXm, task->hasN[0]);
    fetchNeighbor(cx + 1, cz, task->nXp, task->hasN[1]);
    fetchNeighbor(cx, cz - 1, task->nZm, task->hasN[2]);
    fetchNeighbor(cx, cz + 1, task->nZp, task->hasN[3]);

    {
        std::lock_guard lk(m_meshTaskMutex);
        m_meshTaskQueue.push(task);
    }
    m_meshTaskCv.notify_one();
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
    outV.reserve(static_cast<size_t>(N) * N * 16);
    outI.reserve(static_cast<size_t>(N) * N * 24);
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
        v.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        outV.push_back(v);
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    };

    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            float ySelf = cellY(i, j);
            int   tile  = cellTile(i, j);
            float u = ((tile % ATLAS_COLS) + 0.5f) * TILE_UV;
            float v = ((tile / static_cast<float>(ATLAS_COLS)) + 0.5f) * TILE_UV;

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
                    float nv = ((nt / static_cast<float>(ATLAS_COLS)) + 0.5f) * TILE_UV;
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
                    float nv = ((nt / static_cast<float>(ATLAS_COLS)) + 0.5f) * TILE_UV;
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
    outV.reserve(static_cast<size_t>(CELLS) * CELLS * 12);
    outI.reserve(static_cast<size_t>(CELLS) * CELLS * 18);
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
        v = ((t / static_cast<float>(ATLAS_COLS)) + 0.5f) * TILE_UV;
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