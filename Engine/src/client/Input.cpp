#include "client/Input.h"
#include <SDL3/SDL.h>

namespace Eng::client {

// ---------- 工具函数（SDL 键码 → 我们的 KeyCode） ----------
static KeyCode SDLToKeyCode(SDL_Scancode scancode) {
    switch (scancode) {
        // 字母
        case SDL_SCANCODE_A: return KeyCode::A;
        case SDL_SCANCODE_B: return KeyCode::B;
        case SDL_SCANCODE_C: return KeyCode::C;
        case SDL_SCANCODE_D: return KeyCode::D;
        case SDL_SCANCODE_E: return KeyCode::E;
        case SDL_SCANCODE_F: return KeyCode::F;
        case SDL_SCANCODE_G: return KeyCode::G;
        case SDL_SCANCODE_H: return KeyCode::H;
        case SDL_SCANCODE_I: return KeyCode::I;
        case SDL_SCANCODE_J: return KeyCode::J;
        case SDL_SCANCODE_K: return KeyCode::K;
        case SDL_SCANCODE_L: return KeyCode::L;
        case SDL_SCANCODE_M: return KeyCode::M;
        case SDL_SCANCODE_N: return KeyCode::N;
        case SDL_SCANCODE_O: return KeyCode::O;
        case SDL_SCANCODE_P: return KeyCode::P;
        case SDL_SCANCODE_Q: return KeyCode::Q;
        case SDL_SCANCODE_R: return KeyCode::R;
        case SDL_SCANCODE_S: return KeyCode::S;
        case SDL_SCANCODE_T: return KeyCode::T;
        case SDL_SCANCODE_U: return KeyCode::U;
        case SDL_SCANCODE_V: return KeyCode::V;
        case SDL_SCANCODE_W: return KeyCode::W;
        case SDL_SCANCODE_X: return KeyCode::X;
        case SDL_SCANCODE_Y: return KeyCode::Y;
        case SDL_SCANCODE_Z: return KeyCode::Z;

        // 数字
        case SDL_SCANCODE_0: return KeyCode::Num0;
        case SDL_SCANCODE_1: return KeyCode::Num1;
        case SDL_SCANCODE_2: return KeyCode::Num2;
        case SDL_SCANCODE_3: return KeyCode::Num3;
        case SDL_SCANCODE_4: return KeyCode::Num4;
        case SDL_SCANCODE_5: return KeyCode::Num5;
        case SDL_SCANCODE_6: return KeyCode::Num6;
        case SDL_SCANCODE_7: return KeyCode::Num7;
        case SDL_SCANCODE_8: return KeyCode::Num8;
        case SDL_SCANCODE_9: return KeyCode::Num9;

        // 功能键
        case SDL_SCANCODE_SPACE:      return KeyCode::Space;
        case SDL_SCANCODE_LSHIFT:     return KeyCode::LShift;
        case SDL_SCANCODE_RSHIFT:     return KeyCode::RShift;
        case SDL_SCANCODE_LCTRL:      return KeyCode::LCtrl;
        case SDL_SCANCODE_RCTRL:      return KeyCode::RCtrl;
        case SDL_SCANCODE_LALT:       return KeyCode::LAlt;
        case SDL_SCANCODE_RALT:       return KeyCode::RAlt;
        case SDL_SCANCODE_ESCAPE:     return KeyCode::Escape;
        case SDL_SCANCODE_TAB:        return KeyCode::Tab;
        case SDL_SCANCODE_RETURN:     return KeyCode::Enter;
        case SDL_SCANCODE_BACKSPACE:  return KeyCode::Backspace;

        //F键
        case SDL_SCANCODE_F1:         return KeyCode::F1;
        case SDL_SCANCODE_F2:         return KeyCode::F2;
        case SDL_SCANCODE_F3:         return KeyCode::F3;
        case SDL_SCANCODE_F4:         return KeyCode::F4;
        case SDL_SCANCODE_F5:         return KeyCode::F5;
        case SDL_SCANCODE_F6:         return KeyCode::F6;
        case SDL_SCANCODE_F7:         return KeyCode::F7;
        case SDL_SCANCODE_F8:         return KeyCode::F8;
        case SDL_SCANCODE_F9:         return KeyCode::F9;
        case SDL_SCANCODE_F10:        return KeyCode::F10;
        case SDL_SCANCODE_F11:        return KeyCode::F11;
        case SDL_SCANCODE_F12:        return KeyCode::F12;

        // 方向键
        case SDL_SCANCODE_UP:       return KeyCode::Up;
        case SDL_SCANCODE_DOWN:     return KeyCode::Down;
        case SDL_SCANCODE_LEFT:     return KeyCode::Left;
        case SDL_SCANCODE_RIGHT:    return KeyCode::Right;

        default:    return KeyCode::None; // fallback
    }
}

// ---------- 鼠标按钮转换 ----------
static KeyCode MouseButtonToKeyCode(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:   return KeyCode::MouseLeft;
        case SDL_BUTTON_RIGHT:  return KeyCode::MouseRight;
        case SDL_BUTTON_MIDDLE: return KeyCode::MouseMiddle;
        default:                return KeyCode::None; // fallback
    }
}

