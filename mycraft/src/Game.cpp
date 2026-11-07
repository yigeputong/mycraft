#include "game/Game.h"

#include <Engine/tool/FileTool.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <nlohmann/json.hpp>

#include "Engine/Engine.h"
#include "Engine/service/AssetService.h"
#include "Engine/service/IRenderService.h"
#include "Engine/service/InputService.h"
#include "Engine/tool/Log.h"
#include "Engine/tool/MemoryTransport.h"
#include "Engine/tool/MessageReader.h"
#include "Engine/tool/MessageWriter.h"
#include "Engine/tool/NetworkTransport.h"
#include "Engine/tool/Platform.h"
#include "Engine/tool/Transport.h"
#include "Engine/tool/Window.h"

#include "game/GameServer.h"
#include "game/Protocol.h"
#include "game/client/MeshBuilderUtils.h"
#include "game/client/TextureArray.h"
#include "game/core/Blocks.h"
#include "game/core/LightEngine.h"
#include "game/core/Physics.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"

using json = nlohmann::json;

namespace game {

namespace {
void AppendPlayerBox(glm::vec3 c, glm::vec3 size, const glm::vec4& top, const glm::vec4& bottom, const glm::vec4& right,
                     const glm::vec4& left, const glm::vec4& front, const glm::vec4& back,
                     std::vector<Eng::service::Vertex>& vertices, std::vector<uint32_t>& indices) {
    const float x0 = c.x - size.x * 0.5f, x1 = c.x + size.x * 0.5f;
    const float y0 = c.y - size.y * 0.5f, y1 = c.y + size.y * 0.5f;
    const float z0 = c.z - size.z * 0.5f, z1 = c.z + size.z * 0.5f;

    const glm::vec3 corners[8] = {
        {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1},
    };
    const int faces[6][4] = {{1, 5, 6, 2}, {3, 7, 4, 0}, {7, 6, 5, 4}, {0, 1, 2, 3}, {3, 2, 6, 7}, {0, 4, 5, 1}};
    const glm::vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    static constexpr float fuv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    auto toUV = [](float px, float py) { return glm::vec2(px / 64.0f, py / 64.0f); };
    glm::vec4 faceUVs[6] = {right, left, top, bottom, front, back};

    for (int f = 0; f < 6; ++f) {
        auto base = (uint32_t)vertices.size();
        glm::vec4 uvRect = faceUVs[f];

        for (int k = 0; k < 4; ++k) {
            Eng::service::Vertex v;
            v.position = corners[faces[f][k]];
            v.normal = normals[f];
            float u = uvRect.x + fuv[k][0] * uvRect.z;
            float v_coord = uvRect.y + fuv[k][1] * uvRect.w;
            v.uv = toUV(u, v_coord);
            v.layer = 0.0f;
            v.ao = 1.0f;
            v.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            vertices.push_back(v);
        }
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 0);
    }
}

using Eng::service::ActionDesc;
using Eng::service::KeyCode;
using Eng::service::operator""_action;

// 编译期哈希常量
constexpr auto A_MoveForward = "MoveForward"_action;
constexpr auto A_MoveBack = "MoveBack"_action;
constexpr auto A_MoveLeft = "MoveLeft"_action;
constexpr auto A_MoveRight = "MoveRight"_action;
constexpr auto A_Jump = "Jump"_action;    // 跳跃 / 上升（双语义）
constexpr auto A_Sneak = "Sneak"_action;  // 下蹲 / 下降（双语义）
constexpr auto A_Dig = "Dig"_action;
constexpr auto A_Place = "Place"_action;
constexpr auto A_ToggleFly = "ToggleFly"_action;
constexpr auto A_ToggleDebug = "ToggleDebug"_action;
constexpr auto A_ToggleFullscreen = "ToggleFullscreen"_action;
constexpr auto A_OpenMenu = "OpenMenu"_action;
constexpr auto A_OpenBackpack = "OpenBackpack"_action;
constexpr auto A_HotbarSlot0 = "HotbarSlot0"_action;
constexpr auto A_HotbarSlot1 = "HotbarSlot1"_action;
constexpr auto A_HotbarSlot2 = "HotbarSlot2"_action;
constexpr auto A_HotbarSlot3 = "HotbarSlot3"_action;
constexpr auto A_HotbarSlot4 = "HotbarSlot4"_action;
constexpr auto A_HotbarSlot5 = "HotbarSlot5"_action;
constexpr auto A_HotbarSlot6 = "HotbarSlot6"_action;
constexpr auto A_HotbarSlot7 = "HotbarSlot7"_action;
constexpr auto A_HotbarSlot8 = "HotbarSlot8"_action;

static constexpr ActionDesc kGameActions[] = {
    {"MoveForward", {KeyCode::W}, "向前"},        {"MoveBack", {KeyCode::S}, "向后"},
    {"MoveLeft", {KeyCode::A}, "向左"},           {"MoveRight", {KeyCode::D}, "向右"},
    {"Jump", {KeyCode::Space}, "跳跃/上升"},      {"Sneak", {KeyCode::LShift}, "下蹲/下降"},
    {"Sprint", {KeyCode::LCtrl}, "疾跑"},         {"Dig", {KeyCode::MouseLeft}, "挖掘"},
    {"Place", {KeyCode::MouseRight}, "放置"},     {"ToggleFly", {KeyCode::F}, "切换飞行"},
    {"ToggleDebug", {KeyCode::F3}, "调试面板"},   {"ToggleFullscreen", {KeyCode::F11}, "全屏"},
    {"OpenMenu", {KeyCode::Escape}, "菜单"},      {"OpenBackpack", {KeyCode::E}, "背包"},
    {"HotbarSlot0", {KeyCode::Num1}, "快捷栏 1"}, {"HotbarSlot1", {KeyCode::Num2}, "快捷栏 2"},
    {"HotbarSlot2", {KeyCode::Num3}, "快捷栏 3"}, {"HotbarSlot3", {KeyCode::Num4}, "快捷栏 4"},
    {"HotbarSlot4", {KeyCode::Num5}, "快捷栏 5"}, {"HotbarSlot5", {KeyCode::Num6}, "快捷栏 6"},
    {"HotbarSlot6", {KeyCode::Num7}, "快捷栏 7"}, {"HotbarSlot7", {KeyCode::Num8}, "快捷栏 8"},
    {"HotbarSlot8", {KeyCode::Num9}, "快捷栏 9"},
};
}  // namespace

