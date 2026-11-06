#include "game/client/ClientUI.h"
#include "game/Game.h"
#include "game/server/WorldSave.h"
#include "imgui.h"
#include "Engine/service/InputService.h"
#include "Engine/tool/Window.h"
#include "Engine/tool/Platform.h"
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <string>
#include <algorithm>

namespace game::client {

namespace {
    // ==========================================
    // UI 尺寸与颜色常量（基于 FontSize 动态缩放）
    // ==========================================
    constexpr float PADDING_RATIO           = 1.0f;
    constexpr float LEFT_PANEL_WIDTH_RATIO  = 12.0f;
    constexpr float ITEM_WIDTH_RATIO        = 10.0f;
    constexpr float BUTTON_HEIGHT_RATIO     = 1.75f;
    constexpr float MIN_BTN_WIDTH_RATIO     = 6.0f;

    // 移动端安全区比例
    constexpr float SAFE_TOP_RATIO          = 1.5f;
    constexpr float SAFE_BOTTOM_RATIO       = 2.5f;
    constexpr float SAFE_SIDE_RATIO         = 1.0f;

    // 主题颜色
    constexpr ImVec4 COLOR_ACCENT      = ImVec4(0.20f, 0.80f, 0.80f, 1.00f);
    constexpr ImVec4 COLOR_TEXT_ACTIVE = ImVec4(0.20f, 0.80f, 0.80f, 1.00f);
    constexpr ImVec4 COLOR_TEXT_MUTED  = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    constexpr ImVec4 COLOR_WARNING     = ImVec4(1.00f, 0.80f, 0.20f, 1.00f);
    constexpr ImVec4 COLOR_ERROR       = ImVec4(1.00f, 0.30f, 0.30f, 1.00f);

    // 格式化时间戳为可读字符串
    std::string FormatTimeAgo(int64_t timestamp) {
        if (timestamp <= 0) return "Never";
        
        // 统一使用毫秒来计算，彻底规避不同平台 count() 精度不一致的问题
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
            
        int64_t diff_ms = now_ms - timestamp;
        if (diff_ms < 0) diff_ms = 0; // 防止系统时间往回改导致负数
        
        int64_t diff_sec = diff_ms / 1000LL;
        
        if (diff_sec < 60) return std::to_string(diff_sec) + "s ago";
        if (diff_sec < 3600) return std::to_string(diff_sec / 60) + "m ago";
        if (diff_sec < 86400) return std::to_string(diff_sec / 3600) + "h ago";
        return std::to_string(diff_sec / 86400) + "d ago";
    }
}

// ==========================================
// UI 辅助工具实现
// ==========================================
void ClientUI::DrawTextWithShadow(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text) {
    // 获取当前的全局 Alpha（BeginDisabled 会把它调低）
    float alpha = ImGui::GetStyle().Alpha;
    
    // 处理阴影（应用 Alpha）
    ImVec4 shadowV = ImVec4(30.0f/255.0f, 30.0f/255.0f, 30.0f/255.0f, alpha);
    ImU32 shadowColor = ImGui::ColorConvertFloat4ToU32(shadowV);
    
    // 处理文字（应用 Alpha）
    ImVec4 textV = ImGui::ColorConvertU32ToFloat4(color);
    textV.w *= alpha;
    ImU32 textColor = ImGui::ColorConvertFloat4ToU32(textV);

    float offset = ImGui::GetFontSize() * 0.1f;
    dl->AddText(ImVec2(pos.x + offset, pos.y + offset), shadowColor, text);
    dl->AddText(pos, textColor, text);
}

bool ClientUI::DrawButton(const char* label, ImVec2 size) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    bool clicked = ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    // 获取当前的全局 Alpha
    float alpha = ImGui::GetStyle().Alpha;

    // 背景颜色计算（根据状态）
    ImVec4 bgV = ImVec4(0.39f, 0.39f, 0.39f, 1.0f); // 100/255
    if (active) bgV = ImVec4(0.55f, 0.55f, 0.55f, 1.0f); // 140/255
    else if (hovered) bgV = ImVec4(0.47f, 0.47f, 0.47f, 1.0f); // 120/255
    
    // 应用 Alpha
    bgV.w *= alpha;
    ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(bgV);
    
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor);

    float borderThickness = std::max(1.0f, ImGui::GetFontSize() * 0.1f);
    
    // 外边框（纯黑，应用 Alpha）
    ImVec4 outerV = ImVec4(0, 0, 0, alpha);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::ColorConvertFloat4ToU32(outerV), 0.0f, 0, borderThickness * 2.0f);
    
    // 内边框（应用 Alpha）
    ImVec4 innerV = (hovered || active) ? ImVec4(1.0f, 1.0f, 1.0f, alpha) : ImVec4(0.31f, 0.31f, 0.31f, alpha);
    ImU32 innerColor = ImGui::ColorConvertFloat4ToU32(innerV);
    
    dl->AddRect(ImVec2(pos.x + borderThickness, pos.y + borderThickness), 
                ImVec2(pos.x + size.x - borderThickness, pos.y + size.y - borderThickness), 
                innerColor, 0.0f, 0, borderThickness);

    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 textPos = ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f);
    
    // 内部调用 DrawTextWithShadow 已经自动处理了 Alpha
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

