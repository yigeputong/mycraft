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
    // ============================================================
    // 各个界面绘制函数
    // ============================================================
    void DrawMainMenu(Eng::Engine& engine, MyGame& game);
    
    // 世界选择界面（左右分栏布局）
    void DrawWorldSelectMenu(Eng::Engine& engine, MyGame& game);
    
    void DrawSettingsMenu(Eng::Engine& engine, MyGame& game);
    void DrawHUD(Eng::Engine& engine, MyGame& game);

    // ============================================================
    // UI 辅助工具 (由各个界面复用)
    // ============================================================
    // 带阴影的文字绘制（提升可读性）
    void DrawTextWithShadow(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text);
    
    // 旋转文字（用于主菜单的副标题）
    void DrawRotatedText(ImDrawList* dl, ImVec2 center, const char* text, float angleRad, ImU32 color);
    
    // 自定义风格按钮（支持响应式尺寸，常用于触屏）
    bool DrawButton(const char* label, ImVec2 size);
    
    // 3D 等距方块图标（用于物品栏、合成表等）
    void DrawIsometricBlock(ImDrawList* dl, ImVec2 center, float size, BlockId id, ImTextureID texId, const BlockRegistry& reg);
};

} // namespace client
} // namespace game