// ============================================================
// 设置持久化
// ============================================================
void MyGame::LoadSettings(Eng::Engine& engine) {
    // 先填默认值
    m_virtualButtons = {
        {"OpenMenu", {0.08f, 0.08f}, 0.05f, "⬅️"},      {"Jump", {0.88f, 0.85f}, 0.05f, "⬆️"},
        {"OpenBackpack", {0.08f, 0.20f}, 0.05f, "包"}, {"ToggleFly", {0.08f, 0.32f}, 0.05f, "飞"},
        {"Dig", {0.75f, 0.88f}, 0.05f, "挖"},          {"Place", {0.62f, 0.85f}, 0.05f, "放"},
        {"Sneak", {0.50f, 0.85f}, 0.05f, "⬇️"},
    };

    std::string path = Eng::tool::GetWritablePath(SETTINGS_FILE);
    auto data = Eng::tool::ReadFileToMemory(path);
    if (data.empty())
        return;

    try {
        auto j = json::parse(data.begin(), data.end());

        // --- 窗口配置 ---
        auto& wcfg = engine.GetConfig().window;
        if (j.contains("apitype"))
            wcfg.apitype = static_cast<Eng::tool::RenderAPItype>(j["apitype"].get<int>());
        if (j.contains("fullscreen"))
            wcfg.fullscreen = j["fullscreen"];
        if (j.contains("vsync"))
            wcfg.vsync = j["vsync"];

        // --- 渲染配置 ---
        auto& rcfg = engine.GetConfig().render;
        if (j.contains("renderDistance"))
            m_renderDistance = j["renderDistance"];
        if (j.contains("fov"))
            rcfg.fov = j["fov"];
        if (j.contains("aoStrength"))
            rcfg.m_aoStrength = j["aoStrength"];
        if (j.contains("mipmapLevels"))
            rcfg.mipmapLevels = j["mipmapLevels"];
        if (j.contains("enableAnisotropic"))
            rcfg.enableAnisotropic = j["enableAnisotropic"];
        if (j.contains("maxAnisotropy"))
            rcfg.maxAnisotropy = j["maxAnisotropy"];

        // --- 输入与触控 ---
        if (j.contains("input") && j["input"].is_object()) {
            LoadInputBindings(j["input"]);
        }
        if (j.contains("inputMode"))
            m_inputMode = static_cast<InputMode>(j["inputMode"].get<int>());
        if (j.contains("touchSensitivity"))
            m_touchSensitivity = j["touchSensitivity"];
        if (j.contains("touchDeadzone"))
            m_touchDeadzone = j["touchDeadzone"];

        if (j.contains("joystickBaseX"))
            m_joystickBasePos.x = j["joystickBaseX"];
        if (j.contains("joystickBaseY"))
            m_joystickBasePos.y = j["joystickBaseY"];

        if (j.contains("virtualButtons") && j["virtualButtons"].is_array()) {
            m_virtualButtons.clear();
            for (const auto& btn : j["virtualButtons"]) {
                if (!btn.is_object())
                    continue;
                VirtualButtonConfig cfg;
                cfg.actionName = btn.value("action", "");

                if (cfg.actionName == "FlyUp" || cfg.actionName == "FlyDown") {
                    logInfo(logger, "[Game] Dropping legacy virtual button: " << cfg.actionName);
                    continue;
                }

                cfg.pos.x = glm::clamp(btn.value("x", 0.5f), 0.02f, 0.98f);
                cfg.pos.y = glm::clamp(btn.value("y", 0.5f), 0.02f, 0.98f);
                cfg.radius = glm::clamp(btn.value("radius", 0.05f), 0.02f, 0.12f);
                cfg.label = btn.value("label", "");
                if (!cfg.actionName.empty()) {
                    m_virtualButtons.push_back(cfg);
                }
            }
        }

        // 兜底按钮
        auto hasAction = [this](const std::string& name) {
            return std::any_of(m_virtualButtons.begin(), m_virtualButtons.end(),
                               [&](const VirtualButtonConfig& b) { return b.actionName == name; });
        };
        if (!hasAction("OpenMenu"))
            m_virtualButtons.push_back({"OpenMenu", {0.08f, 0.08f}, 0.05f, "||"});
        if (!hasAction("OpenBackpack"))
            m_virtualButtons.push_back({"OpenBackpack", {0.08f, 0.20f}, 0.05f, "包"});
        if (!hasAction("ToggleFly"))
            m_virtualButtons.push_back({"ToggleFly", {0.08f, 0.32f}, 0.05f, "飞"});
        if (!hasAction("Jump"))
            m_virtualButtons.push_back({"Jump", {0.88f, 0.85f}, 0.06f, "跳"});
        if (!hasAction("Sneak"))
            m_virtualButtons.push_back({"Sneak", {0.50f, 0.85f}, 0.05f, "降"});
    } catch (const std::exception& e) {
        logError(logger, "[Game] Failed to parse settings.json: " << e.what());
    }
}

void MyGame::SaveSettings(Eng::Engine& engine) {
    json j;

    // UI 操作的是 mainWin 的配置，所以保存时必须以 mainWin 为准，并同步回 engine
    auto& wcfg = mainWin ? mainWin->GetConfigs() : engine.GetConfig().window;

    // 同步给 engine，防止后续逻辑读错
    engine.GetConfig().window.fullscreen = wcfg.fullscreen;
    engine.GetConfig().window.vsync = wcfg.vsync;
    engine.GetConfig().window.apitype = wcfg.apitype;
    j["apitype"] = static_cast<int>(wcfg.apitype);
    j["fullscreen"] = wcfg.fullscreen;
    j["vsync"] = wcfg.vsync;

    auto& rcfg = engine.GetConfig().render;
    j["renderDistance"] = m_renderDistance;
    j["fov"] = rcfg.fov;
    j["aoStrength"] = rcfg.m_aoStrength;
    j["mipmapLevels"] = rcfg.mipmapLevels;
    j["enableAnisotropic"] = rcfg.enableAnisotropic;
    j["maxAnisotropy"] = rcfg.maxAnisotropy;

    // --- 输入与触控 ---
    json jInput;
    SaveInputBindings(jInput);
    j["input"] = std::move(jInput);
    j["inputMode"] = static_cast<int>(m_inputMode);
    j["touchSensitivity"] = m_touchSensitivity;
    j["touchDeadzone"] = m_touchDeadzone;

    j["joystickBaseX"] = m_joystickBasePos.x;
    j["joystickBaseY"] = m_joystickBasePos.y;

    json jBtns = json::array();
    for (const auto& b : m_virtualButtons) {
        jBtns.push_back(
            {{"action", b.actionName}, {"x", b.pos.x}, {"y", b.pos.y}, {"radius", b.radius}, {"label", b.label}});
    }
    j["virtualButtons"] = jBtns;

    std::string path = Eng::tool::GetWritablePath(SETTINGS_FILE);
    Eng::tool::WriteStringToFile(path, j.dump(4));
}

// ============================================================
// 输入绑定：注册 / 加载 / 保存
// ============================================================
void MyGame::RegisterGameActions() {
    if (!m_inputService)
        return;

    if (!m_inputService->RegisterActions(kGameActions)) {
        logError(logger, "[Game] RegisterGameActions failed (name empty / conflict / duplicate in batch)");
        return;
    }

    m_inputService->SetMoveActions(A_MoveForward, A_MoveBack, A_MoveLeft, A_MoveRight);
    logInfo(logger, "[Game] Registered " << (sizeof(kGameActions) / sizeof(kGameActions[0])) << " input actions");
}

void MyGame::LoadInputBindings(const nlohmann::json& inputJson) {
    if (!m_inputService)
        return;
    if (!inputJson.contains("bindings") || !inputJson["bindings"].is_object())
        return;

    // 迁移映射：旧 action → 新 action（FlyUp/FlyDown 已合并进 Jump/Sneak）
    static const std::unordered_map<std::string, std::string> kLegacyActionMap = {
        {"FlyUp", "Jump"},
        {"FlyDown", "Sneak"},
    };

    // 按目标 action 去重合并 keys（旧存档可能同时存在 Jump 与 FlyUp 绑 Space）
    std::unordered_map<std::string, std::vector<Eng::service::KeyCode>> mergedKeys;

    for (const auto& [rawActionName, keysArr] : inputJson["bindings"].items()) {
        if (!keysArr.is_array())
            continue;

        std::string actionName = rawActionName;
        auto legacy = kLegacyActionMap.find(actionName);
        if (legacy != kLegacyActionMap.end()) {
            logInfo(logger, "[Game] Migrating legacy binding: " << rawActionName << " -> " << legacy->second);
            actionName = legacy->second;
        }

        auto& target = mergedKeys[actionName];
        for (const auto& k : keysArr) {
            if (!k.is_string())
                continue;
            auto kcOpt = Eng::service::KeyCodeFromString(k.get<std::string>());
            if (kcOpt && *kcOpt != Eng::service::KeyCode::None) {
                if (std::find(target.begin(), target.end(), *kcOpt) == target.end()) {
                    target.push_back(*kcOpt);
                }
            }
        }
    }

    std::vector<Eng::service::InputService::BindingSnapshot> bindings;
    bindings.reserve(mergedKeys.size());
    for (auto& [name, keys] : mergedKeys) {
        Eng::service::InputService::BindingSnapshot snap;
        snap.name = name;
        snap.keys = std::move(keys);
        bindings.push_back(std::move(snap));
    }

    if (!m_inputService->ImportBindings(bindings)) {
        logWarning(logger, "[Game] Some input bindings reference unknown actions (ignored)");
    }
}

void MyGame::SaveInputBindings(nlohmann::json& inputJson) {
    if (!m_inputService)
        return;

    inputJson["version"] = 1;

    json jBindings = json::object();
    for (const auto& snap : m_inputService->ExportBindings()) {
        json keys = json::array();
        for (auto kc : snap.keys) {
            keys.push_back(Eng::service::KeyCodeToString(kc));
        }
        jBindings[snap.name] = std::move(keys);
    }
    inputJson["bindings"] = std::move(jBindings);
}

// ============================================================
// 存档列表刷新与网络启动
// ============================================================
void MyGame::RefreshWorldList() {
    m_savedWorlds = game::server::WorldSave::ListAllWorlds();
    logInfo(logger, "[Game] Found " << m_savedWorlds.size() << " saved worlds.");
}