void ClientUI::DrawIsometricBlock(ImDrawList* dl, ImVec2 center, float size, BlockId id, ImTextureID  /*texId*/, const BlockRegistry& reg) {
    if (id == AIR_ID || id == UNKNOWN_ID) return;

    const auto& def = reg.Get(id);
    std::string name = def.displayName;

    // 兼容处理：由于 ImGui 不支持直接采样 Texture Array，暂时用大色块代替
    ImU32 topColor  = IM_COL32(150, 150, 150, 255);
    ImU32 sideColor = IM_COL32(100, 100, 100, 255);

    if (name.find("grass") != std::string::npos) {
        topColor  = IM_COL32(88, 160, 70, 255);   // 草绿
        sideColor = IM_COL32(110, 75, 50, 255);   // 泥土棕
    } else if (name.find("dirt") != std::string::npos) {
        topColor  = IM_COL32(110, 75, 50, 255);
        sideColor = IM_COL32(90, 60, 40, 255);
    } else if (name.find("sand") != std::string::npos) {
        topColor  = IM_COL32(220, 210, 160, 255);
        sideColor = IM_COL32(190, 180, 130, 255);
    } else if (name.find("stone") != std::string::npos) {
        topColor  = IM_COL32(128, 128, 128, 255);
        sideColor = IM_COL32(100, 100, 100, 255);
    } else if (name.find("torch") != std::string::npos) {
        topColor  = IM_COL32(255, 200, 50, 255);
        sideColor = IM_COL32(150, 100, 50, 255);
    } else if (name.find("water") != std::string::npos) {
        topColor  = IM_COL32(63, 118, 228, 255);
        sideColor = IM_COL32(40, 90, 180, 255);
    }

    float w = size * 0.5f;
    float h = size * 0.25f;
    float d = size * 0.5f;

    float cx = center.x;
    float cy = center.y; 
    float totalHeight = h * 2 + d;
    float topY = cy - (totalHeight * 0.5f);
    float midY = topY + h;
    float botY = midY + h;

    // 左侧面
    dl->AddQuadFilled(ImVec2(cx - w, midY), ImVec2(cx, midY - h), ImVec2(cx, midY - h + d), ImVec2(cx - w, midY + d), sideColor);
    // 右侧面
    dl->AddQuadFilled(ImVec2(cx, midY - h), ImVec2(cx + w, midY), ImVec2(cx + w, midY + d), ImVec2(cx, midY - h + d), IM_COL32(80, 80, 80, 255));
    // 顶面
    dl->AddQuadFilled(ImVec2(cx, topY), ImVec2(cx + w, midY), ImVec2(cx, botY), ImVec2(cx - w, midY), topColor);
}

