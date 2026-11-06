#include "Engine/service/InputService.h"

#include <algorithm>
#include <array>

#include <SDL3/SDL.h>

namespace Eng::service {

// ============================================================
// KeyCode 字符串化
// ============================================================
const char* KeyCodeToString(KeyCode k) {
    switch (k) {
        // 字母
        case KeyCode::A: return "A";
        case KeyCode::B: return "B";
        case KeyCode::C: return "C";
        case KeyCode::D: return "D";
        case KeyCode::E: return "E";
        case KeyCode::F: return "F";
        case KeyCode::G: return "G";
        case KeyCode::H: return "H";
        case KeyCode::I: return "I";
        case KeyCode::J: return "J";
        case KeyCode::K: return "K";
        case KeyCode::L: return "L";
        case KeyCode::M: return "M";
        case KeyCode::N: return "N";
        case KeyCode::O: return "O";
        case KeyCode::P: return "P";
        case KeyCode::Q: return "Q";
        case KeyCode::R: return "R";
        case KeyCode::S: return "S";
        case KeyCode::T: return "T";
        case KeyCode::U: return "U";
        case KeyCode::V: return "V";
        case KeyCode::W: return "W";
        case KeyCode::X: return "X";
        case KeyCode::Y: return "Y";
        case KeyCode::Z: return "Z";

        // 数字（"0"~"9" 比 "Num0"~"Num9" 简洁）
        case KeyCode::Num0: return "0";
        case KeyCode::Num1: return "1";
        case KeyCode::Num2: return "2";
        case KeyCode::Num3: return "3";
        case KeyCode::Num4: return "4";
        case KeyCode::Num5: return "5";
        case KeyCode::Num6: return "6";
        case KeyCode::Num7: return "7";
        case KeyCode::Num8: return "8";
        case KeyCode::Num9: return "9";

        // 特殊键
        case KeyCode::Space:      return "Space";
        case KeyCode::LShift:     return "LShift";
        case KeyCode::RShift:     return "RShift";
        case KeyCode::LCtrl:      return "LCtrl";
        case KeyCode::RCtrl:      return "RCtrl";
        case KeyCode::LAlt:       return "LAlt";
        case KeyCode::RAlt:       return "RAlt";
        case KeyCode::Escape:     return "Escape";
        case KeyCode::Tab:        return "Tab";
        case KeyCode::Enter:      return "Enter";
        case KeyCode::Backspace:  return "Backspace";
        case KeyCode::Up:         return "Up";
        case KeyCode::Down:       return "Down";
        case KeyCode::Left:       return "Left";
        case KeyCode::Right:      return "Right";

        // 鼠标
        case KeyCode::MouseLeft:   return "MouseLeft";
        case KeyCode::MouseRight:  return "MouseRight";
        case KeyCode::MouseMiddle: return "MouseMiddle";

        // F 键
        case KeyCode::F1:  return "F1";
        case KeyCode::F2:  return "F2";
        case KeyCode::F3:  return "F3";
        case KeyCode::F4:  return "F4";
        case KeyCode::F5:  return "F5";
        case KeyCode::F6:  return "F6";
        case KeyCode::F7:  return "F7";
        case KeyCode::F8:  return "F8";
        case KeyCode::F9:  return "F9";
        case KeyCode::F10: return "F10";
        case KeyCode::F11: return "F11";
        case KeyCode::F12: return "F12";

        case KeyCode::None: default: return "None";
    }
}

std::optional<KeyCode> KeyCodeFromString(std::string_view s) {
    // 一次构造，终身查表（~60 项）
    static const std::unordered_map<std::string_view, KeyCode> table = [] {
        std::unordered_map<std::string_view, KeyCode> t;
        t.reserve(64);
        t["None"] = KeyCode::None;
        for (int i = 0; i < 26; ++i)
            t[KeyCodeToString(static_cast<KeyCode>(static_cast<std::uint8_t>(KeyCode::A) + i))]
                = static_cast<KeyCode>(static_cast<std::uint8_t>(KeyCode::A) + i);
        for (int i = 0; i < 10; ++i)
            t[KeyCodeToString(static_cast<KeyCode>(static_cast<std::uint8_t>(KeyCode::Num0) + i))]
                = static_cast<KeyCode>(static_cast<std::uint8_t>(KeyCode::Num0) + i);
        for (int i = 0; i < 12; ++i)
            t[KeyCodeToString(static_cast<KeyCode>(static_cast<std::uint8_t>(KeyCode::F1) + i))]
                = static_cast<KeyCode>(static_cast<std::uint8_t>(KeyCode::F1) + i);

        const KeyCode specials[] = {
            KeyCode::Space, KeyCode::LShift, KeyCode::RShift,
            KeyCode::LCtrl, KeyCode::RCtrl, KeyCode::LAlt, KeyCode::RAlt,
            KeyCode::Escape, KeyCode::Tab, KeyCode::Enter, KeyCode::Backspace,
            KeyCode::Up, KeyCode::Down, KeyCode::Left, KeyCode::Right,
            KeyCode::MouseLeft, KeyCode::MouseRight, KeyCode::MouseMiddle,
        };
        for (auto kc : specials) t[KeyCodeToString(kc)] = kc;
        return t;
    }();

    auto it = table.find(s);
    if (it == table.end()) return std::nullopt;
    return it->second;
}

// ============================================================
// 内部工具：SDL scancode → KeyCode（你原有的查表实现）
// ============================================================
static KeyCode SDLToKeyCode(SDL_Scancode sc) {
    static_assert(SDL_SCANCODE_A + 25 == SDL_SCANCODE_Z);
    static_assert(SDL_SCANCODE_1 + 8  == SDL_SCANCODE_9);
    static_assert(SDL_SCANCODE_F1 + 11 == SDL_SCANCODE_F12);
    static_assert(static_cast<std::uint8_t>(KeyCode::A)    + 25 == static_cast<std::uint8_t>(KeyCode::Z));
    static_assert(static_cast<std::uint8_t>(KeyCode::Num0) + 9  == static_cast<std::uint8_t>(KeyCode::Num9));
    static_assert(static_cast<std::uint8_t>(KeyCode::F1)   + 11 == static_cast<std::uint8_t>(KeyCode::F12));

    static const auto table = [] {
        std::array<KeyCode, SDL_SCANCODE_COUNT> t{};
        t.fill(KeyCode::None);

        constexpr auto kc = [](KeyCode base, int offset) {
            return static_cast<KeyCode>(static_cast<std::uint8_t>(base) + offset);
        };

        for (int i = 0; i < 26; ++i)
            t[SDL_SCANCODE_A + i] = kc(KeyCode::A, i);
        for (int i = 1; i <= 9; ++i)
            t[SDL_SCANCODE_1 + (i - 1)] = kc(KeyCode::Num0, i);
        t[SDL_SCANCODE_0] = KeyCode::Num0;
        for (int i = 0; i < 12; ++i)
            t[SDL_SCANCODE_F1 + i] = kc(KeyCode::F1, i);

        t[SDL_SCANCODE_SPACE]     = KeyCode::Space;
        t[SDL_SCANCODE_LSHIFT]    = KeyCode::LShift;
        t[SDL_SCANCODE_RSHIFT]    = KeyCode::RShift;
        t[SDL_SCANCODE_LCTRL]     = KeyCode::LCtrl;
        t[SDL_SCANCODE_RCTRL]     = KeyCode::RCtrl;
        t[SDL_SCANCODE_LALT]      = KeyCode::LAlt;
        t[SDL_SCANCODE_RALT]      = KeyCode::RAlt;
        t[SDL_SCANCODE_ESCAPE]    = KeyCode::Escape;
        t[SDL_SCANCODE_TAB]       = KeyCode::Tab;
        t[SDL_SCANCODE_RETURN]    = KeyCode::Enter;
        t[SDL_SCANCODE_BACKSPACE] = KeyCode::Backspace;
        t[SDL_SCANCODE_UP]        = KeyCode::Up;
        t[SDL_SCANCODE_DOWN]      = KeyCode::Down;
        t[SDL_SCANCODE_LEFT]      = KeyCode::Left;
        t[SDL_SCANCODE_RIGHT]     = KeyCode::Right;

        return t;
    }();

    return static_cast<std::size_t>(sc) < SDL_SCANCODE_COUNT ? table[sc] : KeyCode::None;
}

static KeyCode MouseButtonToKeyCode(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:   return KeyCode::MouseLeft;
        case SDL_BUTTON_RIGHT:  return KeyCode::MouseRight;
        case SDL_BUTTON_MIDDLE: return KeyCode::MouseMiddle;
        default:                return KeyCode::None;
    }
}