bool MyGame::StartLocalServer(bool useMemory, bool createClient, const std::string& worldId) {
    if (m_server)
        return true;

    auto server = std::make_unique<game::server::GameServer>();
    if (!server->Start(25565, worldId, useMemory)) {
        logError(logger, "[Game] Failed to start local server for world: " << worldId);
        m_statusText = "Failed to start: World is locked or corrupted.";
        return false;
    }

    if (createClient) {
        if (useMemory) {
            m_client = std::make_unique<Eng::tool::MemoryClientTransport>(server->GetMemoryPipe());
            m_client->Connect("", 0);
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
    Eng::tool::WindowConfig windowConfig = {.apitype = engine.GetConfig().window.apitype,
                                            .title = "Mycraft v" MYCRAFT_VERSION,
                                            .windowWidth = 1280,
                                            .windowHeight = 720,
                                            .fullscreen = engine.GetConfig().window.fullscreen,
                                            .vsync = engine.GetConfig().window.vsync};

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    winMgr = engine.GetWindowManager();
    m_win = winMgr->CreateWindow(windowConfig);
    mainWin = winMgr->GetWindow(m_win);
    if (!mainWin)
        return false;

    mainWin->onEvent = [this](const SDL_Event& e) {
        ImGuiIO& io = ImGui::GetIO();

        // 手动把触摸事件精准映射为 ImGui 的鼠标事件，彻底绕开 SDL3 在安卓上的坐标计算 Bug
        if (Eng::tool::IsMobilePlatform()) {
            if (e.type == SDL_EVENT_FINGER_DOWN) {
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(e.tfinger.x * io.DisplaySize.x, e.tfinger.y * io.DisplaySize.y);
                io.AddMouseButtonEvent(0, true);
            } else if (e.type == SDL_EVENT_FINGER_MOTION) {
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(e.tfinger.x * io.DisplaySize.x, e.tfinger.y * io.DisplaySize.y);
            } else if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) {
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(e.tfinger.x * io.DisplaySize.x, e.tfinger.y * io.DisplaySize.y);
                io.AddMouseButtonEvent(0, false);
            }
        }

        ImGui_ImplSDL3_ProcessEvent(&e);

        switch (e.type) {
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                if (renderer)
                    RecreateFBOs(e.window.data1, e.window.data2);
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

    if (m_inputService) {
        m_inputService->SetRelativeMode(mainWin->GetSDLWindow(), false);
    }
    m_relativeMode = false;
    return true;
}

void MyGame::RecreateFBOs(int w, int h) {
    if (w <= 0 || h <= 0)
        return;
    Eng::service::rhi::FramebufferCreateInfo info{
        .width = w, .height = h, .hasDepth = true, .colorFormat = Eng::service::rhi::TextureFormat::RGBA8_UNORM};
    m_mainFbo = renderer->CreateFramebuffer(info);
}

bool MyGame::SetupRenderer(Eng::Engine& engine) {
    // 关闭 SDL3 默认的触摸转鼠标，我们将手动在 onEvent 中转换，防止坐标错乱
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    renderer = engine.CreateRenderService(mainWin);
    if (!renderer) {
        logError(logger, "[Game] Engine failed to create render service: " << engine.GetLastError());
        return false;
    }

    m_assetManager = engine.CreateAssetService(renderer);
    if (!m_assetManager) {
        logError(logger, "[Game] Engine failed to create asset service: " << engine.GetLastError());
        return false;
    }

    {
        auto tileTints = m_blockRegistry.CollectTileTints();
        client::BuildBlockTextureArray(m_assetManager, tileTints);
    }

    m_deviceInfo = renderer->GetDeviceInfo();
    m_useVulkan = (m_deviceInfo.backend == "Vulkan");

    logInfo(logger, "[RenderAPI] Backend:     " << m_deviceInfo.backend);
    logInfo(logger, "[RenderAPI] Device:      " << m_deviceInfo.deviceName);
    logInfo(logger, "[RenderAPI] Vendor:      " << m_deviceInfo.vendor);
    logInfo(logger, "[RenderAPI] API version: " << m_deviceInfo.apiVersion);
    logInfo(logger, "[RenderAPI] Driver:      " << m_deviceInfo.driverVersion);
    logInfo(logger, "[RenderAPI] Shader lang: " << m_deviceInfo.shadingLanguage);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (Eng::tool::IsMobilePlatform()) {
        io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }

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
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.09f, 0.95f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.13f, 0.60f);
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.80f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.80f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.80f, 0.80f, 0.30f);

    float dpiScale = SDL_GetWindowDisplayScale(mainWin->GetSDLWindow());
    if (dpiScale <= 0.0f)
        dpiScale = 1.0f;
    m_uiScale = dpiScale;
    style.FontScaleMain = dpiScale;

    static const ImWchar allGlyphs[] = {0x0020, 0xFFFF, 0};

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;
    float baseFontSize = 16.0f;

    ImFont* unifont = nullptr;
    auto fontData = m_assetManager->ReadFile("assets/fonts/unifont-18.0.01.otf");
    if (fontData) {
        size_t fontSize = fontData->size();
        void* fontMem = IM_ALLOC(fontSize);
        memcpy(fontMem, fontData->data(), fontSize);

        ImFontConfig memFontConfig = fontConfig;
        memFontConfig.FontDataOwnedByAtlas = true;

        unifont = io.Fonts->AddFontFromMemoryTTF(fontMem, static_cast<int>(fontSize), baseFontSize, &memFontConfig,
                                                 allGlyphs);
    }
    if (!unifont) {
        logError(logger, "[Game] Failed to load Unifont, falling back to default font.");
        io.Fonts->AddFontDefault();
    } else {
        logInfo(logger, "[Game] Loaded Unifont at size " << baseFontSize << " (Scale: " << dpiScale << ")");
    }

    try {
        m_sceneRenderer = std::make_unique<Eng::client::SceneRenderer>(renderer, m_assetManager);
    } catch (const std::exception& e) {
        logError(logger, "[Game] SceneRenderer init failed: " << e.what());
        return false;
    }

    m_playerTexture = m_assetManager->LoadTexture("assets/textures/entity/player.png");
    if (!m_playerTexture)
        logWarning(logger, "[MyGame] Player texture failed to load.");

    const auto& pixels = client::GetTextureArrayPixels();
    int layerCount = client::GetTextureLayerCount();

    if (pixels.empty() || layerCount == 0) {
        logError(logger, "[MyGame] Fatal: Texture Array pixels are empty!");
        return false;
    }

    Eng::service::rhi::TextureCreateInfo tci;
    tci.pixels = std::as_bytes(std::span(pixels));
    tci.width = client::TILE_SIZE;
    tci.height = client::TILE_SIZE;
    tci.arrayLayers = layerCount;
    tci.format = Eng::service::rhi::TextureFormat::RGBA8_UNORM;
    tci.requestedMips = 0;
    tci.magFilter = Eng::service::rhi::FilterMode::Nearest;
    tci.minFilter = Eng::service::rhi::MinFilterMode::Nearest_Mipmap_Linear;
    tci.wrapU = Eng::service::rhi::WrapMode::ClampToEdge;
    tci.wrapV = Eng::service::rhi::WrapMode::ClampToEdge;
    tci.enableAnisotropy = true;

    m_atlasTexture = renderer->CreateTexture(tci);
    if (!m_atlasTexture) {
        logError(logger, "[MyGame] Failed to create texture array.");
        return false;
    }
    logInfo(logger, "[MyGame] Texture array created with " << layerCount << " layers");

    RecreateFBOs(mainWin->GetConfigs().windowPixelWidth, mainWin->GetConfigs().windowPixelHeight);
    if (!m_mainFbo) {
        logError(logger, "[Game] FBO creation failed!");
        return false;
    }

    m_globalStateUBO =
        renderer->CreateBuffer({.size = sizeof(GlobalState), .usage = Eng::service::rhi::BufferUsage::Uniform});
    if (!m_globalStateUBO) {
        logError(logger, "[Game] Failed to create GlobalState UBO");
        return false;
    }
    renderer->BindUniformBuffer(m_globalStateUBO, 0, 0);

    return true;
}

void MyGame::SetupShaders() {
    m_cubeShader = m_assetManager->LoadShader(m_assetManager->MakeShaderCreateInfo("mesh"));
    m_lodShader = m_assetManager->LoadShader(m_assetManager->MakeShaderCreateInfo("lod"));
    m_skyShader = m_assetManager->LoadShader(m_assetManager->MakeShaderCreateInfo(
        "sky", Eng::service::rhi::GraphicsPipelineState::PresetSkybox(), Eng::service::rhi::VertexLayout::Skybox));
    m_entityShader = m_assetManager->LoadShader(m_assetManager->MakeShaderCreateInfo(
        "entity", Eng::service::rhi::GraphicsPipelineState::PresetOpaque(), Eng::service::rhi::VertexLayout::Standard,
        sizeof(Eng::service::rhi::PerObjectData)));
    m_handShader = m_assetManager->LoadShader(m_assetManager->MakeShaderCreateInfo(
        "entity", Eng::service::rhi::GraphicsPipelineState::PresetHand(), Eng::service::rhi::VertexLayout::Standard,
        sizeof(Eng::service::rhi::PerObjectData)));

    if (!m_cubeShader || !m_skyShader || !m_lodShader || !m_entityShader || !m_handShader) {
        std::string err = "核心着色器加载失败！请检查 assets/shaders/build/ 目录。";
        logFatal(logger, "[MyGame] " << err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shader Load Error", err.c_str(), nullptr);
        throw std::runtime_error(err);
    }
}

// 核心循环
void MyGame::OnStart(Eng::Engine& engine) {
    if (!logger)
        logger = std::make_unique<Eng::tool::Log>();

    if (engine.IsDedicatedServer()) {
        if (!StartLocalServer(false, false, "world_server")) {
            m_shouldQuit = true;
            return;
        }
        return;
    }

    m_inputService = engine.GetInputService();
    if (!m_inputService) {
        m_shouldQuit = true;
        return;
    }

    RegisterGameActions();
    LoadSettings(engine);

    if (m_inputMode == InputMode::Auto) {
        m_activeInputMode = Eng::tool::IsMobilePlatform() ? InputMode::Touch : InputMode::KeyboardMouse;
    } else {
        m_activeInputMode = m_inputMode;
    }

    m_blockRegistry.LoadAll("assets/block");

    try {
        if (!SetupWindow(engine))
            return;
        if (!mainWin->GetFallbackReason().empty()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "图形 API 降级", mainWin->GetFallbackReason().c_str(),
                                     nullptr);
        }
    } catch (const std::exception& e) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "无法启动渲染器", e.what(), nullptr);
        m_shouldQuit = true;
        return;
    }

    if (!SetupRenderer(engine))
        return;
    SetupShaders();

    m_lodManager.SetRegistry(&m_blockRegistry);
    m_meshManager.SetRegistry(&m_blockRegistry);
    m_world.SetRegistry(&m_blockRegistry);
    m_meshManager.Start();

    game::server::WorldSave::CleanupStaleLocks();

    RefreshWorldList();
    m_state = GameState::MainMenu;
}

