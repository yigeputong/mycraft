#include "game/client/ClientUI.h"
#include "game/Game.h"
#include "game/client/TextureAtlas.h"
#include <cmath>
#include <cstdio>
#include <tuple>

namespace game::client {

// ==========================================
// UI 辅助工具实现
// ==========================================
void ClientUI::DrawTextWithShadow(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text) {
    dl->AddText(ImVec2(pos.x + 2, pos.y + 2), IM_COL32(30, 30, 30, 255), text);
    dl->AddText(pos, color, text);
}

bool ClientUI::DrawButton(const char* label, ImVec2 size) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    bool clicked = ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered();

    ImU32 bgColor = hovered ? IM_COL32(120, 120, 120, 255) : IM_COL32(100, 100, 100, 255);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor);

    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, 255), 0.0f, 0, 2.0f);
    ImU32 innerColor = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 80, 80, 255);
    dl->AddRect(ImVec2(pos.x + 1, pos.y + 1), ImVec2(pos.x + size.x - 1, pos.y + size.y - 1), innerColor, 0.0f, 0, 2.0f);

    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 textPos = ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f);
    DrawTextWithShadow(dl, textPos, IM_COL32(255, 255, 255, 255), label);

    return clicked;
}

void ClientUI::DrawRotatedText(ImDrawList* dl, ImVec2 center, const char* text, float angleRad, ImU32 color) {
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 pos = ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
    
    int vtxStart = dl->VtxBuffer.Size;
    dl->AddText(ImVec2(pos.x + 2, pos.y + 2), IM_COL32(30, 30, 30, 255), text);
    dl->AddText(pos, color, text);
    int vtxEnd = dl->VtxBuffer.Size;

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

void ClientUI::DrawIsometricBlock(ImDrawList* dl, ImVec2 center, float size, BlockType type, ImTextureID texId) {
    if (type == BlockType::Air || texId == 0) return;

    int topTile   = TileForBlock(type, 2);
    int leftTile  = TileForBlock(type, 1);
    int rightTile = TileForBlock(type, 0);

    auto getUV = [&](int tile) {
        float u0 = (tile % ATLAS_COLS) * TILE_UV;
        float v0 = (tile / static_cast<float>(ATLAS_COLS)) * TILE_UV;
        float u1 = u0 + TILE_UV;
        float v1 = v0 + TILE_UV;
        return std::make_tuple(ImVec2(u0, v0), ImVec2(u1, v0), ImVec2(u1, v1), ImVec2(u0, v1));
    };

    float w = size * 0.5f;
    float h = size * 0.25f;
    float d = size * 0.5f;

    float cx = center.x;
    float cy = center.y; 
    float totalHeight = h * 2 + d;
    float topY = cy - (totalHeight * 0.5f);
    float midY = topY + h;
    float botY = midY + h;

    auto [lu0, lu1, lu2, lu3] = getUV(leftTile);
    dl->AddImageQuad(texId, ImVec2(cx - w, midY), ImVec2(cx, midY - h), ImVec2(cx, midY - h + d), ImVec2(cx - w, midY + d), lu0, lu1, lu2, lu3);

    auto [ru0, ru1, ru2, ru3] = getUV(rightTile);
    dl->AddImageQuad(texId, ImVec2(cx, midY - h), ImVec2(cx + w, midY), ImVec2(cx + w, midY + d), ImVec2(cx, midY - h + d), ru0, ru1, ru2, ru3);

    auto [tu0, tu1, tu2, tu3] = getUV(topTile);
    dl->AddImageQuad(texId, ImVec2(cx, topY), ImVec2(cx + w, midY), ImVec2(cx, botY), ImVec2(cx - w, midY), tu0, tu1, tu2, tu3);
}


// ==========================================
// 主入口与界面实现
// ==========================================
void ClientUI::Draw(Eng::Engine& engine, MyGame& game) {
    game.renderer->ImGuiNewFrame();
    ImGui::NewFrame();

    // 全局暗色遮罩
    if (game.m_menuOpen || game.m_state == MyGame::GameState::MainMenu) {
        ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
        bgDrawList->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(10, 10, 10, 160));
    }

    if (!game.m_menuOpen) {
        if (game.m_state == MyGame::GameState::MainMenu) {
            DrawMainMenu(engine, game);
        } else if (game.m_state == MyGame::GameState::Playing) {
            DrawHUD(engine, game);
            // 背包 UI 预留
            // if (game.m_inventoryOpen) DrawInventory(engine, game);
        }
    }

    if (game.m_menuOpen) {
        DrawSettingsMenu(engine, game);
    }
}