// ============================================================
// 每帧调度
// ============================================================
void InputService::Update() {
    // 1) Action 整体状态机：先快照上帧结束状态到 prevDown
    for (auto& [h, slot] : m_actions) {
        slot.prevDown = ComputeActionDown(slot);
    }

    // 2) 键盘上帧快照 / 单帧量清零
    m_keyPrevState = m_keyState;
    m_mouseDelta   = glm::vec2(0.0f);
    m_scrollDelta  = 0.0f;

    // 3) 触控：更新 prevActive，清空本帧 delta；释放已抬起槽位
    for (auto& t : m_touches) {
        t.prevActive = t.active;
        t.delta      = glm::vec2(0.0f);
        if (!t.active) {
            t.fingerId = -1;
            t.pos      = glm::vec2(0.0f);
        }
    }
}

// ============================================================
// SDL 事件转发
// ============================================================
void InputService::ProcessEvent(const SDL_Event& event) {
    // --- 追踪最近使用的设备 ---
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            m_lastInputDevice = InputDevice::KeyboardMouse;
            break;

        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            m_lastInputDevice = InputDevice::Touch;
            break;
        default:
            break;
    }
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN: {
            KeyCode kc = SDLToKeyCode(event.key.scancode);
            if (kc != KeyCode::None) m_keyState[kc] = true;
            break;
        }
        case SDL_EVENT_KEY_UP: {
            KeyCode kc = SDLToKeyCode(event.key.scancode);
            if (kc != KeyCode::None) m_keyState[kc] = false;
            break;
        }

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

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            KeyCode kc = MouseButtonToKeyCode(event.button.button);
            if (kc != KeyCode::None) m_keyState[kc] = true;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            KeyCode kc = MouseButtonToKeyCode(event.button.button);
            if (kc != KeyCode::None) m_keyState[kc] = false;
            break;
        }

        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            m_firstMouse = true;
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            m_scrollDelta += event.wheel.y;
            break;

        // ---- 触控（本期预埋；桌面端 SDL3 不会合成 finger 事件） ----
        case SDL_EVENT_FINGER_DOWN: {
            for (auto& t : m_touches) {
                if (!t.active) {
                    t.fingerId = static_cast<std::int64_t>(event.tfinger.fingerID);
                    t.pos      = glm::vec2(event.tfinger.x, event.tfinger.y);
                    t.delta    = glm::vec2(0.0f);
                    t.active   = true;
                    break;
                }
            }
            break;
        }
        case SDL_EVENT_FINGER_MOTION: {
            const auto fid = static_cast<std::int64_t>(event.tfinger.fingerID);
            for (auto& t : m_touches) {
                if (t.active && t.fingerId == fid) {
                    t.pos.x = event.tfinger.x;
                    t.pos.y = event.tfinger.y;
                    t.delta.x += event.tfinger.dx;
                    t.delta.y += event.tfinger.dy;
                    break;
                }
            }
            break;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            const auto fid = static_cast<std::int64_t>(event.tfinger.fingerID);
            for (auto& t : m_touches) {
                if (t.active && t.fingerId == fid) {
                    t.active = false;
                    break;
                }
            }
            break;
        }

        default:
            break;
    }
}

