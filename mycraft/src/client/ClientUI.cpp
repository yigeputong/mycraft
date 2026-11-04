#include "game/client/ClientUI.h"
#include "game/Game.h"
#include "game/client/TextureAtlas.h"
#include "imgui.h"
#include "Engine/tool/Window.h"
#include "Engine/tool/Platform.h"
#include <cmath>
#include <cstdio>
#include <tuple>
#include <cstdarg>

namespace game::client {

namespace {
    // ==========================================
    // UI 尺寸与颜色常量（基于 FontSize 动态缩放）
    // ==========================================
    constexpr float PADDING_RATIO           = 1.0f;   // 基础内边距
    constexpr float LEFT_PANEL_WIDTH_RATIO  = 10.0f;  // 左侧导航栏宽度
    constexpr float ITEM_WIDTH_RATIO        = 10.0f;  // 右侧控件宽度
    constexpr float BUTTON_HEIGHT_RATIO     = 1.75f;  // 按钮高度
    constexpr float MIN_BTN_WIDTH_RATIO     = 6.0f;   // 按钮最小宽度
    constexpr float TEXT_WRAP_RATIO         = 15.0f;  // 文本换行宽度

    // 移动端安全区比例（避开状态栏、导航条、曲面屏边缘）
    constexpr float SAFE_TOP_RATIO          = 1.5f;   // 顶部状态栏高度占比
    constexpr float SAFE_BOTTOM_RATIO       = 2.5f;   // 底部导航栏高度占比
    constexpr float SAFE_SIDE_RATIO         = 1.0f;   // 左右边缘安全距离

    // 主题颜色
    constexpr ImVec4 COLOR_ACCENT      = ImVec4(0.20f, 0.80f, 0.80f, 1.00f); // 青色
    constexpr ImVec4 COLOR_TEXT_ACTIVE = ImVec4(0.20f, 0.80f, 0.80f, 1.00f);
    constexpr ImVec4 COLOR_TEXT_MUTED  = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    constexpr ImVec4 COLOR_WARNING     = ImVec4(1.00f, 0.80f, 0.20f, 1.00f);
    constexpr ImVec4 COLOR_ERROR       = ImVec4(1.00f, 0.30f, 0.30f, 1.00f);
}

// ==========================================
// UI 辅助工具实现
// ==========================================
void ClientUI::DrawTextWithShadow(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text) {
    float offset = ImGui::GetFontSize() * 0.1f;
    dl->AddText(ImVec2(pos.x + offset, pos.y + offset), IM_COL32(30, 30, 30, 255), text);
    dl->AddText(pos, color, text);
}

bool ClientUI::DrawButton(const char* label, ImVec2 size) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    bool clicked = ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered();

    ImU32 bgColor = hovered ? IM_COL32(120, 120, 120, 255) : IM_COL32(100, 100, 100, 255);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor);

    float borderThickness = std::max(1.0f, ImGui::GetFontSize() * 0.1f);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, 255), 0.0f, 0, borderThickness * 2.0f);
    ImU32 innerColor = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 80, 80, 255);
    dl->AddRect(ImVec2(pos.x + borderThickness, pos.y + borderThickness), 
                ImVec2(pos.x + size.x - borderThickness, pos.y + size.y - borderThickness), 
                innerColor, 0.0f, 0, borderThickness);

    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 textPos = ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f);
    DrawTextWithShadow(dl, textPos, IM_COL32(255, 255, 255, 255), label);

    return clicked;
}