void ClientUI::DrawMainMenu([[maybe_unused]] Eng::Engine& engine, MyGame& game) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 160));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##MainMenuRoot", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

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
        if (game.StartLocalServer(true, true)) {
            game.m_state = MyGame::GameState::Playing;
            game.m_statusText.clear();
        } else {
            game.m_statusText = "Failed to start local server";
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(startX, startY + 50));
    if (DrawButton("Multiplayer", ImVec2(menuWidth, buttonHeight))) {
        game.m_showMultiplayerDialog = true;
    }

    ImGui::SetCursorScreenPos(ImVec2(startX, startY + 110));
    if (DrawButton("Options...", ImVec2(menuWidth * 0.48f, buttonHeight))) {
        game.m_menuOpen = true;
    }
    ImGui::SetCursorScreenPos(ImVec2(startX + menuWidth * 0.52f, startY + 110));
    if (DrawButton("Quit Game", ImVec2(menuWidth * 0.48f, buttonHeight))) {
        game.m_shouldQuit = true;
    }

    ImGui::GetWindowDrawList()->AddText(ImVec2(10, io.DisplaySize.y - 30), IM_COL32(200, 200, 200, 255), "Mycraft v0.0.1");
    const char* copyright = "Copyright Mycraft Team. Open Source Project.";
    ImVec2 copySize = ImGui::CalcTextSize(copyright);
    ImGui::GetWindowDrawList()->AddText(ImVec2(io.DisplaySize.x - copySize.x - 10, io.DisplaySize.y - 30), IM_COL32(200, 200, 200, 255), copyright);

    if (!game.m_statusText.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(startX, startY + 170));
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", game.m_statusText.c_str());
    }
    ImGui::End();

    // 多人连接弹窗
    if (game.m_showMultiplayerDialog) ImGui::OpenPopup("Connect to Server");
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Connect to Server", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Address", game.m_ipBuffer, sizeof(game.m_ipBuffer));
        ImGui::InputInt("Port", &game.m_portInput);
        ImGui::Spacing();
        if (ImGui::Button("Connect", ImVec2(120, 40))) {
            if (game.ConnectToServer(game.m_ipBuffer, (uint16_t)game.m_portInput)) {
                game.m_state = MyGame::GameState::Playing;
                game.m_showMultiplayerDialog = false;
                game.m_statusText.clear();
                ImGui::CloseCurrentPopup();
            } else {
                game.m_statusText = "Connection failed";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 40))) {
            game.m_showMultiplayerDialog = false;
            game.m_statusText.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ClientUI::DrawSettingsMenu(Eng::Engine& engine, MyGame& game) {
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
    ImGui::Begin("##SettingsRoot", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

    ImGui::SetCursorPos(ImVec2(20, 20));
    ImGui::SetNextItemWidth(300);
    static char searchBuf[128] = "";
    ImGui::InputTextWithHint("##Search", "Search options...", searchBuf, sizeof(searchBuf));

    ImGui::SameLine(io.DisplaySize.x - 50);
    if (ImGui::Button("X", ImVec2(30, 30))) {
        game.m_menuOpen = false;
    }

    ImGui::Separator();
    ImGui::Spacing();

    float leftWidth = 250.0f;
    float contentHeight = io.DisplaySize.y - 140.0f;

    // 左侧导航
    ImGui::BeginChild("##LeftPanel", ImVec2(leftWidth, contentHeight), true, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Spacing();
        if (game.m_settingsTab == 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.8f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        if (ImGui::Selectable("    Video", game.m_settingsTab == 0)) game.m_settingsTab = 0;
        ImGui::PopStyleColor();

        if (game.m_settingsTab == 1) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.8f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        if (ImGui::Selectable("    Audio", game.m_settingsTab == 1)) game.m_settingsTab = 1;
        ImGui::PopStyleColor();

        if (game.m_settingsTab == 2) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.8f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        if (ImGui::Selectable("    Controls", game.m_settingsTab == 2)) game.m_settingsTab = 2;
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // 右侧面板
    ImGui::BeginChild("##RightPanel", ImVec2(0, contentHeight), true, ImGuiWindowFlags_NoScrollbar);
    {
        auto& wcfg = game.mainWin->GetConfigs();
        auto& rcfg = engine.GetConfig().render;
        auto& ecfg = engine.GetConfig().window;

        if (game.m_settingsTab == 0) {
            ImGui::Spacing();
            ImGui::Text("Render Distance"); ImGui::SameLine(leftWidth * 1.2f);
            ImGui::SetNextItemWidth(150);
            ImGui::SliderInt("##rd", &game.m_renderDistance, 2, 32, "%d chunks");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Fullscreen Mode"); ImGui::SameLine(leftWidth * 1.2f);
            bool fs = wcfg.fullscreen;
            if (ImGui::Checkbox("##fs", &fs)) {
                wcfg.fullscreen = fs;
                game.mainWin->SetFullscreen(fs);
                game.renderer->WaitIdle();
                game.RecreateFBOs(
                    game.mainWin->GetConfigs().windowPixelWidth,
                    game.mainWin->GetConfigs().windowPixelHeight
                );
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Vertical Sync"); ImGui::SameLine(leftWidth * 1.2f);
            if (ImGui::Checkbox("##vsync", &wcfg.vsync)) {
                game.m_applyPending = true;
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

            // 图形 API 设置
            const char* apiDesc = "选择渲染使用的图形API。除非有特殊需求需要更改，否则请维持此设置为默认不变！";
            ImVec4 descColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); 
            if (ecfg.apitype == Eng::client::RenderAPItype::VULKAN) {
                apiDesc = "实验性设置。在部分系统上使用可能会降低性能或导致不稳定。";
                descColor = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
            }

            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(descColor, "%s", apiDesc);
            ImGui::PopTextWrapPos();
            ImGui::Spacing();

            ImGui::Text("图形 API"); ImGui::SameLine(leftWidth * 1.2f);
            ImGui::SetNextItemWidth(300.0f);
            int currentApi = static_cast<int>(ecfg.apitype);
            const char* apiItems[] = { "默认", "优先使用 OpenGL", "优先使用 Vulkan" };
            if (ImGui::Combo("##api", &currentApi, apiItems, IM_ARRAYSIZE(apiItems))) {
                wcfg.apitype = static_cast<Eng::client::RenderAPItype>(currentApi);
                engine.GetConfig().window.apitype = wcfg.apitype;
                game.m_apiChanged = true;
            }

            if (game.m_apiChanged) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "需要重启游戏以应用新的图形 API！");
            }
        } else if (game.m_settingsTab == 1) {
            ImGui::Spacing();
            ImGui::TextDisabled("Audio settings coming soon...");
        } else if (game.m_settingsTab == 2) {
            ImGui::Spacing();
            ImGui::TextDisabled("Controls settings coming soon...");
        }
    }
    ImGui::EndChild();

    // 底部按钮
    float bottomY = io.DisplaySize.y - 60.0f;
    ImGui::SetCursorPos(ImVec2(20.0f, bottomY));
    
    const char* leftBtnText = (game.m_state == MyGame::GameState::Playing) ? "Save & Quit to Title" : "Back to Menu";
    float leftBtnWidth = ImGui::CalcTextSize(leftBtnText).x + 40.0f;
    if (leftBtnWidth < 150.0f) leftBtnWidth = 150.0f; 

    if (ImGui::Button(leftBtnText, ImVec2(leftBtnWidth, 40))) {
        if (game.m_state == MyGame::GameState::Playing) {
            game.m_returnToMenuPending = true;
        } else {
            game.m_menuOpen = false;
        }
    }

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
        game.m_applyPending = true;
        game.SaveSettings(engine);
    }
    ImGui::SameLine();
    if (ImGui::Button(btnDoneText, ImVec2(doneWidth, 40))) {
        game.m_applyPending = true;
        game.m_menuOpen = false;
        game.SaveSettings(engine);
    }

    ImGui::End();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(11);
}

void ClientUI::DrawHUD([[maybe_unused]] Eng::Engine& engine, MyGame& game) {
    ImGuiIO& io = ImGui::GetIO();
    
    // ---- 准心 ----
    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    constexpr float LEN = 8.0f, THICK = 2.0f, GAP = 2.0f;
    ImU32 color = IM_COL32(255, 255, 255, 200);
    dl->AddLine(ImVec2(center.x - GAP - LEN, center.y), ImVec2(center.x - GAP, center.y), color, THICK);
    dl->AddLine(ImVec2(center.x + GAP, center.y), ImVec2(center.x + GAP + LEN, center.y), color, THICK);
    dl->AddLine(ImVec2(center.x, center.y - GAP - LEN), ImVec2(center.x, center.y - GAP), color, THICK);
    dl->AddLine(ImVec2(center.x, center.y + GAP), ImVec2(center.x, center.y + GAP + LEN), color, THICK);

    // ---- 物品栏 ----
    constexpr float SLOT = 50.0f, GAP_ITEM = 4.0f;
    float totalW = MyGame::kHotbarSize * SLOT + (MyGame::kHotbarSize - 1) * GAP_ITEM;
    float startX = (io.DisplaySize.x - totalW) * 0.5f;
    float y = io.DisplaySize.y - SLOT - 20.0f;
    

    for (int i = 0; i < MyGame::kHotbarSize; ++i) {
        float x = startX + i * (SLOT + GAP_ITEM);
        ImVec2 p0(x, y), p1(x + SLOT, y + SLOT);

        ImU32 bg = (i == game.m_hotbarIndex) ? IM_COL32(255, 255, 255, 200) : IM_COL32(0, 0, 0, 120);
        dl->AddRectFilled(p0, p1, bg);
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 0, 0, 2.0f);

        const auto& slot = game.m_hotbar[i];
        if (slot.isEmpty()) continue;

        ImU32 itemColor = IM_COL32(150, 150, 150, 255); // 默认灰色
        if (slot.type == BlockType::Dirt) itemColor = IM_COL32(110, 75, 50, 255);
        else if (slot.type == BlockType::GrassBlock) itemColor = IM_COL32(88, 160, 70, 255);
        else if (slot.type == BlockType::Sand) itemColor = IM_COL32(220, 210, 160, 255);
        else if (slot.type == BlockType::Stone) itemColor = IM_COL32(128, 128, 128, 255);
        
        // 画个内嵌小方块
        dl->AddRectFilled(ImVec2(p0.x + 8, p0.y + 8), ImVec2(p1.x - 8, p1.y - 8), itemColor);
        
        // 画个首字母
        const char* name = BlockName(slot.type);
        char firstLetter[2] = { name[0], '\0' };
        ImVec2 textSize = ImGui::CalcTextSize(firstLetter);
        dl->AddText(ImVec2(p0.x + SLOT * 0.5f - textSize.x * 0.5f, p0.y + SLOT * 0.5f - textSize.y * 0.5f), 
                    IM_COL32(255, 255, 255, 255), firstLetter);

        if (slot.count > 1) {
            char cnt[8];
            std::snprintf(cnt, sizeof(cnt), "%d", slot.count);
            dl->AddText(ImVec2(x + SLOT - 20, y + 4), IM_COL32(255, 255, 255, 255), cnt);
        }
    }

    // ---- F3 调试面板 ----
    if (game.m_showDebug) {
        auto DrawLineBgLeft = [&](float x, float y, ImU32 color, const char* fmt, ...) {
            char buf[512];
            va_list args; va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);

            ImVec2 textSize = ImGui::CalcTextSize(buf);
            float padX = 4.0f;
            float padY = 1.0f;
            dl->AddRectFilled(ImVec2(x - padX, y - padY), 
                              ImVec2(x + textSize.x + padX, y + textSize.y + padY), 
                              IM_COL32(0, 0, 0, 130));
            dl->AddText(ImVec2(x, y), color, buf);
        };

        auto DrawLineBgRight = [&](float rightX, float y, ImU32 color, const char* fmt, ...) {
            char buf[512];
            va_list args; va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);

            ImVec2 textSize = ImGui::CalcTextSize(buf);
            float x = rightX - textSize.x;
            float padX = 4.0f;
            float padY = 1.0f;
            dl->AddRectFilled(ImVec2(x - padX, y - padY), 
                              ImVec2(rightX + padX, y + textSize.y + padY), 
                              IM_COL32(0, 0, 0, 130));
            dl->AddText(ImVec2(x, y), color, buf);
        };

        float dbgY = 10.0f;
        float lineHeight = ImGui::GetTextLineHeight() + 4.0f; 
        ImU32 white = IM_COL32(255, 255, 255, 255);
        ImU32 green = IM_COL32(85, 255, 85, 255);

        // 左侧信息
        float leftX = 14.0f;
        DrawLineBgLeft(leftX, dbgY, green, "Mycraft v0.0.1"); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "%.0f fps | Frame: %.2f ms", game.m_fpsDisplay, game.m_frameMsDisplay); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Server tick: %.2f ms", game.m_serverTickMs); dbgY += lineHeight;
        dbgY += lineHeight * 0.5f;
        DrawLineBgLeft(leftX, dbgY, white, "XYZ: %.3f / %.3f / %.3f", game.cameraPos.x, game.cameraPos.y, game.cameraPos.z); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Yaw/Pitch: %.1f / %.1f", game.yaw, game.pitch); dbgY += lineHeight;
        
        int pcx = static_cast<int>(std::floor(game.cameraPos.x)) >> 4;
        int pcz = static_cast<int>(std::floor(game.cameraPos.z)) >> 4;
        DrawLineBgLeft(leftX, dbgY, white, "Chunk: %d %d", pcx, pcz); dbgY += lineHeight;
        dbgY += lineHeight * 0.5f;

        DrawLineBgLeft(leftX, dbgY, white, "Chunks loaded: %zu", game.m_world.GetChunks().size()); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Pending: %zu", game.m_world.GetPendingSize()); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Render Distance: %d chunks", game.m_renderDistance); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Player ID: %d", game.m_myClientId); dbgY += lineHeight;
        dbgY += lineHeight * 0.5f;

        int hh = (int)game.m_timeOfDay;
        int mm = (int)((game.m_timeOfDay - hh) * 60);
        DrawLineBgLeft(leftX, dbgY, white, "Time: %02d:%02d", hh, mm);

        // 右侧信息
        float rightX = io.DisplaySize.x - 14.0f;
        dbgY = 10.0f;
        DrawLineBgRight(rightX, dbgY, white, "Backend: %s", game.m_deviceInfo.backend.c_str()); dbgY += lineHeight;
        DrawLineBgRight(rightX, dbgY, white, "GPU: %s", game.m_deviceInfo.deviceName.c_str()); dbgY += lineHeight;
        DrawLineBgRight(rightX, dbgY, white, "Vendor: %s", game.m_deviceInfo.vendor.c_str()); dbgY += lineHeight;
        DrawLineBgRight(rightX, dbgY, white, "API: %s", game.m_deviceInfo.apiVersion.c_str()); dbgY += lineHeight;
        DrawLineBgRight(rightX, dbgY, white, "Driver: %s", game.m_deviceInfo.driverVersion.c_str()); dbgY += lineHeight;
        DrawLineBgRight(rightX, dbgY, white, "Shader: %s", game.m_deviceInfo.shadingLanguage.c_str());
    }
}

} // namespace game::client