// ============================================================
// 底层查询
// ============================================================
bool InputService::IsKeyDown(KeyCode key) const {
    auto it = m_keyState.find(key);
    return it != m_keyState.end() && it->second;
}

bool InputService::IsKeyPressed(KeyCode key) const {
    auto itCur  = m_keyState.find(key);
    auto itPrev = m_keyPrevState.find(key);
    const bool cur  = (itCur  != m_keyState.end())     && itCur->second;
    const bool prev = (itPrev != m_keyPrevState.end()) && itPrev->second;
    return cur && !prev;
}

bool InputService::IsKeyReleased(KeyCode key) const {
    auto itCur  = m_keyState.find(key);
    auto itPrev = m_keyPrevState.find(key);
    const bool cur  = (itCur  != m_keyState.end())     && itCur->second;
    const bool prev = (itPrev != m_keyPrevState.end()) && itPrev->second;
    return !cur && prev;
}

glm::vec2 InputService::GetMousePosition() const { return m_mousePos; }
glm::vec2 InputService::GetMouseDelta()    const { return m_mouseDelta; }
float     InputService::GetScrollDelta()   const { return m_scrollDelta; }

void InputService::SetMousePosition(SDL_Window* window, float x, float y) {
    SDL_WarpMouseInWindow(window, x, y);
    m_mousePos   = glm::vec2(x, y);
    m_firstMouse = true;
}

void InputService::SetRelativeMode(SDL_Window* window, bool enabled) {
    m_relativeMode = enabled;
    SDL_SetWindowRelativeMouseMode(window, enabled);
}

// ============================================================
// Action 注册
// ============================================================
bool InputService::RegisterActions(std::span<const ActionDesc> descs) {
    // 严格模式（问 4 = 4-A）：
    //  - name 为空 → 拒绝
    //  - 与已注册冲突 → 拒绝
    //  - 本批内重名 → 拒绝
    //  - 全部通过才原子插入
    std::vector<ActionHash> hashes;
    hashes.reserve(descs.size());

    for (const auto& d : descs) {
        if (d.name.empty()) return false;
        ActionHash h = HashActionName(d.name);
        if (m_actions.contains(h)) return false;
        if (std::find(hashes.begin(), hashes.end(), h) != hashes.end()) return false;
        hashes.push_back(h);
    }

    for (std::size_t i = 0; i < descs.size(); ++i) {
        ActionSlot slot;
        slot.name        = std::string(descs[i].name);
        slot.description = std::string(descs[i].description);
        auto keysSpan = descs[i].GetKeys();
        slot.keys.assign(keysSpan.begin(), keysSpan.end());
        slot.prevDown    = false;
        m_actions.emplace(hashes[i], std::move(slot));
    }
    return true;
}