bool MyGame::OnUpdate(Eng::Engine& /*engine*/, float deltaTime) {
    if (m_minimized) {
        SDL_Delay(16);
        return false;
    }

    if (m_returnToMenuPending) {
        m_returnToMenuPending = false;
        ReturnToMainMenu();
        return false;
    }

    const bool wantRelative = (m_state == GameState::Playing) && !m_menuOpen && !m_inventoryOpen;
    if (wantRelative != m_relativeMode) {
        if (m_inputService && m_activeInputMode == InputMode::KeyboardMouse && !Eng::tool::IsMobilePlatform()) {
            m_inputService->SetRelativeMode(mainWin->GetSDLWindow(), wantRelative);
        }
        m_relativeMode = wantRelative;
    }

    float frameMs = deltaTime * 1000.0f;
    m_lastFrameMs = frameMs;
    m_fpsTimer += deltaTime;
    m_fpsFrameCount += 1;
    m_fpsAccumMs += frameMs;
    m_fpsPeakMs = std::max(m_fpsPeakMs, frameMs);
    if (m_fpsTimer >= 1.0f) {
        m_fpsDisplay = m_fpsFrameCount / m_fpsTimer;
        m_frameMsDisplay = frameMs;
        m_frameMsAvgDisp = m_fpsAccumMs / m_fpsFrameCount;
        m_frameMsPeakDisp = m_fpsPeakMs;
        m_fpsTimer = 0.0f;
        m_fpsFrameCount = 0;
        m_fpsAccumMs = 0.0f;
        m_fpsPeakMs = 0.0f;
    }

    m_animTime += deltaTime;

    if (m_state != GameState::Playing) {
        return m_shouldQuit;
    }

    game::PlayerMotion m;
    m.position = cameraPos;
    m.velocity = m_playerVelocity;
    m.onGround = m_playerOnGround;
    m.flyMode = m_flyMode;

    game::StepPlayer(m, m_lastMoveDir, m_jumpHoldTimer > 0.0f, deltaTime,
                     [this](int x, int y, int z) { return IsSolidAt(x, y, z); });

    cameraPos = m.position;
    m_playerVelocity = m.velocity;
    m_playerOnGround = m.onGround;

    constexpr float CORRECTION_RATE = 8.0f;
    float t = 1.0f - std::exp(-CORRECTION_RATE * deltaTime);
    cameraPos += m_positionError * t;
    m_positionError *= (1.0f - t);

    float hoursPerSec = 24.0f / kDayLengthSec;
    m_timeOfDay = std::fmod(m_timeOfDay + hoursPerSec * deltaTime, 24.0f);

    float angle = (m_timeOfDay / 24.0f) * 6.2831853f - 1.5707963f;
    glm::vec3 sunDir = glm::normalize(glm::vec3(cos(angle), sin(angle), 0.3f));
    float sunH = sunDir.y;

    float sunI = glm::clamp(sunH * 2.5f, 0.0f, 0.85f);
    float warm = 1.0f - glm::clamp(sunH * 3.0f, 0.0f, 1.0f);
    glm::vec3 sunColor = glm::mix(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.5f, 0.2f), warm);
    glm::vec3 ambient =
        glm::mix(glm::vec3(0.1f, 0.12f, 0.2f), glm::vec3(0.35f, 0.4f, 0.45f), glm::clamp(sunH * 2.0f, 0.0f, 1.0f));

    m_globalState.lightDir = sunDir;
    m_globalState.lightColor = sunColor;
    m_globalState.lightIntensity = sunI;
    m_globalState.lightAmbient = ambient;
    m_globalState.timeOfDay = m_timeOfDay;

    if (GetInput(deltaTime))
        return true;

    std::vector<uint8_t> data;
    while (m_client->Receive(data)) {
        HandleServerMessage(data);
    }

    constexpr float PICKUP_TIME = 0.2f;
    for (auto it = m_itemEntities.begin(); it != m_itemEntities.end();) {
        auto& e = it->second;

        if (e.pickingUp) {
            e.pickupT += deltaTime / PICKUP_TIME;
            if (e.pickupT >= 1.0f) {
                if (e.pickerId == m_myClientId)
                    AddToInventory(e.itemType, 1);
                it = m_itemEntities.erase(it);
                continue;
            }
            glm::vec3 target = cameraPos + glm::vec3(0.0f, PLAYER_EYE / 2, 0.0f);
            float pickupT = e.pickupT * e.pickupT;
            e.renderPos = glm::mix(e.pickupStart, target, pickupT);
            ++it;
            continue;
        }

        if (e.interpT < 1.0f) {
            e.interpT += deltaTime * 20.0f;
            if (e.interpT > 1.0f)
                e.interpT = 1.0f;
            e.renderPos = glm::mix(e.prevPos, e.targetPos, e.interpT);
        }
        ++it;
    }

    m_world.UpdateStreaming(
        cameraPos, m_renderDistance,
        [this](int cx, int cz) {
            Eng::tool::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkRequest));
            w.Write<int32_t>(cx);
            w.Write<int32_t>(cz);
            m_client->Send(w.GetBuffer());
        },
        [this](int cx, int cz, const game::client::ClientChunk& chunk) { m_lodManager.StoreRealChunk(cx, cz, chunk); });

    for (auto it = m_knownSceneChunks.begin(); it != m_knownSceneChunks.end();) {
        if (m_world.GetChunk(*it) == nullptr) {
            if (m_sceneRenderer)
                m_sceneRenderer->OnChunkUnloaded(*it);
            m_chunkMeshVersions.erase(*it);
            it = m_knownSceneChunks.erase(it);
        } else {
            ++it;
        }
    }

    auto dirtyKeys = m_world.FetchDirtyChunks(8);
    for (uint64_t key : dirtyKeys) {
        auto* cc = m_world.GetChunk(key);
        if (cc)
            QueueChunkMeshTask(key);
    }

    int actualLodDistance = m_enableLod ? m_lodRenderDistance : 0;
    m_lodManager.Update(cameraPos, m_world, m_renderDistance, actualLodDistance, renderer);

    DrainMeshResults(16);

    return false;
}