// 主入口
void ClientUI::Draw(Eng::Engine& engine, MyGame& game) {
    game.renderer->ImGuiNewFrame();

    //   每帧必清注入状态（唯一可信的清零点）
    //   原先放在 ProcessTouchInputAndDrawOverlay 里，但那个函数在
    //   m_activeInputMode != Touch 时会提前 return，导致 injected 残留。
    if (game.m_inputService) {
        game.m_inputService->ResetInjectedStates();
    }

    // 用透明全屏窗口包裹编辑模式，让 InvisibleButton 正常工作
    if (game.m_isEditingLayout) {
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(40, 40, 40, 255));
        
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##EditLayoutRoot", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        
        ProcessTouchInputAndDrawOverlay(engine, game);
        
        ImGui::End();
        ImGui::PopStyleVar();
        return; 
    }

    if (game.m_menuOpen || game.m_state == MyGame::GameState::MainMenu || game.m_state == MyGame::GameState::WorldSelect) {
        ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
        bgDrawList->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(10, 10, 10, 160));
    }

    if (!game.m_menuOpen) {
        if (game.m_state == MyGame::GameState::MainMenu) {
            DrawMainMenu(engine, game);
        } else if (game.m_state == MyGame::GameState::WorldSelect) {
            DrawWorldSelectMenu(engine, game);
        } else if (game.m_state == MyGame::GameState::Playing) {
            DrawHUD(engine, game);
            ProcessTouchInputAndDrawOverlay(engine, game); // 在 HUD 之上处理触控
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
    float safeTop = 0.0f, safeBottom = 0.0f;
    if (Eng::tool::IsMobilePlatform()) {
        safeTop = fontSize * SAFE_TOP_RATIO;
        safeBottom = fontSize * SAFE_BOTTOM_RATIO;
    }

    float menuWidth = fontSize * 15.0f;      
    float buttonHeight = fontSize * 1.8f;    
    float buttonGap = fontSize * 0.6f;       
    
    float titleTextBaseWidth = ImGui::CalcTextSize("MYCRAFT").x;
    float desiredScale = 2.0f;               
    if (titleTextBaseWidth * desiredScale > io.DisplaySize.x * 0.8f) {
        desiredScale = (io.DisplaySize.x * 0.8f) / titleTextBaseWidth;
    }
    
    ImGui::PushFont(ImGui::GetFont(), fontSize * desiredScale);
    ImVec2 titleSize = ImGui::CalcTextSize("MYCRAFT");
    ImGui::PopFont();
    
    float titleToButtonsSpace = fontSize * 2.0f; 
    float totalButtonsHeight = buttonHeight * 3 + buttonGap * 2;
    float versionSpace = fontSize * 3.0f; 
    float totalContentHeight = titleSize.y + titleToButtonsSpace + totalButtonsHeight + versionSpace;
    
    float usableHeight = io.DisplaySize.y - safeTop - safeBottom;
    float blockStartY = safeTop + (usableHeight - totalContentHeight) * 0.5f;
    if (blockStartY < safeTop + fontSize * 2.0f) blockStartY = safeTop + fontSize * 2.0f;

    float startX = (io.DisplaySize.x - menuWidth) * 0.5f;
    ImVec2 titlePos = ImVec2((io.DisplaySize.x - titleSize.x) * 0.5f, blockStartY);
    
    ImGui::PushFont(ImGui::GetFont(), fontSize * desiredScale);
    DrawTextWithShadow(ImGui::GetWindowDrawList(), titlePos, IM_COL32(200, 200, 200, 255), "MYCRAFT");
    ImGui::PopFont();
    
    const char* splash = "Only in Mycraft!";
    float splashAngle = -0.3f;
    ImVec2 splashCenter(titlePos.x + titleSize.x - fontSize * 2.0f, titlePos.y + titleSize.y - fontSize * 0.5f);
    DrawRotatedText(ImGui::GetWindowDrawList(), splashCenter, splash, splashAngle, IM_COL32(255, 255, 0, 255));

    float buttonsStartY = blockStartY + titleSize.y + titleToButtonsSpace;

    ImGui::SetCursorScreenPos(ImVec2(startX, buttonsStartY));
    if (DrawButton("Single Player", ImVec2(menuWidth, buttonHeight))) {
        game.RefreshWorldList();
        game.m_state = MyGame::GameState::WorldSelect;
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

    float bottomTextY = io.DisplaySize.y - safeBottom - fontSize * 1.5f;
    const char* versionStr = "Mycraft v" MYCRAFT_VERSION;
    ImVec2 versionSize = ImGui::CalcTextSize(versionStr);
    ImGui::GetWindowDrawList()->AddText(ImVec2((io.DisplaySize.x - versionSize.x) * 0.5f, bottomTextY), IM_COL32(150, 150, 150, 255), versionStr);

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

void ClientUI::DrawWorldSelectMenu([[maybe_unused]] Eng::Engine& engine, MyGame& game) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* bg_dl = ImGui::GetBackgroundDrawList();
    bg_dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 180));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##WorldSelectRoot", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

    const float fontSize = ImGui::GetFontSize();
    ImGuiStyle& style = ImGui::GetStyle();
    float spacing = style.ItemSpacing.x; // 统一使用全局的 ItemSpacing 作为间隙
    
    float safeTop = 0.0f, safeBottom = 0.0f, safeSide = 0.0f;
    if (Eng::tool::IsMobilePlatform()) {
        safeTop = fontSize * SAFE_TOP_RATIO;
        safeBottom = fontSize * SAFE_BOTTOM_RATIO;
        safeSide = fontSize * SAFE_SIDE_RATIO;
    }

    ImVec2 rootPos(safeSide + fontSize, safeTop + fontSize);
    ImVec2 rootSize(io.DisplaySize.x - (safeSide + fontSize) * 2.0f, io.DisplaySize.y - safeTop - safeBottom - fontSize * 2.0f);
    
    float leftPanelWidth = fontSize * LEFT_PANEL_WIDTH_RATIO;
    float rightPanelWidth = rootSize.x - leftPanelWidth - spacing;

    // =========================================================
    // 左侧控制面板
    // =========================================================
    ImGui::SetCursorScreenPos(rootPos);
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(spacing, spacing));
    ImGui::BeginChild("##LeftPanel", ImVec2(leftPanelWidth, rootSize.y), true, ImGuiWindowFlags_NoScrollbar);
    {
        // 1. 顶部信息区
        if (!game.m_selectedWorldId.empty()) {
            auto it = std::find_if(game.m_savedWorlds.begin(), game.m_savedWorlds.end(),
                [&](const auto& w) { return w.worldId == game.m_selectedWorldId; });
            
            if (it != game.m_savedWorlds.end()) {
                ImGui::PushTextWrapPos(leftPanelWidth - spacing * 2.0f);
                ImGui::TextColored(COLOR_ACCENT, "%s", it->displayName.c_str());
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Seed: %u", it->seed);
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Last played: %s", FormatTimeAgo(it->lastPlayedTime).c_str());
                ImGui::PopTextWrapPos();
            }
        } else {
            ImGui::TextDisabled("No world selected.");
        }

        // 2. 底部按钮区
        float btnHeight = fontSize * 2.5f;
        float btnGap = fontSize * 0.5f; // ★ 把间隙调小
        float totalBtnHeight = (btnHeight * 4.0f) + (btnGap * 3.0f); 

        float contentBottomY = ImGui::GetWindowContentRegionMax().y;
        float startY = contentBottomY - totalBtnHeight;

        if (ImGui::GetCursorPosY() > startY) {
            startY = ImGui::GetCursorPosY();
        }

        ImGui::SetCursorPosY(startY);
        float btnWidth = ImGui::GetContentRegionAvail().x;
        bool hasSelection = !game.m_selectedWorldId.empty();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, btnGap));

        // 按钮 1: Play
        ImGui::BeginDisabled(!hasSelection);
        if (DrawButton("Play", ImVec2(btnWidth, btnHeight))) {
            if (game.StartLocalServer(true, true, game.m_selectedWorldId)) {
                game.EnterGame();
            } else {
                game.m_statusText = "Failed to start. World may be locked.";
            }
        }
        ImGui::EndDisabled();
        
        // 按钮 2: Create a New World
        if (DrawButton("Create a New World", ImVec2(btnWidth, btnHeight))) {
            game.m_showNewWorldDialog = true;
            game.m_newWorldNameBuffer[0] = '\0';
            game.m_newWorldSeedBuffer[0] = '\0';
        }

        // 按钮 3: Delete World
        ImGui::BeginDisabled(!hasSelection);
        if (DrawButton("Delete World", ImVec2(btnWidth, btnHeight))) {
            game.m_worldToDelete = game.m_selectedWorldId;
            game.m_showDeleteConfirmDialog = true;
        }
        ImGui::EndDisabled();

        // 按钮 4: Back to Menu
        if (DrawButton("Back to Menu", ImVec2(btnWidth, btnHeight))) {
            game.m_state = MyGame::GameState::MainMenu;
            game.m_selectedWorldId.clear();
            game.m_statusText.clear();
        }

        ImGui::PopStyleVar(); // 恢复 ItemSpacing
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(); // 恢复 WindowPadding

    // =========================================================
    // 右侧世界列表区
    // =========================================================
    ImGui::SetCursorScreenPos(ImVec2(rootPos.x + leftPanelWidth + spacing, rootPos.y));
    ImGui::BeginChild("##RightPanel", ImVec2(rightPanelWidth, rootSize.y), true);
    {
        ImGui::SetCursorPos(ImVec2(spacing, spacing));
        ImGui::SetNextItemWidth(rightPanelWidth - spacing * 2.0f);
        ImGui::InputTextWithHint("##Search", "Search worlds...", game.m_searchBuffer, sizeof(game.m_searchBuffer));
        
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginChild("##ListScroll", ImVec2(0, 0), false);
        {
            ImDrawList* dl = ImGui::GetWindowDrawList(); 

            std::string searchStr = game.m_searchBuffer;
            std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

            for (size_t i = 0; i < game.m_savedWorlds.size(); ++i) {
                const auto& meta = game.m_savedWorlds[i];
                
                std::string worldNameStr = meta.displayName;
                std::transform(worldNameStr.begin(), worldNameStr.end(), worldNameStr.begin(), ::tolower);
                if (!searchStr.empty() && worldNameStr.find(searchStr) == std::string::npos) {
                    continue;
                }

                float itemHeight = fontSize * 4.5f;
                float itemWidth = ImGui::GetContentRegionAvail().x;
                ImGui::PushID(static_cast<int>(i));
                
                ImVec2 cardPos = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##card", ImVec2(itemWidth, itemHeight))) {
                    game.m_selectedWorldId = meta.worldId; 
                }
                
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    game.m_selectedWorldId = meta.worldId;
                    if (game.StartLocalServer(true, true, game.m_selectedWorldId)) {
                        game.EnterGame();
                    } else {
                        game.m_statusText = "Failed to start. World may be locked.";
                    }
                }

                bool hovered = ImGui::IsItemHovered();
                bool selected = (game.m_selectedWorldId == meta.worldId);

                ImU32 cardBg = selected ? IM_COL32(80, 80, 80, 255) : (hovered ? IM_COL32(60, 60, 60, 255) : IM_COL32(30, 30, 30, 255));
                dl->AddRectFilled(cardPos, ImVec2(cardPos.x + itemWidth, cardPos.y + itemHeight), cardBg);
                
                if (selected) {
                    dl->AddRect(cardPos, ImVec2(cardPos.x + itemWidth, cardPos.y + itemHeight), IM_COL32(255, 255, 255, 255), 0.0f, 0, 3.0f);
                }

                float textX = cardPos.x + spacing;
                dl->AddText(ImVec2(textX, cardPos.y + fontSize * 0.8f), IM_COL32(255, 255, 255, 255), meta.displayName.c_str());
                
                std::string details = "Seed: " + std::to_string(meta.seed) + " | Last played: " + FormatTimeAgo(meta.lastPlayedTime);
                dl->AddText(ImVec2(textX, cardPos.y + fontSize * 2.5f), IM_COL32(200, 200, 200, 255), details.c_str());

                ImGui::Dummy(ImVec2(0, fontSize * 0.5f)); 
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    if (!game.m_statusText.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(rootPos.x, rootPos.y - fontSize * 2.0f));
        ImGui::TextColored(COLOR_ERROR, "%s", game.m_statusText.c_str());
    }

    ImGui::End();

    // =========================================================
    // 弹窗管理
    // =========================================================
    if (game.m_showNewWorldDialog) ImGui::OpenPopup("New World");
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New World", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // 加一点内边距，让界面不那么挤
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 12));

        ImGui::Text("World Name:");
        ImGui::SetNextItemWidth(fontSize * 20.0f);
        ImGui::InputText("##WorldName", game.m_newWorldNameBuffer, sizeof(game.m_newWorldNameBuffer));
        
        ImGui::Spacing();

        ImGui::Text("Seed (leave blank for random):");
        ImGui::SetNextItemWidth(fontSize * 20.0f);
        ImGui::InputText("##WorldSeed", game.m_newWorldSeedBuffer, sizeof(game.m_newWorldSeedBuffer));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("Create", ImVec2(fontSize * 8.0f, fontSize * 2.5f))) {
            std::string newId = game::server::WorldSave::CreateNewWorld(game.m_newWorldNameBuffer, game.m_newWorldSeedBuffer);
            if (!newId.empty()) {
                game.RefreshWorldList();
                game.m_selectedWorldId = newId;
                game.m_statusText.clear();
                game.m_showNewWorldDialog = false;
                ImGui::CloseCurrentPopup();
            } else {
                game.m_statusText = "Failed to create world.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fontSize * 8.0f, fontSize * 2.5f))) {
            game.m_showNewWorldDialog = false;
            game.m_statusText.clear();
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::PopStyleVar(2);
        ImGui::EndPopup();
    }

    if (game.m_showDeleteConfirmDialog) {
        ImGui::OpenPopup("Delete World");
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete World", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(COLOR_ERROR, "Are you sure you want to delete this world?");
        ImGui::Text("This action cannot be undone!");
        ImGui::Spacing();
        
        if (ImGui::Button("Delete", ImVec2(fontSize * 8.0f, fontSize * 2.5f))) {
            if (game::server::WorldSave::DeleteWorld(game.m_worldToDelete)) {
                game.RefreshWorldList();
                if (game.m_selectedWorldId == game.m_worldToDelete) {
                    game.m_selectedWorldId.clear();
                }
                game.m_statusText.clear();
            } else {
                game.m_statusText = "Failed to delete world. Files may be locked.";
            }
            game.m_showDeleteConfirmDialog = false;
            game.m_worldToDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fontSize * 8.0f, fontSize * 2.5f))) {
            game.m_showDeleteConfirmDialog = false;
            game.m_worldToDelete.clear();
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

    // 统一四周边距
    const float marginX = padding + safeSide;
    const float marginY = padding + safeTop;

    // ==========================================
    // 1. 顶部工具栏 (占满整行，两端严格对齐 marginX)
    // ==========================================
    ImGui::SetCursorPos(ImVec2(marginX, marginY));
    
    const char* leftBtnText = (game.m_state == MyGame::GameState::Playing) ? "Save & Quit to Title" : "Back to Menu";
    float leftBtnWidth = ImGui::CalcTextSize(leftBtnText).x + padding * 2.0f;
    if (leftBtnWidth < fontSize * ITEM_WIDTH_RATIO) leftBtnWidth = fontSize * ITEM_WIDTH_RATIO; 
    if (ImGui::Button(leftBtnText, ImVec2(leftBtnWidth, buttonHeight))) {
        if (game.m_state == MyGame::GameState::Playing) game.m_returnToMenuPending = true;
        else game.m_menuOpen = false;
    }

    ImGui::SameLine();
    float applyWidth = ImGui::CalcTextSize("Apply").x + padding * 2.0f;
    if (applyWidth < fontSize * MIN_BTN_WIDTH_RATIO) applyWidth = fontSize * MIN_BTN_WIDTH_RATIO;
    if (ImGui::Button("Apply", ImVec2(applyWidth, buttonHeight))) {
        game.m_applyPending = true;
        game.SaveSettings(engine);
    }

    ImGui::SameLine();
    float doneWidth = ImGui::CalcTextSize("Done").x + padding * 2.0f;
    if (doneWidth < fontSize * MIN_BTN_WIDTH_RATIO) doneWidth = fontSize * MIN_BTN_WIDTH_RATIO;
    if (ImGui::Button("Done", ImVec2(doneWidth, buttonHeight))) {
        game.m_applyPending = true;
        game.m_menuOpen = false;
        game.SaveSettings(engine);
    }

    // 搜索框和 X 按钮
    ImGui::SameLine();
    float currentX = ImGui::GetCursorPosX();
    float xBtnWidth = buttonHeight;
    float rightEdgeX = io.DisplaySize.x - marginX - xBtnWidth; // X按钮左边界
    float searchWidth = rightEdgeX - ImGui::GetStyle().ItemSpacing.x - currentX;
    
    if (searchWidth > fontSize * 5.0f) {
        float calculatedFramePaddingY = (buttonHeight - fontSize) * 0.5f;
        if (calculatedFramePaddingY < 0.0f) calculatedFramePaddingY = 0.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, calculatedFramePaddingY));
        ImGui::SetNextItemWidth(searchWidth);
        static char searchBuf[128] = "";
        ImGui::InputTextWithHint("##Search", "Search options...", searchBuf, sizeof(searchBuf));
        ImGui::PopStyleVar();
        ImGui::SameLine();
    } else {
        ImGui::SetCursorPosX(rightEdgeX);
    }

    if (ImGui::Button("X", ImVec2(xBtnWidth, buttonHeight))) game.m_menuOpen = false;

    ImGui::Separator();
    ImGui::Spacing();

    // ==========================================
    // 2. 主体内容区 (搬用 WorldSelectMenu 的等宽绝对定位方案)
    // ==========================================
    const float contentStartY = ImGui::GetCursorPosY(); 
    // 留出底部安全区及边距 (使用 marginY 或 marginX 保持一致)
    float contentHeight = io.DisplaySize.y - contentStartY - (padding + safeBottom); 
    if (contentHeight < fontSize * LEFT_PANEL_WIDTH_RATIO) contentHeight = fontSize * LEFT_PANEL_WIDTH_RATIO;

    float spacing = ImGui::GetStyle().ItemSpacing.x;
    // 右侧面板严格等宽计算：屏幕宽度 - (左边距 * 2) - 左侧面板宽度 - 中间间距
    float rightPanelWidth = io.DisplaySize.x - (marginX * 2.0f) - leftWidth - spacing;
    if (rightPanelWidth < 0.0f) rightPanelWidth = 0.0f;

    // 给子窗口加上统一的内边距，避免内部文字/滚动条贴边框
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));

    // 左侧面板
    ImGui::SetCursorPos(ImVec2(marginX, contentStartY));
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
    
    // 右侧面板
    ImGui::SetCursorPos(ImVec2(marginX + leftWidth + spacing, contentStartY));
    ImGui::BeginChild("##RightPanel", ImVec2(rightPanelWidth, contentHeight), true);
    {
        auto& wcfg = game.mainWin->GetConfigs();
        auto& rcfg = engine.GetConfig().render;
        auto& ecfg = engine.GetConfig().window;
        
        // 获取内部可用宽度（剔除滚动条和 Padding）
        float innerAvailWidth = ImGui::GetContentRegionAvail().x;
        
        // 比例分配：标签占 40%，控件占剩余空间（扣除间距），实现右侧严格对齐
        const float textOffset = innerAvailWidth * 0.40f; 
        const float controlWidth = innerAvailWidth - textOffset - ImGui::GetStyle().ItemSpacing.x;
        const float textWrapWidth = innerAvailWidth;

        if (game.m_settingsTab == 0) {
            ImGui::Spacing();
            ImGui::Text("Render Distance"); ImGui::SameLine(textOffset);
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderInt("##rd", &game.m_renderDistance, 2, 32, "%d chunks");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            
            ImGui::Checkbox("Enable LOD", &game.m_enableLod);
            ImGui::BeginDisabled(!game.m_enableLod);
            ImGui::Text("LOD Render Distance"); ImGui::SameLine(textOffset);
            ImGui::SetNextItemWidth(controlWidth); 
            ImGui::InputInt("##lrd", &game.m_lodRenderDistance, 1, 10);
            int minLodDist = game.m_renderDistance;
            int maxLodDist = game.m_renderDistance * 16; 
            if (game.m_lodRenderDistance < minLodDist) game.m_lodRenderDistance = minLodDist;
            if (game.m_lodRenderDistance > maxLodDist) game.m_lodRenderDistance = maxLodDist;
            ImGui::EndDisabled();
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Fullscreen Mode"); ImGui::SameLine(textOffset);
            bool fs = wcfg.fullscreen;
            if (ImGui::Checkbox("##fs", &fs)) {
                wcfg.fullscreen = fs;
                game.mainWin->SetFullscreen(fs);
                game.renderer->WaitIdle();
                game.RecreateFBOs(game.mainWin->GetConfigs().windowPixelWidth, game.mainWin->GetConfigs().windowPixelHeight);
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Vertical Sync"); ImGui::SameLine(textOffset);
            if (ImGui::Checkbox("##vsync", &wcfg.vsync)) game.m_applyPending = true;
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Ambient Occlusion"); ImGui::SameLine(textOffset);
            bool ao = rcfg.m_aoStrength > 0.5f;
            if (ImGui::Checkbox("##ao", &ao)) rcfg.m_aoStrength = ao ? 1.0f : 0.0f;
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Mipmap Levels"); ImGui::SameLine(textOffset);
            ImGui::SetNextItemWidth(controlWidth);
            if (ImGui::SliderInt("##mipmap", &rcfg.mipmapLevels, 0, 4, "%d")) { }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = OFF\n1~4 = 生成 Mipmap 层级\n(需要重新加载世界生效)");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            ImGui::Text("Anisotropic Filtering"); ImGui::SameLine(textOffset);
            ImGui::Checkbox("##aniso", &rcfg.enableAnisotropic);
            if (rcfg.enableAnisotropic) {
                ImGui::Text("Aniso Level"); ImGui::SameLine(textOffset);
                ImGui::SetNextItemWidth(controlWidth);
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

            ImGui::Text("Field of View"); ImGui::SameLine(textOffset);
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat("##fov", &rcfg.fov, 30.0f, 120.0f, "%.0f");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            const char* apiDesc = "选择渲染使用的图形API。除非有特殊需求需要更改，否则请维持此设置为默认不变！";
            ImVec4 descColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); 
            if (ecfg.apitype == Eng::tool::RenderAPItype::VULKAN) {
                apiDesc = "实验性设置。在部分系统上使用可能会降低性能或导致不稳定。";
                descColor = COLOR_WARNING;
            }
            ImGui::PushTextWrapPos(textWrapWidth);
            ImGui::TextColored(descColor, "%s", apiDesc);
            ImGui::PopTextWrapPos();
            ImGui::Spacing();

            ImGui::Text("图形 API"); ImGui::SameLine(textOffset);
            ImGui::SetNextItemWidth(controlWidth);
            int currentApi = static_cast<int>(ecfg.apitype);
            const char* apiItems[] = { "默认", "优先使用 OpenGL", "优先使用 Vulkan" };
            if (ImGui::Combo("##api", &currentApi, apiItems, IM_ARRAYSIZE(apiItems))) {
                wcfg.apitype = static_cast<Eng::tool::RenderAPItype>(currentApi);
                engine.GetConfig().window.apitype = wcfg.apitype;
                game.m_apiChanged = true;
            }
            if (game.m_apiChanged) ImGui::TextColored(COLOR_ERROR, "需要重启游戏以应用新的图形 API！");
            ImGui::Dummy(ImVec2(0, fontSize * 2.0f)); 
        } else if (game.m_settingsTab == 1) {
            ImGui::Spacing(); ImGui::TextDisabled("Audio settings coming soon...");
        } else if (game.m_settingsTab == 2) {
            ImGui::Spacing();
            
            std::string autoLabel = "Auto";
            if (game.m_activeInputMode == MyGame::InputMode::Touch) {
                autoLabel += " (Touch)";
            } else {
                autoLabel += " (Keyboard & Mouse)";
            }
            
            const char* modes[] = { autoLabel.c_str(), "Keyboard & Mouse", "Touch" };
            int currentMode = static_cast<int>(game.m_inputMode);
            ImGui::Text("Input Preference"); ImGui::SameLine(textOffset);
            ImGui::SetNextItemWidth(controlWidth); 
            if (ImGui::Combo("##inputmode", &currentMode, modes, IM_ARRAYSIZE(modes))) {
                game.m_inputMode = static_cast<MyGame::InputMode>(currentMode);
                if (game.m_inputMode != MyGame::InputMode::Auto) {
                    game.m_activeInputMode = game.m_inputMode;
                }
            }
            
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            
            bool showTouchSettings = (game.m_inputMode == MyGame::InputMode::Touch) || 
                                     (game.m_inputMode == MyGame::InputMode::Auto && game.m_activeInputMode == MyGame::InputMode::Touch);
            if (showTouchSettings) {
                ImGui::Text("Touch Sensitivity"); ImGui::SameLine(textOffset);
                ImGui::SetNextItemWidth(controlWidth);
                ImGui::SliderFloat("##touchsens", &game.m_touchSensitivity, 0.05f, 0.50f, "%.2f");

                ImGui::Spacing();
                ImGui::Text("Touch Deadzone"); ImGui::SameLine(textOffset);
                ImGui::SetNextItemWidth(controlWidth);
                ImGui::SliderFloat("##touchdeadzone", &game.m_touchDeadzone, 0.0f, 20.0f, "%.0f px");
                
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                
                if (ImGui::Button("Edit Touch Layout", ImVec2(controlWidth, buttonHeight))) {
                    game.m_isEditingLayout = true;
                    game.m_menuOpen = false; 
                }
            } else {
                ImGui::TextDisabled("Touch settings are hidden in Keyboard & Mouse mode.");
            }
            ImGui::Dummy(ImVec2(0, fontSize * 2.0f));
        } else if (game.m_settingsTab == 3) {
            ImGui::Spacing();
            ImGui::TextColored(COLOR_ACCENT, "Mycraft Engine v" MYCRAFT_VERSION);
            ImGui::Separator(); ImGui::Spacing();
            ImGui::Text("GitHub Repository:"); ImGui::SameLine(); 
            ImGui::TextLinkOpenURL("yigeputong/mycraft", "https://github.com/yigeputong/mycraft");
            ImGui::Spacing(); ImGui::Text("Special Thanks & Open Source Credits:"); ImGui::Spacing();

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
    ImGui::PopStyleVar(); // 恢复 WindowPadding

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

    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float LEN = fontSize * 0.6f, THICK = fontSize * 0.15f, GAP = fontSize * 0.15f;
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

    float SLOT = fontSize * 3.5f; 
    float GAP_ITEM = fontSize * 0.3f;
    float totalW = MyGame::kHotbarSize * SLOT + (MyGame::kHotbarSize - 1) * GAP_ITEM;
    float startX = (io.DisplaySize.x - totalW) * 0.5f;
    float y = io.DisplaySize.y - SLOT - safeBottom - fontSize * 1.5f; 

    BlockId dirtId  = game.m_blockRegistry.Get("mycraft:dirt").id;
    BlockId grassId = game.m_blockRegistry.Get("mycraft:grass_block").id;
    BlockId sandId  = game.m_blockRegistry.Get("mycraft:sand").id;
    BlockId stoneId = game.m_blockRegistry.Get("mycraft:stone").id;

    // DrawHUD 没有 ImGui::Begin 包裹，
    // 不能调用 InvisibleButton 等需要窗口上下文的 API。
    // 这里改为直接通过鼠标坐标和触摸点手动判定点击，彻底消除 FallbackWindow。
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool isMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse;
    
    // 查找是否有新的触摸点落在了物品栏区域（针对移动端）
    ImVec2 touchClickPos(-1.0f, -1.0f);
    if (game.m_activeInputMode == MyGame::InputMode::Touch) {
        for (const auto& t : game.m_inputService->GetAllTouches()) {
            if (t.active && !t.prevActive) {
                touchClickPos = ImVec2(t.pos.x * io.DisplaySize.x, t.pos.y * io.DisplaySize.y);
                break;
            }
        }
    }

    for (int i = 0; i < MyGame::kHotbarSize; ++i) {
        float x = startX + i * (SLOT + GAP_ITEM);
        ImVec2 p0(x, y), p1(x + SLOT, y + SLOT);

        // 手动点击命中判定
        bool clicked = false;
        if (isMouseClicked) {
            if (mousePos.x >= p0.x && mousePos.x <= p1.x && mousePos.y >= p0.y && mousePos.y <= p1.y) {
                clicked = true;
            }
        }
        if (touchClickPos.x >= 0.0f) {
            if (touchClickPos.x >= p0.x && touchClickPos.x <= p1.x && touchClickPos.y >= p0.y && touchClickPos.y <= p1.y) {
                clicked = true;
            }
        }
        
        if (clicked) {
            game.m_hotbarIndex = i;
        }

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
        dl->AddText(ImVec2(p0.x + SLOT * 0.5f - textSize.x * 0.5f, p0.y + SLOT * 0.5f - textSize.y * 0.5f), IM_COL32(255, 255, 255, 255), firstLetter);

        if (slot.count > 1) {
            char cnt[8]; std::snprintf(cnt, sizeof(cnt), "%d", slot.count);
            dl->AddText(ImVec2(x + SLOT - textSize.x * 2.0f, y + fontSize * 0.2f), IM_COL32(255, 255, 255, 255), cnt);
        }
    }

    if (game.m_showDebug) {
        auto DrawLineBgLeft = [&](float x, float y, ImU32 color, const char* fmt, ...) {
            char buf[512]; va_list args; va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
            ImVec2 textSize = ImGui::CalcTextSize(buf);
            dl->AddRectFilled(ImVec2(x - 4, y - 1), ImVec2(x + textSize.x + 4, y + textSize.y + 1), IM_COL32(0, 0, 0, 130));
            dl->AddText(ImVec2(x, y), color, buf);
        };
        auto DrawLineBgRight = [&](float rightX, float y, ImU32 color, const char* fmt, ...) {
            char buf[512]; va_list args; va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
            ImVec2 textSize = ImGui::CalcTextSize(buf);
            float x = rightX - textSize.x;
            dl->AddRectFilled(ImVec2(x - 4, y - 1), ImVec2(rightX + 4, y + textSize.y + 1), IM_COL32(0, 0, 0, 130));
            dl->AddText(ImVec2(x, y), color, buf);
        };

        float dbgY = safeTop + fontSize * 0.5f;
        float lineHeight = ImGui::GetTextLineHeight() + 4.0f; 
        ImU32 white = IM_COL32(255, 255, 255, 255), green = IM_COL32(85, 255, 85, 255), yellow = IM_COL32(255, 255, 85, 255);

        float leftX = safeSide + fontSize * 0.8f;
        DrawLineBgLeft(leftX, dbgY, green, "Mycraft v" MYCRAFT_VERSION); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "%.0f fps | Frame: %.2f ms", game.m_fpsDisplay, game.m_frameMsDisplay); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Server tick: %.2f ms", game.m_serverTickMs); dbgY += lineHeight;
        dbgY += lineHeight * 0.5f;

        int bx = (int)std::floor(game.cameraPos.x);
        int by = (int)std::floor(game.cameraPos.y);
        int bz = (int)std::floor(game.cameraPos.z);
        DrawLineBgLeft(leftX, dbgY, white, "XYZ: %.2f / %.2f / %.2f", game.cameraPos.x, game.cameraPos.y, game.cameraPos.z); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Block: %d, %d, %d", bx, by, bz); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Chunk: %d, %d", bx >> 4, bz >> 4); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Yaw / Pitch: %.1f / %.1f", game.yaw, game.pitch); dbgY += lineHeight;
        dbgY += lineHeight * 0.5f;

        DrawLineBgLeft(leftX, dbgY, white, "Data Chunks: %zu", game.m_world.GetChunks().size()); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Mesh Chunks: %zu", game.m_world.GetMeshCount()); dbgY += lineHeight;
        DrawLineBgLeft(leftX, dbgY, white, "Client Pending: %zu", game.m_world.GetPendingSize()); dbgY += lineHeight;
        if (game.m_server) DrawLineBgLeft(leftX, dbgY, white, "Server Chunks: %zu", game.m_server->GetChunkCount()); dbgY += lineHeight;
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
        DrawLineBgLeft(leftX, dbgY, white, "Time: %02d:%02d", hh, mm); dbgY += lineHeight;

        DrawLineBgLeft(leftX, dbgY, green, "Seed: %u", game.m_worldSeed); dbgY += lineHeight;

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

void ClientUI::ProcessTouchInputAndDrawOverlay(Eng::Engine& engine, MyGame& game) {
    if (game.m_activeInputMode != MyGame::InputMode::Touch && !game.m_isEditingLayout) return;
    
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 sz = io.DisplaySize;
    ImDrawList* fgDraw = ImGui::GetForegroundDrawList();
    
    // 记录哪些触摸点已经被虚拟按键消耗
    std::unordered_set<int64_t> consumedTouches;

    // 1. 绘制并处理虚拟按键
    for (size_t i = 0; i < game.m_virtualButtons.size(); ++i) {
        auto& btn = game.m_virtualButtons[i];
        ImVec2 center(btn.pos.x * sz.x, btn.pos.y * sz.y);
        float rad = btn.radius * sz.x;
        bool isHovered = false;
        bool isActive = false;

        // 视觉层的动态标签
        std::string labelToShow = btn.label;
        if (btn.actionName == "Jump" && game.m_flyMode) labelToShow = "升";

        if (game.m_isEditingLayout) {
            ImGui::SetCursorScreenPos(ImVec2(center.x - rad, center.y - rad));
            ImGui::InvisibleButton(("##vbtn" + std::to_string(i)).c_str(), ImVec2(rad * 2.0f, rad * 2.0f));
            isHovered = ImGui::IsItemHovered();
            isActive = ImGui::IsItemActive();

            if (isActive) {
                ImVec2 delta = io.MouseDelta;
                btn.pos.x += delta.x / sz.x;
                btn.pos.y += delta.y / sz.y;
                btn.pos.x = glm::clamp(btn.pos.x, 0.05f, 0.95f);
                btn.pos.y = glm::clamp(btn.pos.y, 0.05f, 0.95f);
            }
        } else {
            // 正常模式：仅遍历 Touch 点
            for (const auto& t : game.m_inputService->GetAllTouches()) {
                if (!t.active) continue;
                ImVec2 touchPos(t.pos.x * sz.x, t.pos.y * sz.y);
                float dx = touchPos.x - center.x;
                float dy = touchPos.y - center.y;
                if (dx * dx + dy * dy < rad * rad) {
                    consumedTouches.insert(t.fingerId);
                    isActive = true;
                    
                    // 针对 OpenMenu 特殊处理。
                    // 原因是注入发生在 Render 阶段，IsActionPressed 会被下一帧的 Update 吞掉边沿。
                    if (btn.actionName == "OpenMenu") {
                        game.m_menuOpen = true;
                    } else {
                        game.m_inputService->InjectActionState(Eng::service::HashActionName(btn.actionName), true);
                    }
                    break;
                }
            }
        }

        // 绘制按键视觉
        ImU32 color = game.m_isEditingLayout ? IM_COL32(255, 100, 100, 120) : IM_COL32(0, 0, 0, 60);
        if (isActive) color = IM_COL32(255, 255, 255, 80);
        else if (isHovered) color = IM_COL32(255, 255, 255, 50);

        fgDraw->AddCircleFilled(center, rad, color);
        fgDraw->AddCircle(center, rad, IM_COL32(255, 255, 255, 150), 32, 2.0f);
        
        ImVec2 textSize = ImGui::CalcTextSize(labelToShow.c_str());
        fgDraw->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                        IM_COL32_WHITE, labelToShow.c_str());
    }

    // 2. 处理摇杆和视角
    float baseRadius = sz.x * 0.12f;
    float stickRadius = sz.x * 0.05f;

    if (!game.m_isEditingLayout) {
        // ================= 正常游玩模式 =================
        game.m_leftTouchId = -1; 
        
        for (const auto& t : game.m_inputService->GetAllTouches()) {
            if (!t.active || consumedTouches.contains(t.fingerId)) continue;
            
            ImVec2 touchPos(t.pos.x * sz.x, t.pos.y * sz.y);
            float pxDeltaX = t.delta.x * sz.x;
            float pxDeltaY = t.delta.y * sz.y;

            if (t.pos.x < 0.5f) {
                // 左屏虚拟摇杆
                if (game.m_leftTouchId == -1) {
                    game.m_leftTouchId = static_cast<int>(t.fingerId);
                    if (!t.prevActive) {
                        game.m_joystickOriginPx = {touchPos.x, touchPos.y};
                    }
                }
                if (game.m_leftTouchId == t.fingerId) {
                    game.m_joystickCurrentPx = {touchPos.x, touchPos.y};
                    float diffX = touchPos.x - game.m_joystickOriginPx.x;
                    float diffY = touchPos.y - game.m_joystickOriginPx.y;
                    float maxRadius = baseRadius;
                    
                    float lenSqr = diffX * diffX + diffY * diffY;
                    if (lenSqr > maxRadius * maxRadius) {
                        float len = std::sqrt(lenSqr);
                        diffX = (diffX / len) * maxRadius;
                        diffY = (diffY / len) * maxRadius;
                    }
                    game.m_inputService->InjectMoveDir(glm::vec2(diffX / maxRadius, -diffY / maxRadius));
                }
            } else {
                // 右屏视角滑动
                float lenSqr = pxDeltaX * pxDeltaX + pxDeltaY * pxDeltaY;
                if (lenSqr > game.m_touchDeadzone * game.m_touchDeadzone) {
                    game.yaw += pxDeltaX * game.m_touchSensitivity;
                    game.pitch -= pxDeltaY * game.m_touchSensitivity;
                    game.pitch = glm::clamp(game.pitch, -89.0f, 89.0f);
                }
            }
        }
        
        // 绘制摇杆
        if (game.m_leftTouchId == -1) {
            // 没触摸时：画在固定位置
            ImVec2 defaultCenter(game.m_joystickBasePos.x * sz.x, game.m_joystickBasePos.y * sz.y); 
            fgDraw->AddCircleFilled(defaultCenter, baseRadius, IM_COL32(255, 255, 255, 20));
            fgDraw->AddCircle(defaultCenter, baseRadius, IM_COL32(255, 255, 255, 60), 32, 2.0f);
            fgDraw->AddCircleFilled(defaultCenter, stickRadius, IM_COL32(255, 255, 255, 60));
        } else {
            // 有触摸时：画在手指按下的动态位置
            fgDraw->AddCircleFilled(ImVec2(game.m_joystickOriginPx.x, game.m_joystickOriginPx.y), baseRadius, IM_COL32(255, 255, 255, 40));
            fgDraw->AddCircle(ImVec2(game.m_joystickOriginPx.x, game.m_joystickOriginPx.y), baseRadius, IM_COL32(255, 255, 255, 100), 32, 2.0f);
            fgDraw->AddCircleFilled(ImVec2(game.m_joystickCurrentPx.x, game.m_joystickCurrentPx.y), stickRadius, IM_COL32(255, 255, 255, 150));
        }
    } else {
        // ================= 编辑布局模式 =================
        // 允许拖拽摇杆的默认位置
        ImVec2 center(game.m_joystickBasePos.x * sz.x, game.m_joystickBasePos.y * sz.y);
        
        ImGui::SetCursorScreenPos(ImVec2(center.x - baseRadius, center.y - baseRadius));
        ImGui::InvisibleButton("##joystick_base", ImVec2(baseRadius * 2.0f, baseRadius * 2.0f));
        
        bool isHovered = ImGui::IsItemHovered();
        bool isActive = ImGui::IsItemActive();
        
        if (isActive) {
            game.m_joystickBasePos.x += io.MouseDelta.x / sz.x;
            game.m_joystickBasePos.y += io.MouseDelta.y / sz.y;
            // 限制在左半屏内，防止拖到右边影响按钮
            game.m_joystickBasePos.x = glm::clamp(game.m_joystickBasePos.x, 0.05f, 0.45f);
            game.m_joystickBasePos.y = glm::clamp(game.m_joystickBasePos.y, 0.05f, 0.95f);
            center = ImVec2(game.m_joystickBasePos.x * sz.x, game.m_joystickBasePos.y * sz.y);
        }
        
        ImU32 color = IM_COL32(255, 100, 100, 120);
        if (isActive) color = IM_COL32(255, 255, 255, 80);
        else if (isHovered) color = IM_COL32(255, 255, 255, 50);
        
        fgDraw->AddCircleFilled(center, baseRadius, color);
        fgDraw->AddCircle(center, baseRadius, IM_COL32(255, 255, 255, 150), 32, 2.0f);
        fgDraw->AddCircleFilled(center, stickRadius, IM_COL32(255, 255, 255, 150));
    }
    
    // 3. 编辑模式 UI 提示（嵌入 ##EditLayoutRoot 内部，中央面板）
    //   原实现创建独立的 "##edit_controls" 窗口，但拖拽虚拟按钮时 InvisibleButton
    //   会 FocusWindow(##EditLayoutRoot) 把全屏 Root 拉到顶层，覆盖掉独立面板 →
    //   Done & Save 点不动。现在改为在当前窗口（##EditLayoutRoot）内部用
    //   SetCursorScreenPos + Button 绘制，天然在同一层，无焦点抢占问题。
    if (game.m_isEditingLayout) {
        constexpr float kEditPanelWidth  = 240.0f;
        constexpr float kEditPanelHeight = 100.0f;
        ImVec2 panelPos(sz.x * 0.5f - kEditPanelWidth * 0.5f,
                        sz.y * 0.5f - kEditPanelHeight * 0.5f);
        
        // 背景（直接画到当前窗口 DrawList，即 ##EditLayoutRoot 的 DrawList）
        ImGui::GetWindowDrawList()->AddRectFilled(
            panelPos,
            ImVec2(panelPos.x + kEditPanelWidth, panelPos.y + kEditPanelHeight),
            IM_COL32(20, 20, 20, 220), 4.0f);
        ImGui::GetWindowDrawList()->AddRect(
            panelPos,
            ImVec2(panelPos.x + kEditPanelWidth, panelPos.y + kEditPanelHeight),
            IM_COL32(255, 255, 255, 150), 4.0f, 0, 2.0f);
        
        ImGui::SetCursorScreenPos(ImVec2(panelPos.x + 10.0f, panelPos.y + 8.0f));
        ImGui::TextColored(COLOR_WARNING, "Touch Layout Edit Mode");
        
        ImGui::SetCursorScreenPos(ImVec2(panelPos.x + 10.0f, panelPos.y + 30.0f));
        ImGui::TextUnformatted("Drag buttons to reposition.");
        
        ImGui::SetCursorScreenPos(ImVec2(panelPos.x + 10.0f, panelPos.y + 55.0f));
        if (ImGui::Button("Done & Save", ImVec2(kEditPanelWidth - 20.0f, 35.0f))) {
            game.m_isEditingLayout = false;
            game.m_menuOpen = true; // 退出编辑模式后，重新打开设置菜单
            game.SaveSettings(engine);
        }
    }
}

} // namespace game::client