void ClientUI::DrawRotatedText(ImDrawList* dl, ImVec2 center, const char* text, float angleRad, ImU32 color) {
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 pos = ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
    
    int vtxStart = dl->VtxBuffer.Size;
    float offset = ImGui::GetFontSize() * 0.1f;
    dl->AddText(ImVec2(pos.x + offset, pos.y + offset), IM_COL32(30, 30, 30, 255), text);
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

void ClientUI::DrawIsometricBlock(ImDrawList* dl, ImVec2 center, float size, BlockId id, ImTextureID texId, const BlockRegistry& reg) {
    if (id == AIR_ID || texId == 0) return;

    const auto& def = reg.Get(id);
    
    int topTile   = ResolveTile(def.render.ForTop().tile);
    int sideTile  = ResolveTile(def.render.ForSide().tile);
    
    if (topTile < 0) topTile = 0;
    if (sideTile < 0) sideTile = 0;

    auto getUV = [&](int tile) {
        float u0, v0, u1, v1;
        GetTileUV(tile, u0, v0, u1, v1);
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

    auto [lu0, lu1, lu2, lu3] = getUV(sideTile);
    dl->AddImageQuad(texId, ImVec2(cx - w, midY), ImVec2(cx, midY - h), ImVec2(cx, midY - h + d), ImVec2(cx - w, midY + d), lu0, lu1, lu2, lu3);

    auto [ru0, ru1, ru2, ru3] = getUV(sideTile);
    dl->AddImageQuad(texId, ImVec2(cx, midY - h), ImVec2(cx + w, midY), ImVec2(cx + w, midY + d), ImVec2(cx, midY - h + d), ru0, ru1, ru2, ru3);

    auto [tu0, tu1, tu2, tu3] = getUV(topTile);
    dl->AddImageQuad(texId, ImVec2(cx, topY), ImVec2(cx + w, midY), ImVec2(cx, botY), ImVec2(cx - w, midY), tu0, tu1, tu2, tu3);
}


// ==========================================
// 主入口与界面实现
// ==========================================
void ClientUI::Draw(Eng::Engine& engine, MyGame& game) {
    game.renderer->ImGuiNewFrame();

    if (game.m_menuOpen || game.m_state == MyGame::GameState::MainMenu) {
        ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
        bgDrawList->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(10, 10, 10, 160));
    }

    if (!game.m_menuOpen) {
        if (game.m_state == MyGame::GameState::MainMenu) {
            DrawMainMenu(engine, game);
        } else if (game.m_state == MyGame::GameState::Playing) {
            DrawHUD(engine, game);
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

    float fontSize = ImGui::GetFontSize();
    
    float safeTop = 0.0f;
    float safeBottom = 0.0f;
    if (Eng::tool::IsMobilePlatform()) {
        safeTop = fontSize * SAFE_TOP_RATIO;
        safeBottom = fontSize * SAFE_BOTTOM_RATIO;
    }

    // 整体尺寸
    float menuWidth = fontSize * 15.0f;      
    float buttonHeight = fontSize * 1.8f;    
    float buttonGap = fontSize * 0.6f;       
    
    // 动态标题防溢出（基础缩放调小）
    float titleTextBaseWidth = ImGui::CalcTextSize("MYCRAFT").x;
    float desiredScale = 2.0f;               
    if (titleTextBaseWidth * desiredScale > io.DisplaySize.x * 0.8f) {
        desiredScale = (io.DisplaySize.x * 0.8f) / titleTextBaseWidth;
    }
    
    ImGui::PushFont(ImGui::GetFont(), fontSize * desiredScale);
    ImVec2 titleSize = ImGui::CalcTextSize("MYCRAFT");
    ImGui::PopFont();
    
    // 间距
    float titleToButtonsSpace = fontSize * 2.0f; 

    // 计算整体垂直居中时，把底部的版本号空间也算进去，防止遮挡
    float totalButtonsHeight = buttonHeight * 3 + buttonGap * 2;
    float versionSpace = fontSize * 3.0f; // 预留给底部版本号的间距
    float totalContentHeight = titleSize.y + titleToButtonsSpace + totalButtonsHeight + versionSpace;
    
    float usableHeight = io.DisplaySize.y - safeTop - safeBottom;
    float blockStartY = safeTop + (usableHeight - totalContentHeight) * 0.5f;
    
    if (blockStartY < safeTop + fontSize * 2.0f) blockStartY = safeTop + fontSize * 2.0f;

    float startX = (io.DisplaySize.x - menuWidth) * 0.5f;
    ImVec2 titlePos = ImVec2((io.DisplaySize.x - titleSize.x) * 0.5f, blockStartY);
    
    // 标题
    ImGui::PushFont(ImGui::GetFont(), fontSize * desiredScale);
    DrawTextWithShadow(ImGui::GetWindowDrawList(), titlePos, IM_COL32(200, 200, 200, 255), "MYCRAFT");
    ImGui::PopFont();
    
    // 副标题
    const char* splash = "Only in Mycraft!";
    float splashAngle = -0.3f;
    ImVec2 splashCenter(titlePos.x + titleSize.x - fontSize * 2.0f, titlePos.y + titleSize.y - fontSize * 0.5f);
    DrawRotatedText(ImGui::GetWindowDrawList(), splashCenter, splash, splashAngle, IM_COL32(255, 255, 0, 255));

    float buttonsStartY = blockStartY + titleSize.y + titleToButtonsSpace;

    ImGui::SetCursorScreenPos(ImVec2(startX, buttonsStartY));
    if (DrawButton("Single Player", ImVec2(menuWidth, buttonHeight))) {
        if (game.StartLocalServer(true, true)) {
            game.EnterGame();
            game.m_statusText.clear();
        } else {
            game.m_statusText = "Failed to start local server";
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(startX, buttonsStartY + buttonHeight + buttonGap));
    if (DrawButton("Multiplayer", ImVec2(menuWidth, buttonHeight))) {
        game.m_showMultiplayerDialog = true;
    }

    ImGui::SetCursorScreenPos(ImVec2(startX, buttonsStartY + (buttonHeight + buttonGap) * 2));
    if (DrawButton("Options...", ImVec2(menuWidth * 0.48f, buttonHeight))) {
        game.m_menuOpen = true;
    }
    ImGui::SetCursorScreenPos(ImVec2(startX + menuWidth * 0.52f, buttonsStartY + (buttonHeight + buttonGap) * 2));
    if (DrawButton("Quit Game", ImVec2(menuWidth * 0.48f, buttonHeight))) {
        game.m_shouldQuit = true;
    }

    if (!game.m_statusText.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(startX, buttonsStartY + (buttonHeight + buttonGap) * 3));
        ImGui::TextColored(COLOR_ERROR, "%s", game.m_statusText.c_str());
    }

    // 底部版本号
    float bottomTextY = io.DisplaySize.y - safeBottom - fontSize * 1.5f;
    const char* versionStr = "Mycraft v" MYCRAFT_VERSION;
    ImVec2 versionSize = ImGui::CalcTextSize(versionStr);
    ImGui::GetWindowDrawList()->AddText(ImVec2((io.DisplaySize.x - versionSize.x) * 0.5f, bottomTextY), 
        IM_COL32(150, 150, 150, 255), versionStr);

    ImGui::End();

    // 多人弹窗
    if (game.m_showMultiplayerDialog) ImGui::OpenPopup("Connect to Server");
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fontSize * 25.0f, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Connect to Server", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Address", game.m_ipBuffer, sizeof(game.m_ipBuffer));
        ImGui::InputInt("Port", &game.m_portInput);
        ImGui::Spacing();
        if (ImGui::Button("Connect", ImVec2(fontSize * 6.0f, buttonHeight))) {
            if (game.ConnectToServer(game.m_ipBuffer, (uint16_t)game.m_portInput)) {
                game.EnterGame();
                game.m_showMultiplayerDialog = false;
                game.m_statusText.clear();
                ImGui::CloseCurrentPopup();
            } else {
                game.m_statusText = "Connection failed";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fontSize * 6.0f, buttonHeight))) {
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
    ImGui::PushStyleColor(ImGuiCol_CheckMark, COLOR_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, COLOR_ACCENT);
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

    const float fontSize = ImGui::GetFontSize();
    const float padding = fontSize * PADDING_RATIO;
    const float buttonHeight = fontSize * BUTTON_HEIGHT_RATIO;
    const float leftWidth = fontSize * LEFT_PANEL_WIDTH_RATIO;

    float safeTop = 0.0f, safeBottom = 0.0f, safeSide = 0.0f;
    if (Eng::tool::IsMobilePlatform()) {
        safeTop = fontSize * SAFE_TOP_RATIO;
        safeBottom = fontSize * SAFE_BOTTOM_RATIO;
        safeSide = fontSize * SAFE_SIDE_RATIO;
    }

    // =========================================================
    // 1. 顶部单行工具栏
    // =========================================================
    ImGui::SetCursorPos(ImVec2(padding + safeSide, padding + safeTop));
    
    const char* leftBtnText = (game.m_state == MyGame::GameState::Playing) ? "Save & Quit to Title" : "Back to Menu";
    float leftBtnWidth = ImGui::CalcTextSize(leftBtnText).x + fontSize * PADDING_RATIO * 2.0f;
    if (leftBtnWidth < fontSize * ITEM_WIDTH_RATIO) leftBtnWidth = fontSize * ITEM_WIDTH_RATIO; 

    if (ImGui::Button(leftBtnText, ImVec2(leftBtnWidth, buttonHeight))) {
        if (game.m_state == MyGame::GameState::Playing) {
            game.m_returnToMenuPending = true;
        } else {
            game.m_menuOpen = false;
        }
    }

    ImGui::SameLine();
    const char* btnApplyText = "Apply";
    float applyWidth = ImGui::CalcTextSize(btnApplyText).x + fontSize * PADDING_RATIO * 2.0f;
    if (applyWidth < fontSize * MIN_BTN_WIDTH_RATIO) applyWidth = fontSize * MIN_BTN_WIDTH_RATIO;
    if (ImGui::Button(btnApplyText, ImVec2(applyWidth, buttonHeight))) {
        game.m_applyPending = true;
        game.SaveSettings(engine);
    }

    ImGui::SameLine();
    const char* btnDoneText = "Done";
    float doneWidth = ImGui::CalcTextSize(btnDoneText).x + fontSize * PADDING_RATIO * 2.0f;
    if (doneWidth < fontSize * MIN_BTN_WIDTH_RATIO) doneWidth = fontSize * MIN_BTN_WIDTH_RATIO;
    if (ImGui::Button(btnDoneText, ImVec2(doneWidth, buttonHeight))) {
        game.m_applyPending = true;
        game.m_menuOpen = false;
        game.SaveSettings(engine);
    }

    // 中间搜索框区
    ImGui::SameLine();
    float availWidth = ImGui::GetContentRegionAvail().x;
    
    float searchWidth = availWidth - safeSide - buttonHeight - (ImGui::GetStyle().ItemSpacing.x * 2.0f);
    if (searchWidth < fontSize * 10.0f) searchWidth = fontSize * 10.0f;

    float calculatedFramePaddingY = (buttonHeight - fontSize) * 0.5f;
    if (calculatedFramePaddingY < 0.0f) calculatedFramePaddingY = 0.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, calculatedFramePaddingY));
    ImGui::SetNextItemWidth(searchWidth);
    static char searchBuf[128] = "";
    ImGui::InputTextWithHint("##Search", "Search options...", searchBuf, sizeof(searchBuf));
    ImGui::PopStyleVar();

    // 右侧关闭按钮 (使用 safeSide 对齐)
    ImGui::SameLine(io.DisplaySize.x - padding - safeSide - buttonHeight);
    if (ImGui::Button("X", ImVec2(buttonHeight, buttonHeight))) {
        game.m_menuOpen = false;
    }

    ImGui::Separator();
    ImGui::Spacing();

    // =========================================================
    // 2. 中间面板区（占据所有剩余高度）
    // =========================================================
    const float contentStartY = ImGui::GetCursorPosY(); 
    float contentHeight = io.DisplaySize.y - contentStartY - safeBottom - padding; 
    if (contentHeight < fontSize * LEFT_PANEL_WIDTH_RATIO) contentHeight = fontSize * LEFT_PANEL_WIDTH_RATIO;

    // 左侧导航
    ImGui::SetCursorPosX(padding + safeSide);
    ImGui::BeginChild("##LeftPanel", ImVec2(leftWidth, contentHeight), true, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Spacing();
        auto DrawTab = [&](const char* label, int tabId) {
            if (game.m_settingsTab == tabId) ImGui::PushStyleColor(ImGuiCol_Text, COLOR_TEXT_ACTIVE);
            else ImGui::PushStyleColor(ImGuiCol_Text, COLOR_TEXT_MUTED);
            if (ImGui::Selectable(label, game.m_settingsTab == tabId)) game.m_settingsTab = tabId;
            ImGui::PopStyleColor();
        };
        DrawTab("    Video", 0);
        DrawTab("    Audio", 1);
        DrawTab("    Controls", 2);
        DrawTab("    About", 3);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // 右侧面板
    float rightPanelWidth = ImGui::GetContentRegionAvail().x - safeSide;
    if (rightPanelWidth < 0.0f) rightPanelWidth = 0.0f;
    
    ImGui::BeginChild("##RightPanel", ImVec2(rightPanelWidth, contentHeight), true);
    {
        auto& wcfg = game.mainWin->GetConfigs();
        auto& rcfg = engine.GetConfig().render;
        auto& ecfg = engine.GetConfig().window;
        
        const float rightPanelLeftPadding = padding + fontSize * PADDING_RATIO;
        const float itemWidth = fontSize * ITEM_WIDTH_RATIO;
        const float textWrapWidth = leftWidth + rightPanelLeftPadding + fontSize * TEXT_WRAP_RATIO;

        if (game.m_settingsTab == 0) {
            ImGui::Spacing();
            
            ImGui::Text("Render Distance"); ImGui::SameLine(leftWidth + rightPanelLeftPadding);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::SliderInt("##rd", &game.m_renderDistance, 2, 32, "%d chunks");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            
            ImGui::Checkbox("Enable LOD", &game.m_enableLod);
            
            ImGui::BeginDisabled(!game.m_enableLod);
            ImGui::Text("LOD Render Distance"); ImGui::SameLine(leftWidth + rightPanelLeftPadding);
            ImGui::SetNextItemWidth(itemWidth); 
            ImGui::InputInt("##lrd", &game.m_lodRenderDistance, 1, 10);
            
            int minLodDist = game.m_renderDistance;
            int maxLodDist = game.m_renderDistance * 16; 
            if (game.m_lodRenderDistance < minLodDist) game.m_lodRenderDistance = minLodDist;
            if (game.m_lodRenderDistance > maxLodDist) game.m_lodRenderDistance = maxLodDist;
            ImGui::SameLine();
            ImGui::Text("chunks");
            ImGui::EndDisabled();
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Fullscreen Mode"); ImGui::SameLine(leftWidth + rightPanelLeftPadding);
            bool fs = wcfg.fullscreen;
            if (ImGui::Checkbox("##fs", &fs)) {
                wcfg.fullscreen = fs;
                game.mainWin->SetFullscreen(fs);
                game.renderer->WaitIdle();
                game.RecreateFBOs(game.mainWin->GetConfigs().windowPixelWidth, game.mainWin->GetConfigs().windowPixelHeight);
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Vertical Sync"); ImGui::SameLine(leftWidth + rightPanelLeftPadding);
            if (ImGui::Checkbox("##vsync", &wcfg.vsync)) {
                game.m_applyPending = true;
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Ambient Occlusion"); ImGui::SameLine(leftWidth + rightPanelLeftPadding);
            bool ao = rcfg.m_aoStrength > 0.5f;
            if (ImGui::Checkbox("##ao", &ao)) {
                rcfg.m_aoStrength = ao ? 1.0f : 0.0f;
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Mipmap Levels"); ImGui::SameLine(leftWidth + rightPanelLeftPadding);
            ImGui::SetNextItemWidth(itemWidth);
            if (ImGui::SliderInt("##mipmap", &rcfg.mipmapLevels, 0, 4, "%d")) { }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = OFF\n1~4 = 生成 Mipmap 层级\n(需要重新加载世界生效)");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Anisotropic Filtering"); ImGui::SameLine(leftWidth + rightPanelLeftPadding);
            ImGui::Checkbox("##aniso", &rcfg.enableAnisotropic);
            
            if (rcfg.enableAnisotropic) {
                ImGui::Text("Aniso Level"); ImGui::SameLine(leftWidth + rightPanelLeftPadding);
                ImGui::SetNextItemWidth(itemWidth);
                
                const char* anisoItems[] = { "1x", "2x", "4x", "8x", "16x" };
                int anisoIndex = 4; 
                if (rcfg.maxAnisotropy <= 1.0f) anisoIndex = 0;
                else if (rcfg.maxAnisotropy <= 2.0f) anisoIndex = 1;
                else if (rcfg.maxAnisotropy <= 4.0f) anisoIndex = 2;
                else if (rcfg.maxAnisotropy <= 8.0f) anisoIndex = 3;
                
                if (ImGui::Combo("##anisolevel", &anisoIndex, anisoItems, IM_ARRAYSIZE(anisoItems))) {
                    static constexpr float anisoValues[] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
                    rcfg.maxAnisotropy = anisoValues[anisoIndex];
                }
            }
            
            ImGui::Spacing();
            ImGui::PushTextWrapPos(textWrapWidth);
            ImGui::TextColored(COLOR_WARNING, "Note: Texture settings require restarting the world to take effect.");
            ImGui::PopTextWrapPos();
            
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Field of View"); ImGui::SameLine(leftWidth + rightPanelLeftPadding);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::SliderFloat("##fov", &rcfg.fov, 30.0f, 120.0f, "%.0f");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            const char* apiDesc = "选择渲染使用的图形API。除非有特殊需求需要更改，否则请维持此设置为默认不变！";
            ImVec4 descColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); 
            if (ecfg.apitype == Eng::tool::RenderAPItype::VULKAN) {
                apiDesc = "实验性设置。在部分系统上使用可能会降低性能或导致不稳定。";
                descColor = COLOR_WARNING;
            }

            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(descColor, "%s", apiDesc);
            ImGui::PopTextWrapPos();
            ImGui::Spacing();

            ImGui::Text("图形 API"); ImGui::SameLine(leftWidth + rightPanelLeftPadding);
            ImGui::SetNextItemWidth(itemWidth);
            int currentApi = static_cast<int>(ecfg.apitype);
            const char* apiItems[] = { "默认", "优先使用 OpenGL", "优先使用 Vulkan" };
            if (ImGui::Combo("##api", &currentApi, apiItems, IM_ARRAYSIZE(apiItems))) {
                wcfg.apitype = static_cast<Eng::tool::RenderAPItype>(currentApi);
                engine.GetConfig().window.apitype = wcfg.apitype;
                game.m_apiChanged = true;
            }

            if (game.m_apiChanged) {
                ImGui::TextColored(COLOR_ERROR, "需要重启游戏以应用新的图形 API！");
            }
            ImGui::Dummy(ImVec2(0, fontSize * 2.0f)); 
        } else if (game.m_settingsTab == 1) {
            ImGui::Spacing();
            ImGui::TextDisabled("Audio settings coming soon...");
        } else if (game.m_settingsTab == 2) {
            ImGui::Spacing();
            ImGui::TextDisabled("Controls settings coming soon...");
        } else if (game.m_settingsTab == 3) {
            ImGui::Spacing();
            ImGui::TextColored(COLOR_ACCENT, "Mycraft Engine v" MYCRAFT_VERSION);
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::Text("GitHub Repository:"); ImGui::SameLine();
            ImGui::TextLinkOpenURL("yigeputong/mycraft", "https://github.com/yigeputong/mycraft");
            
            ImGui::Spacing();
            ImGui::Text("Special Thanks & Open Source Credits:");
            ImGui::Spacing();

            auto LinkItem = [](const char* name, const char* repo, const char* url) {
                ImGui::BulletText("%s", name); ImGui::SameLine(); 
                ImGui::TextLinkOpenURL(repo, url);
            };

            LinkItem("SDL3", "libsdl-org/SDL", "https://github.com/libsdl-org/SDL");
            LinkItem("Dear ImGui", "ocornut/imgui", "https://github.com/ocornut/imgui");
            LinkItem("glad (GL/GLES Loader)", "Dav1dde/glad", "https://github.com/Dav1dde/glad");
            LinkItem("glm (OpenGL Mathematics)", "g-truc/glm", "https://github.com/g-truc/glm");
            LinkItem("Vulkan SDK", "KhronosGroup/Vulkan-SDK", "https://github.com/KhronosGroup/Vulkan-SDK");
            LinkItem("Volk (Vulkan Meta-loader)", "zeux/volk", "https://github.com/zeux/volk");
            LinkItem("VulkanMemoryAllocator", "GPUOpen/VulkanMemoryAllocator", "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator");
            LinkItem("Assimp (3D Model Loader)", "assimp/assimp", "https://github.com/assimp/assimp");
            LinkItem("nlohmann/json", "nlohmann/json", "https://github.com/nlohmann/json");
            LinkItem("SDL_image", "libsdl-org/SDL_image", "https://github.com/libsdl-org/SDL_image");
            LinkItem("SDL_net", "libsdl-org/SDL_net", "https://github.com/libsdl-org/SDL_net");
            LinkItem("libiconv", "GNU libiconv", "https://savannah.gnu.org/projects/libiconv/");
            LinkItem("vcpkg (C++ Package Manager)", "microsoft/vcpkg", "https://github.com/microsoft/vcpkg");

            ImGui::Spacing();
            ImGui::TextColored(COLOR_TEXT_MUTED, "And all the open-source contributors who made this possible!");
            ImGui::Dummy(ImVec2(0, fontSize * 2.0f)); 
        }
    }
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(11);
}

void ClientUI::DrawHUD([[maybe_unused]] Eng::Engine& engine, MyGame& game) {
    ImGuiIO& io = ImGui::GetIO();
    float fontSize = ImGui::GetFontSize();
    
    float safeTop = 0.0f, safeBottom = 0.0f, safeSide = 0.0f;
    if (Eng::tool::IsMobilePlatform()) {
        safeTop = fontSize * SAFE_TOP_RATIO;
        safeBottom = fontSize * SAFE_BOTTOM_RATIO;
        safeSide = fontSize * SAFE_SIDE_RATIO;
    }

    // ---- 准心 ----
    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    
    float LEN = fontSize * 0.6f; 
    float THICK = fontSize * 0.15f; 
    float GAP = fontSize * 0.15f;
    
    ImU32 color = IM_COL32(255, 255, 255, 200);
    dl->AddLine(ImVec2(center.x - GAP - LEN, center.y), ImVec2(center.x - GAP, center.y), color, THICK);
    dl->AddLine(ImVec2(center.x + GAP, center.y), ImVec2(center.x + GAP + LEN, center.y), color, THICK);
    dl->AddLine(ImVec2(center.x, center.y - GAP - LEN), ImVec2(center.x, center.y - GAP), color, THICK);
    dl->AddLine(ImVec2(center.x, center.y + GAP), ImVec2(center.x, center.y + GAP + LEN), color, THICK);

    if (game.m_currentRayHit.hit) {
        BlockId targetId = game.GetBlockAt(game.m_currentRayHit.bx, game.m_currentRayHit.by, game.m_currentRayHit.bz);
        if (targetId != AIR_ID && targetId != UNKNOWN_ID) {
            const std::string& name = game.m_blockRegistry.Get(targetId).displayName;
            
            ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
            ImVec2 textPos = ImVec2(center.x - textSize.x * 0.5f, center.y + GAP + LEN + fontSize * 0.5f);
            
            dl->AddRectFilled(ImVec2(textPos.x - 4, textPos.y - 2), ImVec2(textPos.x + textSize.x + 4, textPos.y + textSize.y + 2), IM_COL32(0,0,0,150));
            dl->AddText(textPos, IM_COL32(255,255,255,255), name.c_str());
        }
    }

    // ---- 物品栏 ----
    float SLOT = fontSize * 3.5f; 
    float GAP_ITEM = fontSize * 0.3f;
    float totalW = MyGame::kHotbarSize * SLOT + (MyGame::kHotbarSize - 1) * GAP_ITEM;
    float startX = (io.DisplaySize.x - totalW) * 0.5f;
    float y = io.DisplaySize.y - SLOT - safeBottom - fontSize * 1.5f; 

    BlockId dirtId  = game.m_blockRegistry.Get("mycraft:dirt").id;
    BlockId grassId = game.m_blockRegistry.Get("mycraft:grass_block").id;
    BlockId sandId  = game.m_blockRegistry.Get("mycraft:sand").id;
    BlockId stoneId = game.m_blockRegistry.Get("mycraft:stone").id;

    for (int i = 0; i < MyGame::kHotbarSize; ++i) {
        float x = startX + i * (SLOT + GAP_ITEM);
        ImVec2 p0(x, y), p1(x + SLOT, y + SLOT);

        ImU32 bg = (i == game.m_hotbarIndex) ? IM_COL32(255, 255, 255, 200) : IM_COL32(0, 0, 0, 120);
        dl->AddRectFilled(p0, p1, bg);
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), 0, 0, 2.0f);

        const auto& slot = game.m_hotbar[i];
        if (slot.isEmpty()) continue;

        ImU32 itemColor = IM_COL32(150, 150, 150, 255);
        if (slot.type == dirtId)       itemColor = IM_COL32(110, 75, 50, 255);
        else if (slot.type == grassId) itemColor = IM_COL32(88, 160, 70, 255);
        else if (slot.type == sandId)  itemColor = IM_COL32(220, 210, 160, 255);
        else if (slot.type == stoneId) itemColor = IM_COL32(128, 128, 128, 255);
        
        float inset = SLOT * 0.15f;
        dl->AddRectFilled(ImVec2(p0.x + inset, p0.y + inset), ImVec2(p1.x - inset, p1.y - inset), itemColor);
        
        const char* name = game.m_blockRegistry.Get(slot.type).displayName.c_str();
        char firstLetter[2] = { name[0], '\0' };
        ImVec2 textSize = ImGui::CalcTextSize(firstLetter);
        dl->AddText(ImVec2(p0.x + SLOT * 0.5f - textSize.x * 0.5f, p0.y + SLOT * 0.5f - textSize.y * 0.5f), 
                    IM_COL32(255, 255, 255, 255), firstLetter);

        if (slot.count > 1) {
            char cnt[8];
            std::snprintf(cnt, sizeof(cnt), "%d", slot.count);
            dl->AddText(ImVec2(x + SLOT - textSize.x * 2.0f, y + fontSize * 0.2f), IM_COL32(255, 255, 255, 255), cnt);
        }
    }

    // ---- F3 调试面板 ----
    if (game.m_showDebug) {
        auto DrawLineBgLeft = [&](float x, float y, ImU32 color, const char* fmt, ...) {
            char buf[512];
            va_list args;
            va_start(args, fmt);
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
            va_list args;
            va_start(args, fmt);
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

        float dbgY = safeTop + fontSize * 0.5f;
        float lineHeight = ImGui::GetTextLineHeight() + 4.0f; 
        ImU32 white = IM_COL32(255, 255, 255, 255);
        ImU32 green = IM_COL32(85, 255, 85, 255);
        ImU32 yellow = IM_COL32(255, 255, 85, 255);

        float leftX = safeSide + fontSize * 0.8f;
        DrawLineBgLeft(leftX, dbgY, green, "Mycraft v" MYCRAFT_VERSION); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "%.0f fps | Frame: %.2f ms", game.m_fpsDisplay, game.m_frameMsDisplay); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Server tick: %.2f ms", game.m_serverTickMs); dbgY += lineHeight;
        dbgY += lineHeight * 0.5f;

        int bx = (int)std::floor(game.cameraPos.x);
        int by = (int)std::floor(game.cameraPos.y);
        int bz = (int)std::floor(game.cameraPos.z);
        int pcx = bx >> 4;
        int pcz = bz >> 4;
        DrawLineBgLeft(leftX, dbgY, white, "XYZ: %.2f / %.2f / %.2f", game.cameraPos.x, game.cameraPos.y, game.cameraPos.z); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Block: %d, %d, %d", bx, by, bz); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Chunk: %d, %d", pcx, pcz); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Yaw / Pitch: %.1f / %.1f", game.yaw, game.pitch); dbgY += lineHeight;
        dbgY += lineHeight * 0.5f;

        DrawLineBgLeft(leftX, dbgY, white, "Data Chunks: %zu", game.m_world.GetChunks().size()); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Mesh Chunks: %zu", game.m_world.GetMeshCount()); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Client Pending: %zu", game.m_world.GetPendingSize()); dbgY += lineHeight;
        if (game.m_server) {
            DrawLineBgLeft(leftX, dbgY, white, "Server Chunks: %zu", game.m_server->GetChunkCount()); dbgY += lineHeight;
        } else {
            DrawLineBgLeft(leftX, dbgY, white, "Server Chunks: N/A (Multiplayer)"); dbgY += lineHeight;
        }
        dbgY += lineHeight * 0.5f;

        DrawLineBgLeft(leftX, dbgY, white, "Native Distance: %d chunks", game.m_renderDistance); dbgY += lineHeight;
        
        if (game.m_enableLod) {
            DrawLineBgLeft(leftX, dbgY, yellow, "LOD Distance: %d chunks", game.m_lodRenderDistance); dbgY += lineHeight;
            DrawLineBgLeft(leftX, dbgY, yellow, "LOD Units: %zu", game.m_lodManager.GetLodUnitCount()); dbgY += lineHeight;
        } else {
            DrawLineBgLeft(leftX, dbgY, yellow, "LOD: Disabled"); dbgY += lineHeight;
        }
        dbgY += lineHeight * 0.5f;

        DrawLineBgLeft(leftX, dbgY, white, "Player ID: %d", game.m_myClientId); dbgY += lineHeight;
        
        int hh = static_cast<int>(game.m_timeOfDay);
        int mm = static_cast<int>((game.m_timeOfDay - hh) * 60);
        DrawLineBgLeft(leftX, dbgY, white, "Time: %02d:%02d", hh, mm);
        dbgY += lineHeight;

        float rightX = io.DisplaySize.x - safeSide - fontSize * 0.8f;
        dbgY = safeTop + fontSize * 0.5f;
        DrawLineBgRight(rightX, dbgY, white, "Backend: %s", game.m_deviceInfo.backend.c_str()); dbgY += lineHeight;
        DrawLineBgRight(rightX, dbgY, white, "GPU: %s", game.m_deviceInfo.deviceName.c_str()); dbgY += lineHeight;
        DrawLineBgRight(rightX, dbgY, white, "Vendor: %s", game.m_deviceInfo.vendor.c_str()); dbgY += lineHeight;
        DrawLineBgRight(rightX, dbgY, white, "API: %s", game.m_deviceInfo.apiVersion.c_str()); dbgY += lineHeight;
        DrawLineBgRight(rightX, dbgY, white, "Driver: %s", game.m_deviceInfo.driverVersion.c_str()); dbgY += lineHeight;
        DrawLineBgRight(rightX, dbgY, white, "Shader: %s", game.m_deviceInfo.shadingLanguage.c_str());
    }
}

} // namespace game::client