void MyGame::OnRender(Eng::Engine& engine) {
    if (m_minimized)
        return;

    if (m_applyPending) {
        renderer->ApplySettings(mainWin->GetConfigs(), engine.GetConfig().render);
        m_applyPending = false;
    }

    if (m_state == GameState::Playing) {
        glm::vec3 eye = cameraPos + glm::vec3(0.0f, PLAYER_EYE, 0.0f);
        view = glm::lookAt(eye, eye + cameraFront, cameraUp);
        projection =
            renderer->MakePerspectiveMatrix(glm::radians(engine.GetConfig().render.fov), mainWin->GetAspectRatio(),
                                            engine.GetConfig().render.zNear, engine.GetConfig().render.zFar);
    }

    m_globalState.view = view;
    m_globalState.projection = projection;
    m_globalState.viewPos = cameraPos;
    m_globalState.aoStrength = engine.GetConfig().render.m_aoStrength;

    renderer->UpdateBuffer(m_globalStateUBO, 0, std::as_bytes(std::span<const GlobalState>(&m_globalState, 1)));

    const glm::vec4 clearColor =
        (m_state == GameState::Playing) ? glm::vec4(0.5f, 0.7f, 1.0f, 1.0f) : glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    renderer->Clear(Eng::service::rhi::ClearFlag::All, clearColor);

    renderer->BeginFrame();

    if (m_state == GameState::Playing) {
        glm::mat4 viewProj = projection * view;
        Eng::client::Frustum frameFrustum;
        frameFrustum.UpdatePlanes(viewProj);

        m_sceneRenderer->BeginScene();

        m_lodManager.PrepareDraw(renderer, frameFrustum);
        m_lodManager.Draw(renderer, m_cubeShader, m_atlasTexture);

        if (!m_playerMesh.vbo)
            m_playerMesh = BuildPlayerMesh();
        for (auto& [id, ps] : m_otherPlayers) {
            Eng::service::rhi::PerObjectData data;
            data.model = glm::translate(glm::mat4(1.0f), ps.position);
            data.model = glm::rotate(data.model, ps.yaw, glm::vec3(0, 1, 0));
            m_sceneRenderer->SubmitMesh(m_playerMesh, m_entityShader, m_playerTexture, data);
        }

        if (!m_itemEntities.empty()) {
            std::unordered_map<BlockId, std::vector<Eng::service::rhi::PerObjectData>> instances;
            for (const auto& [id, e] : m_itemEntities) {
                Eng::service::rhi::PerObjectData data;
                data.model = glm::translate(glm::mat4(1.0f), e.renderPos);
                data.model = glm::rotate(data.model, m_animTime * 2.0f, glm::vec3(0, 1, 0));
                data.model = glm::translate(
                    data.model, glm::vec3(0.0f, std::sin(m_animTime * 3.0f + e.id * 0.5f) * 0.1f + 0.1f, 0.0f));
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

        if (!m_skyboxMesh.vbo)
            m_skyboxMesh = BuildSkyboxMesh();
        m_sceneRenderer->SubmitMesh(m_skyboxMesh, m_skyShader, m_atlasTexture);

        if (!m_handMesh.vbo)
            m_handMesh = BuildHandMesh();

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
        glm::mat4 handModel = glm::inverse(view) * handLocalModel;

        if (m_handShader) {
            m_sceneRenderer->SubmitMesh(m_handMesh, m_handShader, m_playerTexture, {.model = handModel});

            auto& curSlot = m_inventory[m_hotbarIndex];
            if (!curSlot.isEmpty()) {
                const auto& itemMesh = GetOrCreateItemMesh(curSlot.type);
                if (itemMesh.vbo) {
                    glm::mat4 itemLocalModel = handLocalModel;
                    itemLocalModel = glm::translate(itemLocalModel, glm::vec3(0.0f, 0.2f, -0.5f));
                    itemLocalModel = glm::scale(itemLocalModel, glm::vec3(0.5f));
                    glm::mat4 itemModel = glm::inverse(view) * itemLocalModel;
                    m_sceneRenderer->SubmitMesh(itemMesh, m_handShader, m_atlasTexture, {.model = itemModel});
                }
            }
        }

        m_sceneRenderer->EndScene(frameFrustum, m_cubeShader, m_atlasTexture);
    }

    // 把 UI 处理完全交还给 ClientUI
    m_ui.Draw(engine, *this);

    renderer->EndFrame();
}

void MyGame::OnShutdown(Eng::Engine& /*engine*/) {
    ExitGame();

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
    renderer = nullptr;
    mainWin = nullptr;
    winMgr = nullptr;
    m_win = 0;
}

// 游戏生命周期
void MyGame::EnterGame() {
    logInfo(logger, "[Game] Entering game session...");

    m_meshManager.Start();

    cameraPos = glm::vec3(0.0f, 40.0f, 0.0f);
    cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    yaw = -90.0f;
    pitch = 0.0f;
    m_playerVelocity = glm::vec3(0.0f);
    m_positionError = glm::vec3(0.0f);
    m_lastMoveDir = glm::vec3(0.0f);
    m_playerOnGround = false;

    m_timeOfDay = 8.0f;
    m_animTime = 0.0f;
    m_walkDist = 0.0f;
    m_swingProgress = 0.0f;

    m_jumpHoldTimer = 0.0f;
    m_placeCooldown = 0.0f;
    m_digCooldown = 0.0f;

    m_fpsTimer = 0.0f;
    m_fpsFrameCount = 0;
    m_fpsAccumMs = 0.0f;
    m_fpsPeakMs = 0.0f;
    m_fpsDisplay = 0.0f;
    m_frameMsDisplay = 0.0f;
    m_frameMsAvgDisp = 0.0f;
    m_frameMsPeakDisp = 0.0f;
    m_serverTickMs = 0.0f;
    m_serverTickAvg = 0.0f;
    m_serverTickMax = 0.0f;

    m_hotbarIndex = 0;
    m_inventoryOpen = false;

    m_itemEntities.clear();
    m_otherPlayers.clear();
    m_myClientId = 0;

    if (m_sceneRenderer) {
        m_sceneRenderer->Clear();
    }
    m_knownSceneChunks.clear();

    m_state = GameState::Playing;
    m_menuOpen = false;
    m_statusText.clear();

    logInfo(logger, "[Game] Game session entered successfully.");
}

void MyGame::ExitGame() {
    logInfo(logger, "[Game] Exiting game session...");

    if (renderer)
        renderer->WaitIdle();

    m_lodManager.Stop();
    m_meshManager.Stop();

    if (m_client)
        m_client.reset();
    if (m_server) {
        m_server->Stop();
        m_server.reset();
    }

    m_world.Clear();
    m_lodManager.Clear();
    m_itemEntities.clear();
    m_otherPlayers.clear();

    m_playerMesh = {};
    m_handMesh = {};
    m_skyboxMesh = {};
    m_itemMeshCache.clear();

    if (m_sceneRenderer) {
        m_sceneRenderer->Clear();
    }
    m_knownSceneChunks.clear();

    m_state = GameState::MainMenu;
    m_selectedWorldId.clear();
    m_menuOpen = false;
    m_statusText.clear();

    m_chunkMeshVersions.clear();

    logInfo(logger, "[Game] Game session exited.");
}

void MyGame::ReturnToMainMenu() {
    ExitGame();
    logInfo(logger, "[Game] Returned to main menu");
}

// 输入
void MyGame::SendClickContainer(int16_t slot, uint8_t button, uint8_t mode) {
    if (!m_client)
        return;
    Eng::tool::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::ClickContainer));
    w.Write<int32_t>(m_inventoryStateId);
    w.Write<int16_t>(slot);
    w.Write<uint8_t>(button);
    w.Write<uint8_t>(mode);
    m_client->Send(w.GetBuffer());

    m_inventoryStateId++;
}

