#pragma once

#include <unordered_map>
#include <glm/glm.hpp>
#include <SDL3/SDL.h>

namespace Eng::client {

enum class KeyCode {
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    Space, LShift, RShift, LCtrl, RCtrl, LAlt, RAlt,
    Escape, Tab, Enter, Backspace,
    Up, Down, Left, Right,

    MouseLeft, MouseRight, MouseMiddle,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
};

class Input {
public:
    static Input& Get();

    // 必须在每帧开始时调用（更新状态）
    void Update();

    // ---- 查询接口 ----
    // 按键是否被按住（持续按住）
    static bool IsKeyDown(KeyCode key);
    // 按键是否刚被按下（上升沿）
    static bool IsKeyPressed(KeyCode key);
    // 按键是否刚被释放（下降沿）
    static bool IsKeyReleased(KeyCode key);

    // 鼠标位置（绝对坐标，窗口坐标）
    static glm::vec2 GetMousePosition();
    // 鼠标相对移动（自上一帧以来的偏移）
    static glm::vec2 GetMouseDelta();
    // 获取垂直滚轮偏移量（正=向上滚动）
    static float GetScrollDelta(); ;
    // 设置鼠标位置（用于重置或锁定）
    static void SetMousePosition(SDL_Window* window, float x, float y);

    // 启用/禁用相对鼠标模式（隐藏+锁定）
    static void SetRelativeMode(SDL_Window* window, bool enabled);
    static bool IsRelativeMode();

    // 内部事件处理（由 WindowManager 调用）
    void ProcessEvent(const SDL_Event& event);

private:
    Input() = default;

    // 状态存储
    std::unordered_map<KeyCode, bool> m_keyState;      // 当前帧是否按下
    std::unordered_map<KeyCode, bool> m_keyPrevState;  // 上一帧是否按下

    glm::vec2 m_mousePos = glm::vec2(0.0f);
    glm::vec2 m_mouseDelta = glm::vec2(0.0f);
    bool m_firstMouse = true;
    float m_scrollDelta = 0.0f;

    bool m_relativeMode = false;
};
    
} // namespace Eng::client
