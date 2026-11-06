#pragma once

#include "Engine/Engine.h"
#include <imgui.h>
#include "game/core/BlockRegistry.h"

namespace game {
class MyGame; // 前置声明

namespace client {

class ClientUI {
public:
    // 主绘制入口
    void Draw(Eng::Engine& engine, MyGame& game);

private:
    // 各个界面绘制函数
    void DrawMainMenu(Eng::Engine& engine, MyGame& game);
    void DrawWorldSelectMenu(Eng::Engine& engine, MyGame& game);
    void DrawSettingsMenu(Eng::Engine& engine, MyGame& game);
    void DrawHUD(Eng::Engine& engine, MyGame& game);
    
    // 触控输入处理与 UI 绘制
    void ProcessTouchInputAndDrawOverlay(Eng::Engine& engine, MyGame& game);

    // UI 辅助工具
    void DrawTextWithShadow(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text);
    void DrawRotatedText(ImDrawList* dl, ImVec2 center, const char* text, float angleRad, ImU32 color);
    bool DrawButton(const char* label, ImVec2 size);
    void DrawIsometricBlock(ImDrawList* dl, ImVec2 center, float size, BlockId id, ImTextureID texId, const BlockRegistry& reg);
};

} // namespace client
} // namespace game