bool MyGame::GetInput(float dt) {
    if (!m_inputService)
        return m_shouldQuit;

    UpdateActiveInputMode();

    // ==========================================
    // 1. 全局 UI 开关
    // ==========================================
    if (m_inputService->IsActionPressed(A_OpenMenu)) {
        m_menuOpen = !m_menuOpen;
        if (m_menuOpen)
            m_inventoryOpen = false;  // 打开菜单时自动关背包
    }
    if (m_inputService->IsActionPressed(A_OpenBackpack)) {
        m_inventoryOpen = !m_inventoryOpen;

        // 如果关闭背包，且手上还拿着东西
        if (!m_inventoryOpen && !m_heldItemStack.isEmpty()) {
            bool placed = false;
            // 优先寻找主背包或快捷栏的空位放回去
            for (int i = 0; i < 36; ++i) {
                if (m_inventory[i].isEmpty()) {
                    m_inventory[i] = m_heldItemStack;
                    m_heldItemStack = {};
                    placed = true;
                    // 发送左键点击放回该格子
                    SendClickContainer(i, 0, 0);
                    break;
                }
            }
            // 如果背包真的满了，再执行丢弃
            if (!placed) {
                SendClickContainer(-1, 0, 4);
                m_heldItemStack = {};  // 本地预测清空
            }
        }
    }

    // 如果菜单或背包打开，拦截后续游戏内操作
    if (m_menuOpen || m_inventoryOpen) {
        // 处理背包内的 Q 键丢弃（快捷栏里的物品）
        if (m_inventoryOpen && m_inputService->IsKeyPressed(Eng::service::KeyCode::Q)) {
            auto& curSlot = m_inventory[m_hotbarIndex];
            if (!curSlot.isEmpty()) {
                SendClickContainer(m_hotbarIndex, 0, 4);  // 丢一个
            }
        }
        return m_shouldQuit;
    }

    // ==========================================
    // 2. 游戏内视角与移动
    // ==========================================
    if (m_inputService->IsActionPressed(A_ToggleFly)) {
        m_flyMode = !m_flyMode;
        m_positionError = glm::vec3(0.0f);
        m_playerVelocity = glm::vec3(0.0f);
    }

    if (m_inputService->IsActionPressed(A_ToggleFullscreen)) {
        auto& configs = mainWin->GetConfigs();
        configs.fullscreen = !configs.fullscreen;
        mainWin->SetFullscreen(configs.fullscreen);
        RecreateFBOs(mainWin->GetConfigs().windowPixelWidth, mainWin->GetConfigs().windowPixelHeight);
    }

    if (m_inputService->IsActionPressed(A_ToggleDebug))
        m_showDebug = !m_showDebug;

    // 键鼠视角
    if (m_activeInputMode == InputMode::KeyboardMouse) {
        glm::vec2 mouseDelta = m_inputService->GetMouseDelta();
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse) {
            yaw += mouseDelta.x * 0.1f;
            pitch -= mouseDelta.y * 0.1f;
            pitch = glm::clamp(pitch, -89.0f, 89.0f);
        }
    }

    glm::vec3 mousefront;
    mousefront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    mousefront.y = sin(glm::radians(pitch));
    mousefront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(mousefront);

    // 移动
    glm::vec2 moveLocal = m_inputService->GetMoveDir();
    glm::vec3 forward = glm::normalize(glm::vec3(cameraFront.x, 0.0f, cameraFront.z));
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    glm::vec3 moveDir = forward * moveLocal.y + right * moveLocal.x;

    // 飞行
    if (m_flyMode) {
        if (m_inputService->IsActionDown(A_Jump))
            moveDir.y += 1.0f;
        if (m_inputService->IsActionDown(A_Sneak))
            moveDir.y -= 1.0f;
    }
    if (glm::length(moveDir) > 0.001f)
        moveDir = glm::normalize(moveDir);

    game::net::PlayerInput netInput;
    netInput.moveDir = moveDir;
    m_lastMoveDir = moveDir;
    netInput.look = glm::vec2(glm::radians(yaw), glm::radians(pitch));

    // 跳跃
    if (!m_flyMode) {
        if (m_inputService->IsActionDown(A_Jump)) {
            m_jumpHoldTimer = 0.2f;
        }
    }
    m_jumpHoldTimer = std::max(0.0f, m_jumpHoldTimer - dt);
    netInput.jump = (!m_flyMode && m_jumpHoldTimer > 0.0f);

    // ==========================================
    // 3. 快捷栏切换 (使用合并后的 m_inventory)
    // ==========================================
    m_placeCooldown = std::max(0.0f, m_placeCooldown - dt);
    m_digCooldown = std::max(0.0f, m_digCooldown - dt);

    float scroll = m_inputService->GetScrollDelta();
    if (scroll > 0)
        m_hotbarIndex = (m_hotbarIndex - 1 + kHotbarSize) % kHotbarSize;
    if (scroll < 0)
        m_hotbarIndex = (m_hotbarIndex + 1) % kHotbarSize;

    if (m_inputService->IsActionPressed(A_HotbarSlot0))
        m_hotbarIndex = 0;
    if (m_inputService->IsActionPressed(A_HotbarSlot1))
        m_hotbarIndex = 1;
    if (m_inputService->IsActionPressed(A_HotbarSlot2))
        m_hotbarIndex = 2;
    if (m_inputService->IsActionPressed(A_HotbarSlot3))
        m_hotbarIndex = 3;
    if (m_inputService->IsActionPressed(A_HotbarSlot4))
        m_hotbarIndex = 4;
    if (m_inputService->IsActionPressed(A_HotbarSlot5))
        m_hotbarIndex = 5;
    if (m_inputService->IsActionPressed(A_HotbarSlot6))
        m_hotbarIndex = 6;
    if (m_inputService->IsActionPressed(A_HotbarSlot7))
        m_hotbarIndex = 7;
    if (m_inputService->IsActionPressed(A_HotbarSlot8))
        m_hotbarIndex = 8;
    if (m_hotbarIndex < 0 || m_hotbarIndex >= kHotbarSize)
        m_hotbarIndex = 0;

    // ==========================================
    // 4. 挖掘与放置
    // ==========================================
    bool wantDig = m_inputService->IsActionDown(A_Dig);
    bool wantPlace = m_inputService->IsActionDown(A_Place);

    glm::vec3 eyePos = cameraPos + glm::vec3(0.0f, PLAYER_EYE, 0.0f);
    m_currentRayHit = game::RayCast(m_blockRegistry, eyePos, cameraFront, 5.0f,
                                    [this](int wx, int wy, int wz) { return this->GetBlockAt(wx, wy, wz); });

    netInput.dig = (wantDig && m_digCooldown <= 0.0f);
    netInput.place = (wantPlace && m_placeCooldown <= 0.0f);

    if (netInput.dig)
        m_digCooldown = kDigInterval;
    if (netInput.place)
        m_placeCooldown = kPlaceInterval;

    // 从合并后的快捷栏 0~8 获取当前物品
    auto& curSlot = m_inventory[m_hotbarIndex];

    if (curSlot.isEmpty()) {
        netInput.placeBlock = AIR_ID;
    } else {
        netInput.placeBlock = curSlot.type;
        if (netInput.place) {
            // 本地预测扣除
            curSlot.count--;
            if (curSlot.count <= 0)
                curSlot = game::ItemStack{};

            // 发送放置请求时，服务端其实也应该通过 PlayerInput 扣除背包
            // 这里假设你服务端的 ApplyPlayerInput 已经处理了扣减逻辑
        }
    }
    netInput.fly = m_flyMode;

    // 发送移动与操作包
    Eng::tool::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::PlayerInput));
    w.WriteVec3(netInput.moveDir);
    w.WriteVec2(netInput.look);
    w.Write(static_cast<uint8_t>(netInput.jump ? 1 : 0));
    w.Write(static_cast<uint8_t>(netInput.dig ? 1 : 0));
    w.Write(static_cast<uint8_t>(netInput.place ? 1 : 0));
    w.Write<uint16_t>(netInput.placeBlock);
    w.Write(static_cast<uint8_t>(netInput.fly ? 1 : 0));
    m_client->Send(w.GetBuffer());

    // ==========================================
    // 5. 动画与反馈
    // ==========================================
    if (glm::length(moveDir) > 0.1f && m_playerOnGround) {
        m_walkDist += dt * 10.0f;
    } else {
        m_walkDist = 0.0f;
    }

    if (wantDig) {
        m_swingProgress += dt * 4.0f;
        if (m_swingProgress > 1.0f)
            m_swingProgress -= 1.0f;
    } else {
        m_swingProgress = 0.0f;
    }

    return m_shouldQuit;
}

void MyGame::UpdateActiveInputMode() {
    if (m_inputMode != InputMode::Auto) {
        m_activeInputMode = m_inputMode;
        return;
    }

    // 动态切换只在“游戏进行中且未打开菜单”时生效
    if (Eng::tool::IsMobilePlatform() && m_state == GameState::Playing && !m_menuOpen) {
        bool hasTouch = false;
        if (m_inputService) {
            for (const auto& t : m_inputService->GetAllTouches()) {
                if (t.active) {
                    hasTouch = true;
                    break;
                }
            }
        }

        if (hasTouch) {
            m_activeInputMode = InputMode::Touch;
        } else {
            auto device = m_inputService->GetLastInputDevice();
            if (device == Eng::service::InputService::InputDevice::KeyboardMouse) {
                m_activeInputMode = InputMode::KeyboardMouse;
            } else {
                m_activeInputMode = InputMode::Touch;  // 默认保持 Touch
            }
        }
    } else {
        // 主菜单 / 设置菜单：完全交由 ImGui 原生处理，底层设备状态供其他逻辑参考
        auto device = m_inputService->GetLastInputDevice();
        if (device == Eng::service::InputService::InputDevice::Touch)
            m_activeInputMode = InputMode::Touch;
        else if (device == Eng::service::InputService::InputDevice::KeyboardMouse)
            m_activeInputMode = InputMode::KeyboardMouse;
    }
}

