#pragma once
#include "Engine.h"
#include <imgui.h>
#include "../core/BlockRegistry.h" // ★ 引入 BlockId 和 BlockRegistry

namespace game {
class MyGame; // 前置声明

namespace client {

class ClientUI {
public:
    void Draw(Eng::Engine& engine, MyGame& game);
    
private:
    void DrawMainMenu(Eng::Engine& engine, MyGame& game);
    void DrawSettingsMenu(Eng::Engine& engine, MyGame& game);
    void DrawHUD(Eng::Engine& engine, MyGame& game);

    // UI 辅助工具
    void DrawTextWithShadow(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text);
    void DrawRotatedText(ImDrawList* dl, ImVec2 center, const char* text, float angleRad, ImU32 color);
    bool DrawButton(const char* label, ImVec2 size);
    
    // 3D 等距图标（用于物品栏）★ 改为 BlockId 并传入 BlockRegistry
    void DrawIsometricBlock(ImDrawList* dl, ImVec2 center, float size, BlockId id, ImTextureID texId, const BlockRegistry& reg);
};

} // namespace client
} // namespace game