void InputService::InjectActionState(ActionHash a, bool state) {
    auto it = m_actions.find(a);
    if (it != m_actions.end()) it->second.injected = state;
}

void InputService::InjectMoveDir(const glm::vec2& dir) {
    m_injectedMoveDir = dir;
}

void InputService::ResetInjectedStates() {
    for (auto& [h, slot] : m_actions) {
        slot.injected = false;
    }
    m_injectedMoveDir = glm::vec2(0.0f);
}

// ============================================================
// Action 查询
// ============================================================
bool InputService::ComputeActionDown(const ActionSlot& slot) const {
    if (slot.injected) return true; // 注入优先
    for (KeyCode k : slot.keys) {
        if (IsKeyDown(k)) return true;
    }
    return false;
}

bool InputService::IsActionDown(ActionHash a) const {
    auto it = m_actions.find(a);
    return it != m_actions.end() && ComputeActionDown(it->second);
}

bool InputService::IsActionPressed(ActionHash a) const {
    auto it = m_actions.find(a);
    if (it == m_actions.end()) return false;
    // 整体状态机：上帧 up 且本帧 down
    return !it->second.prevDown && ComputeActionDown(it->second);
}

bool InputService::IsActionReleased(ActionHash a) const {
    auto it = m_actions.find(a);
    if (it == m_actions.end()) return false;
    // 整体状态机：上帧 down 且本帧 up
    return it->second.prevDown && !ComputeActionDown(it->second);
}

bool InputService::IsActionDown(std::string_view name) const {
    return IsActionDown(HashActionName(name));
}

bool InputService::IsActionPressed(std::string_view name) const {
    return IsActionPressed(HashActionName(name));
}

bool InputService::IsActionReleased(std::string_view name) const {
    return IsActionReleased(HashActionName(name));
}

// ============================================================
// 移动方向特例
// ============================================================
void InputService::SetMoveActions(ActionHash forward, ActionHash back,
                                  ActionHash left,    ActionHash right) {
    m_moveForward = forward;
    m_moveBack    = back;
    m_moveLeft    = left;
    m_moveRight   = right;
    m_moveBound   = (forward != 0 && back != 0 && left != 0 && right != 0);
}

glm::vec2 InputService::GetMoveDir() const {
    if (glm::length(m_injectedMoveDir) > 1e-4f) {
        float len = glm::length(m_injectedMoveDir);
        return len > 1.0f ? (m_injectedMoveDir / len) : m_injectedMoveDir;
    }
    if (!m_moveBound) return glm::vec2(0.0f);
    const float x = static_cast<float>(IsActionDown(m_moveRight)) - static_cast<float>(IsActionDown(m_moveLeft));
    const float y = static_cast<float>(IsActionDown(m_moveForward)) - static_cast<float>(IsActionDown(m_moveBack));
    glm::vec2 v(x, y);
    const float len = glm::length(v);
    if (len > 1.0f) v /= len;
    return v;
}

// ============================================================
// 重映射 / 序列化
// ============================================================
void InputService::SetActionKeys(ActionHash a, std::span<const KeyCode> keys) {
    auto it = m_actions.find(a);
    if (it == m_actions.end()) return;
    it->second.keys.assign(keys.begin(), keys.end());
}

std::vector<KeyCode> InputService::GetActionKeys(ActionHash a) const {
    auto it = m_actions.find(a);
    if (it == m_actions.end()) return {};
    return it->second.keys;
}

std::vector<InputService::BindingSnapshot> InputService::ExportBindings() const {
    std::vector<BindingSnapshot> out;
    out.reserve(m_actions.size());
    for (const auto& [h, slot] : m_actions) {
        BindingSnapshot s;
        s.name = slot.name;
        s.keys = slot.keys;
        out.push_back(std::move(s));
    }
    return out;
}

bool InputService::ImportBindings(std::span<const BindingSnapshot> bindings) {
    bool allOk = true;
    for (const auto& snap : bindings) {
        auto it = m_actions.find(HashActionName(snap.name));
        if (it == m_actions.end()) {
            allOk = false;  // 未知 action：忽略，但告知调用方
            continue;
        }
        it->second.keys = snap.keys;
    }
    return allOk;
}

} // namespace Eng::service