// ============================================================
// 服务端消息处理
// ============================================================
void MyGame::HandleServerMessage(const std::vector<uint8_t>& data) {
    if (data.empty())
        return;

    auto type = static_cast<game::net::MessageType>(data[0]);
    Eng::tool::MessageReader r(std::span<const uint8_t>(data).subspan(1));

    switch (type) {
        case game::net::MessageType::WorldState: {
            uint32_t count = r.ReadU32BE();
            m_serverTickMs = r.Read<float>();
            m_serverTickAvg = m_serverTickAvg * 0.95f + m_serverTickMs * 0.05f;
            if (m_serverTickMs > m_serverTickMax)
                m_serverTickMax = m_serverTickMs;

            for (uint32_t i = 0; i < count; ++i) {
                game::net::PlayerState ps;
                ps.id = r.ReadU32BE();
                uint8_t nlen = r.Read<uint8_t>();
                auto nbytes = r.ReadBytes(nlen);
                ps.name.assign(nbytes.begin(), nbytes.end());
                ps.position = r.ReadVec3();
                ps.yaw = r.Read<float>();
                ps.pitch = r.Read<float>();

                if (ps.id == m_myClientId) {
                    glm::vec3 diff = ps.position - cameraPos;
                    if (glm::length(diff) > 3.0f) {
                        cameraPos = ps.position;
                        m_playerVelocity = glm::vec3(0.0f);
                        m_positionError = glm::vec3(0.0f);
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
            if (sy != CHUNK_SIZE_Y)
                return;

            auto bytes = r.ReadBytes(CHUNK_VOLUME * sizeof(BlockId));
            std::span<const BlockId> blockSpan(reinterpret_cast<const BlockId*>(bytes.data()), CHUNK_VOLUME);

            auto skyBytes = r.ReadBytes(CHUNK_VOLUME);
            auto blockLightBytes = r.ReadBytes(CHUNK_VOLUME * sizeof(LightRGB));

            // 优化：避免在栈上分配几百 KB 导致线程爆栈，使用 thread_local 复用内存
            static thread_local std::array<uint8_t, CHUNK_VOLUME> skyData;
            static thread_local std::array<LightRGB, CHUNK_VOLUME> blockLightData;

            std::memcpy(skyData.data(), skyBytes.data(), CHUNK_VOLUME);
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

            int cx = WorldToChunkX(bx);
            int cz = WorldToChunkZ(bz);
            int lx = WorldToLocalX(bx);
            int ly = by;
            int lz = WorldToLocalZ(bz);

            auto* pC = m_world.GetChunk(ChunkKey(cx, cz));
            if (pC && pC->chunk) {
                Chunk& chunk = *pC->chunk;

                BlockId oldB = ChunkGet(chunk, lx, ly, lz);
                ChunkSet(chunk, lx, ly, lz, t);

                LightEngine::OnBlockChanged(m_blockRegistry, chunk, lx, ly, lz, oldB, t);

                m_world.MarkDirty(ChunkKey(cx, cz));
                if (lx == 0)
                    m_world.MarkDirty(ChunkKey(cx - 1, cz));
                if (lx == 15)
                    m_world.MarkDirty(ChunkKey(cx + 1, cz));
                if (lz == 0)
                    m_world.MarkDirty(ChunkKey(cx, cz - 1));
                if (lz == 15)
                    m_world.MarkDirty(ChunkKey(cx, cz + 1));
            }
            break;
        }

        case game::net::MessageType::EntitySpawn: {
            uint32_t id = r.Read<uint32_t>();
            glm::vec3 pos = r.ReadVec3();
            BlockId bt = r.Read<uint16_t>();

            ClientItemEntity e;
            e.id = id;
            e.itemType = bt;
            e.renderPos = pos;
            e.prevPos = pos;
            e.targetPos = pos;
            e.interpT = 1.0f;
            m_itemEntities[id] = e;
            break;
        }

        case game::net::MessageType::EntityUpdate: {
            uint32_t id = r.Read<uint32_t>();
            glm::vec3 pos = r.ReadVec3();

            auto it = m_itemEntities.find(id);
            if (it != m_itemEntities.end()) {
                it->second.prevPos = it->second.renderPos;
                it->second.targetPos = pos;
                it->second.interpT = 0.0f;
            }
            break;
        }

        case game::net::MessageType::EntityDestroy: {
            uint32_t id = r.Read<uint32_t>();
            uint8_t reason = r.Read<uint8_t>();
            uint32_t pickerId = r.ReadU32BE();

            auto it = m_itemEntities.find(id);
            if (it != m_itemEntities.end()) {
                if (reason == 1) {
                    it->second.pickingUp = true;
                    it->second.pickupT = 0.0f;
                    it->second.pickupStart = it->second.renderPos;
                    it->second.pickerId = pickerId;
                } else {
                    m_itemEntities.erase(it);
                }
            }
            break;
        }

        case game::net::MessageType::ServerInfo: {
            uint32_t seed = r.ReadU32BE();
            glm::vec3 spawn = r.ReadVec3();

            m_worldSeed = seed;
            cameraPos = spawn;
            m_playerVelocity = glm::vec3(0.0f);
            m_positionError = glm::vec3(0.0f);

            m_lodManager.Start(seed);
            logInfo(logger,
                    "[LOD] Received world seed: " << seed << " spawn: " << spawn.x << "," << spawn.y << "," << spawn.z);
            break;
        }

        case game::net::MessageType::InventoryUpdate: {
            m_inventoryStateId = r.Read<int32_t>();
            // 接收 36 格背包
            for (int i = 0; i < 36; ++i) {
                m_inventory[i].type = r.Read<uint16_t>();
                m_inventory[i].count = r.Read<int32_t>();
            }
            // 接收鼠标手持物品
            m_heldItemStack.type = r.Read<uint16_t>();
            m_heldItemStack.count = r.Read<int32_t>();

            break;
        }

        case game::net::MessageType::PlayerInput:
        case game::net::MessageType::ChunkRequest:
        case game::net::MessageType::ClientHello:
        case game::net::MessageType::ClickContainer:
        case game::net::MessageType::SetSlot:
            break;
    }
}

// ============================================================
// Mesh 构建
// ============================================================
const Eng::client::RenderMesh& MyGame::GetOrCreateItemMesh(BlockId id) {
    auto it = m_itemMeshCache.find(id);
    if (it != m_itemMeshCache.end())
        return it->second;

    auto res = m_itemMeshCache.emplace(id, BuildItemCubeMesh(id));
    return res.first->second;
}

Eng::client::RenderMesh MyGame::BuildItemCubeMesh(BlockId id) {
    game::client::RawMeshData data = client::MeshBuilderUtils::BuildItemMeshData(m_blockRegistry, id);
    Eng::client::RenderMesh mesh;
    if (!data.vertices.empty()) {
        mesh.vbo = renderer->CreateBuffer({.size = data.vertices.size() * sizeof(Eng::service::Vertex),
                                           .usage = Eng::service::rhi::BufferUsage::Vertex});
        renderer->UpdateBuffer(mesh.vbo, 0, std::as_bytes(std::span(data.vertices)));
        mesh.ibo = renderer->CreateBuffer(
            {.size = data.indices.size() * sizeof(uint32_t), .usage = Eng::service::rhi::BufferUsage::Index});
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

    // 优化：使用 constexpr 或 static const 避免每次重建时重复构造
    static const glm::vec4 h_top(8, 0, 8, 8), h_bot(16, 0, 8, 8), h_right(0, 8, 8, 8), h_left(16, 8, 8, 8),
        h_front(8, 8, 8, 8), h_back(24, 8, 8, 8);
    static const glm::vec4 b_top(20, 16, 8, 4), b_bot(28, 16, 8, 4), b_right(16, 20, 4, 12), b_left(28, 20, 4, 12),
        b_front(20, 20, 8, 12), b_back(32, 20, 8, 12);
    static const glm::vec4 ra_top(44, 16, 4, 4), ra_bot(48, 16, 4, 4), ra_right(40, 20, 4, 12), ra_left(48, 20, 4, 12),
        ra_front(44, 20, 4, 12), ra_back(52, 20, 4, 12);
    static const glm::vec4 rl_top(4, 16, 4, 4), rl_bot(8, 16, 4, 4), rl_right(0, 20, 4, 12), rl_left(8, 20, 4, 12),
        rl_front(4, 20, 4, 12), rl_back(12, 20, 4, 12);

    AppendPlayerBox({0.0f, 1.55f, 0.0f}, {0.5f, 0.5f, 0.5f}, h_top, h_bot, h_right, h_left, h_front, h_back, vertices,
                    indices);
    AppendPlayerBox({0.0f, 1.05f, 0.0f}, {0.5f, 0.5f, 0.25f}, b_top, b_bot, b_right, b_left, b_front, b_back, vertices,
                    indices);
    AppendPlayerBox({-0.375f, 1.05f, 0.0f}, {0.25f, 0.5f, 0.25f}, ra_top, ra_bot, ra_right, ra_left, ra_front, ra_back,
                    vertices, indices);
    AppendPlayerBox({0.375f, 1.05f, 0.0f}, {0.25f, 0.5f, 0.25f}, ra_top, ra_bot, ra_right, ra_left, ra_front, ra_back,
                    vertices, indices);
    AppendPlayerBox({-0.125f, 0.4f, 0.0f}, {0.25f, 0.8f, 0.25f}, rl_top, rl_bot, rl_right, rl_left, rl_front, rl_back,
                    vertices, indices);
    AppendPlayerBox({0.125f, 0.4f, 0.0f}, {0.25f, 0.8f, 0.25f}, rl_top, rl_bot, rl_right, rl_left, rl_front, rl_back,
                    vertices, indices);

    Eng::client::RenderMesh mesh;
    mesh.vbo = renderer->CreateBuffer(
        {.size = vertices.size() * sizeof(Eng::service::Vertex), .usage = Eng::service::rhi::BufferUsage::Vertex});
    renderer->UpdateBuffer(mesh.vbo, 0, std::as_bytes(std::span(vertices)));
    mesh.ibo = renderer->CreateBuffer(
        {.size = indices.size() * sizeof(uint32_t), .usage = Eng::service::rhi::BufferUsage::Index});
    renderer->UpdateBuffer(mesh.ibo, 0, std::as_bytes(std::span(indices)));
    mesh.indexCount = static_cast<uint32_t>(indices.size());
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexed = true;
    return mesh;
}

Eng::client::RenderMesh MyGame::BuildHandMesh() {
    std::vector<Eng::service::Vertex> vertices;
    std::vector<uint32_t> indices;

    static const glm::vec4 ra_top(44, 16, 4, 4), ra_bot(48, 16, 4, 4), ra_right(40, 20, 4, 12), ra_left(48, 20, 4, 12),
        ra_front(44, 20, 4, 12), ra_back(52, 20, 4, 12);
    AppendPlayerBox({0.0f, 0.0f, 0.0f}, {0.25f, 0.25f, 0.6f}, ra_top, ra_bot, ra_right, ra_left, ra_front, ra_back,
                    vertices, indices);

    Eng::client::RenderMesh mesh;
    mesh.vbo = renderer->CreateBuffer(
        {.size = vertices.size() * sizeof(Eng::service::Vertex), .usage = Eng::service::rhi::BufferUsage::Vertex});
    renderer->UpdateBuffer(mesh.vbo, 0, std::as_bytes(std::span(vertices)));
    mesh.ibo = renderer->CreateBuffer(
        {.size = indices.size() * sizeof(uint32_t), .usage = Eng::service::rhi::BufferUsage::Index});
    renderer->UpdateBuffer(mesh.ibo, 0, std::as_bytes(std::span(indices)));
    mesh.indexCount = static_cast<uint32_t>(indices.size());
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexed = true;
    return mesh;
}

Eng::client::RenderMesh MyGame::BuildSkyboxMesh() {
    const float s = 1.0f;
    float verts[] = {
        s,  -s, -s, s,  -s, s, s,  s,  s,  s,  s,  s,  s,  s,  -s, s,  -s, -s, -s, -s, s,  -s, -s, -s, -s, s,  -s,
        -s, s,  -s, -s, s,  s, -s, -s, s,  -s, s,  -s, s,  s,  -s, s,  s,  s,  s,  s,  s,  -s, s,  s,  -s, s,  -s,
        -s, -s, s,  s,  -s, s, s,  -s, -s, s,  -s, -s, -s, -s, -s, -s, -s, s,  -s, -s, s,  s,  -s, s,  s,  s,  s,
        s,  s,  s,  -s, s,  s, -s, -s, s,  s,  -s, -s, -s, -s, -s, -s, s,  -s, -s, s,  -s, s,  s,  -s, s,  -s, -s,
    };
    std::vector<Eng::service::Vertex> v(36);
    for (size_t i = 0; i < 36; ++i) {
        v[i].position = {verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2]};
        v[i].normal = {0, 0, 0};
        v[i].uv = {0, 0};
        v[i].layer = 0.0f;
        v[i].ao = 1.0f;
        v[i].light = glm::vec4(1.0f);
    }

    Eng::client::RenderMesh mesh;
    mesh.vbo = renderer->CreateBuffer(
        {.size = v.size() * sizeof(Eng::service::Vertex), .usage = Eng::service::rhi::BufferUsage::Vertex});
    renderer->UpdateBuffer(mesh.vbo, 0, std::as_bytes(std::span(v)));
    mesh.vertexCount = static_cast<uint32_t>(v.size());
    mesh.indexed = false;
    return mesh;
}

// ============================================================
// 世界查询
// ============================================================
BlockId MyGame::GetBlockAt(int wx, int wy, int wz) const {
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
                if (IsSolidAt(x, y, z))
                    return true;
    return false;
}

// ============================================================
// 背包
// ============================================================
void MyGame::AddToInventory(BlockId id, int count) {
    if (id == AIR_ID || count <= 0)
        return;

    // 1. 优先填快捷栏 (0-8)
    for (int i = 0; i < 9; ++i) {
        if (m_inventory[i].type == id && m_inventory[i].count < 64) {
            int move = std::min(64 - m_inventory[i].count, count);
            m_inventory[i].count += move;
            count -= move;
            if (count <= 0)
                return;
        }
    }
    for (int i = 0; i < 9; ++i) {
        if (m_inventory[i].isEmpty()) {
            m_inventory[i].type = id;
            m_inventory[i].count = count;
            return;
        }
    }

    // 2. 其次填主背包 (9-35)
    for (int i = 9; i < 36; ++i) {
        if (m_inventory[i].type == id && m_inventory[i].count < 64) {
            int move = std::min(64 - m_inventory[i].count, count);
            m_inventory[i].count += move;
            count -= move;
            if (count <= 0)
                return;
        }
    }
    for (int i = 9; i < 36; ++i) {
        if (m_inventory[i].isEmpty()) {
            m_inventory[i].type = id;
            m_inventory[i].count = count;
            return;
        }
    }
}

// ============================================================
// Mesh 结果汇聚
// ============================================================
void MyGame::QueueChunkMeshTask(uint64_t key) {
    auto* pC = m_world.GetChunk(key);
    if (!pC || !pC->chunk)
        return;

    int cx = pC->chunk->chunk_x;
    int cz = pC->chunk->chunk_z;

    auto* cXm = m_world.GetChunk(ChunkKey(cx - 1, cz));
    auto* cXp = m_world.GetChunk(ChunkKey(cx + 1, cz));
    auto* cZm = m_world.GetChunk(ChunkKey(cx, cz - 1));
    auto* cZp = m_world.GetChunk(ChunkKey(cx, cz + 1));

    uint32_t currentVersion = ++m_chunkMeshVersions[key];

    m_meshManager.Submit(key, currentVersion, pC->chunk, cXm ? cXm->chunk : nullptr, cXp ? cXp->chunk : nullptr,
                         cZm ? cZm->chunk : nullptr, cZp ? cZp->chunk : nullptr);
}

void MyGame::DrainMeshResults(size_t maxCount) {
    if (!m_sceneRenderer)
        return;

    m_meshManager.DrainResults(maxCount, [this](game::client::MeshResult&& r) {
        auto it = m_chunkMeshVersions.find(r.chunkKey);
        if (it != m_chunkMeshVersions.end() && r.version < it->second) {
            return;
        }

        auto* cc = m_world.GetChunk(r.chunkKey);
        if (!cc)
            return;

        bool hasOpaque = !r.vertices.empty() && !r.indices.empty();
        bool hasWater = !r.waterVertices.empty() && !r.waterIndices.empty();

        if (hasOpaque) {
            m_sceneRenderer->OnChunkMeshReady(r.chunkKey, std::move(r.vertices), std::move(r.indices), cc->aabbMin,
                                              cc->aabbMax);
            m_knownSceneChunks.insert(r.chunkKey);
        } else {
            m_sceneRenderer->OnChunkUnloaded(r.chunkKey);
            m_knownSceneChunks.erase(r.chunkKey);
        }

        if (hasWater) {
            m_sceneRenderer->SubmitWaterChunk(r.chunkKey, std::move(r.waterVertices), std::move(r.waterIndices),
                                              cc->aabbMin, cc->aabbMax);
        } else {
            m_sceneRenderer->RemoveWaterChunk(r.chunkKey);
        }
    });
}

}  // namespace game