// ---------- 单例 ----------
Input& Input::Get() {
    static Input instance;
    return instance;
}

// ---------- 更新（每帧调用） ----------
void Input::Update() {
    // 把当前状态备份到上一帧
    m_keyPrevState = m_keyState;

    m_mouseDelta = glm::vec2(0.0f);
    m_scrollDelta = 0.0f;
}

// ---------- 事件处理（由 WindowManager 调用） ----------
void Input::ProcessEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            m_keyState[SDLToKeyCode(event.key.scancode)] = true;
            break;
        case SDL_EVENT_KEY_UP:
            m_keyState[SDLToKeyCode(event.key.scancode)] = false;
            break;

        case SDL_EVENT_MOUSE_MOTION:
            m_mousePos.x = event.motion.x;
            m_mousePos.y = event.motion.y;
            if (!m_firstMouse) {
                m_mouseDelta.x += event.motion.xrel;
                m_mouseDelta.y += event.motion.yrel;
            } else {
                m_firstMouse = false;
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            m_keyState[MouseButtonToKeyCode(event.button.button)] = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            m_keyState[MouseButtonToKeyCode(event.button.button)] = false;
            break;

        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            m_firstMouse = true; // 鼠标重新进入窗口，重置 last
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            m_scrollDelta += event.wheel.y; // 累加（向上滚为正，向下为负）
            break;
        default:
            break;
    }
}

// ---------- 查询函数 ----------
bool Input::IsKeyDown(KeyCode key) {
    auto it = Get().m_keyState.find(key);
    return it != Get().m_keyState.end() && it->second;
}

bool Input::IsKeyPressed(KeyCode key) {
    auto& state = Get().m_keyState;
    auto& prev = Get().m_keyPrevState;
    return state[key] && !prev[key];
}

bool Input::IsKeyReleased(KeyCode key) {
    auto& state = Get().m_keyState;
    auto& prev = Get().m_keyPrevState;
    return !state[key] && prev[key];
}

glm::vec2 Input::GetMousePosition() {
    return Get().m_mousePos;
}

glm::vec2 Input::GetMouseDelta() {
    return Get().m_mouseDelta;
}

float Input::GetScrollDelta() {
    return Get().m_scrollDelta;
}

void Input::SetMousePosition(SDL_Window* window, float x, float y) {
    SDL_WarpMouseInWindow(window, x, y); // nullptr = 当前聚焦窗口
    Get().m_mousePos = glm::vec2(x, y);
    Get().m_firstMouse = true;
}

void Input::SetRelativeMode(SDL_Window* window, bool enabled) {
    Get().m_relativeMode = enabled;
    SDL_SetWindowRelativeMouseMode(window, enabled);
}

bool Input::IsRelativeMode() {
    return Get().m_relativeMode;
}

} // namespace Eng::client