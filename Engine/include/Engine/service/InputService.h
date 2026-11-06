#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <SDL3/SDL.h>

namespace Eng::service {

enum class KeyCode : std::uint8_t {
    None,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    Space, LShift, RShift, LCtrl, RCtrl, LAlt, RAlt,
    Escape, Tab, Enter, Backspace,
    Up, Down, Left, Right,

    MouseLeft, MouseRight, MouseMiddle,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
};

// ---- KeyCode 字符串化（供配置文件读写） ----
const char* KeyCodeToString(KeyCode k);
std::optional<KeyCode> KeyCodeFromString(std::string_view s);

// ---- Action 哈希（编译期可算，业务层可用 "_action" 字面量） ----
using ActionHash = std::uint64_t;

constexpr ActionHash HashActionName(std::string_view name) noexcept {
    // FNV-1a 64
    std::uint64_t h = 14695981039346656037ull;
    for (char c : name) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

constexpr ActionHash operator""_action(const char* s, std::size_t n) noexcept {
    return HashActionName(std::string_view{s, n});
}

// ---- Action 描述（业务层注册用） ----
// 使用固定容量 std::array<KeyCode, N> 而非 std::vector：
//  - 使 ActionDesc 可用 constexpr 初始化，避免 static storage duration 抛异常
//  - 每 action 最多 kMaxKeysPerAction 个键（当前 4，超出部分会被截断）
struct ActionDesc {
    static constexpr std::size_t kMaxKeysPerAction = 4;

    std::string_view                        name;          // 注册名，全局唯一
    std::array<KeyCode, kMaxKeysPerAction>  keys{};        // 有效项在 [0, keysCount)
    std::uint8_t                            keysCount = 0; // keys 有效项数
    std::string_view                        description;   // 可选（UI 显示）

    constexpr ActionDesc(std::string_view n,
                         std::initializer_list<KeyCode> ks,
                         std::string_view d) noexcept
        : name(n), description(d) {
        for (KeyCode k : ks) {
            if (keysCount >= kMaxKeysPerAction) break;
            keys[keysCount++] = k;
        }
    }

    constexpr std::span<const KeyCode> GetKeys() const noexcept {
        return {keys.data(), static_cast<std::size_t>(keysCount)};
    }
};

// 输入服务。由 Engine::Init() 创建并持有；WindowService 通过构造注入获得引用（可为空）。
// 生命周期：Engine 持有 unique_ptr；WindowService 只持观察指针。
class InputService final {
public:
    InputService() = default;
    ~InputService() = default;

    InputService(const InputService&) = delete;
    InputService& operator=(const InputService&) = delete;

    // ============================================================
    // 每帧调度
    // ============================================================
    void Update();

    // SDL 事件转发（由 WindowService::DispatchEvent 调用）
    void ProcessEvent(const SDL_Event& event);

    // ============================================================
    // 键盘/鼠标底层查询（保留；业务层应优先用 Action API）
    // ============================================================
    bool IsKeyDown(KeyCode key) const;
    bool IsKeyPressed(KeyCode key) const;
    bool IsKeyReleased(KeyCode key) const;

    glm::vec2 GetMousePosition() const;
    glm::vec2 GetMouseDelta() const;
    float     GetScrollDelta() const;

    void SetMousePosition(SDL_Window* window, float x, float y);
    void SetRelativeMode(SDL_Window* window, bool enabled);
    bool IsRelativeMode() const { return m_relativeMode; }

    // ============================================================
    // Action 注册表
    // ============================================================
    // 严格模式：任一 name 为空、与已注册名冲突、或本批内重名，整批拒绝，返回 false。
    // 成功时整批原子插入。
    bool RegisterActions(std::span<const ActionDesc> descs);

    void InjectActionState(ActionHash a, bool state);
    void InjectMoveDir(const glm::vec2& dir);
    void ResetInjectedStates();

    // ============================================================
    // Action 查询（ActionHash 版本 = 编译期 hash，零开销）
    // ============================================================
    bool IsActionDown(ActionHash a) const;      // 当前任一绑定键按住
    bool IsActionPressed(ActionHash a) const;   // action 整体状态：上帧 up → 本帧 down
    bool IsActionReleased(ActionHash a) const;  // action 整体状态：上帧 down → 本帧 up

    // 字符串重载（低频路径用，比如 UI/调试；内部 HashActionName 后查表）
    bool IsActionDown(std::string_view name) const;
    bool IsActionPressed(std::string_view name) const;
    bool IsActionReleased(std::string_view name) const;

    // ============================================================
    // 移动方向特例（必须显式绑定 4 个 action 才有意义）
    // ============================================================
    // 约定：forward/back/left/right 是业务层注册的 action 哈希。
    // 返回值：x = right - left，y = forward - back；
    //         magnitude 上限 1（斜向组合时归一化，单轴保持 =1）。
    //        未绑定 SetMoveActions 时返回 (0,0)。
    void SetMoveActions(ActionHash forward, ActionHash back,
                        ActionHash left,    ActionHash right);
    glm::vec2 GetMoveDir() const;

    // ============================================================
    // Action 绑定重映射 / 序列化
    // ============================================================
    void SetActionKeys(ActionHash a, std::span<const KeyCode> keys);
    std::vector<KeyCode> GetActionKeys(ActionHash a) const;

    struct BindingSnapshot {
        std::string          name;
        std::vector<KeyCode> keys;
    };

    // 导出全部 action（含当前键位），供业务层写 settings.json。
    std::vector<BindingSnapshot> ExportBindings() const;

    // 从外部导入。返回 true 表示全部条目均应用成功；缺失 action 会被忽略。
    // 返回值 false 仅表示存在被忽略的条目（调用方可以打 warning）。
    bool ImportBindings(std::span<const BindingSnapshot> bindings);

    // ============================================================
    // 触控预埋（本期业务层不消费）
    // ============================================================
    static constexpr std::size_t MAX_TOUCHES = 10;

    struct TouchPoint {
        std::int64_t fingerId    = -1;         // -1 = 空闲槽位
        glm::vec2    pos         = {0.0f, 0.0f}; // 归一化 [0,1]，左上角原点
        glm::vec2    delta       = {0.0f, 0.0f}; // 本帧累计，归一化
        bool         active      = false;
        bool         prevActive  = false;      // 上帧 active 状态（内部用）
    };

    // 返回全部槽位（含未激活的），业务层自行过滤 active。
    std::span<const TouchPoint> GetAllTouches() const { return m_touches; }

    enum class InputDevice : std::uint8_t {
        None,
        KeyboardMouse,
        Touch
    };
    InputDevice GetLastInputDevice() const { return m_lastInputDevice; }

private:
    struct ActionSlot {
        std::string          name;
        std::string          description;
        std::vector<KeyCode> keys;
        bool                 prevDown = false;  // 上帧结束时的 down 状态（整体状态机用）
        bool                 injected = false;
    };

    // 底层状态
    std::unordered_map<KeyCode, bool> m_keyState;
    std::unordered_map<KeyCode, bool> m_keyPrevState;

    glm::vec2 m_mousePos    = {0.0f, 0.0f};
    glm::vec2 m_mouseDelta  = {0.0f, 0.0f};
    bool      m_firstMouse  = true;
    float     m_scrollDelta = 0.0f;
    glm::vec2 m_injectedMoveDir = {0.0f, 0.0f};

    bool m_relativeMode = false;
    InputDevice m_lastInputDevice = InputDevice::None;

    // Action 表
    std::unordered_map<ActionHash, ActionSlot> m_actions;

    // 移动方向 4 个 action（业务层通过 SetMoveActions 绑定）
    ActionHash m_moveForward = 0;
    ActionHash m_moveBack    = 0;
    ActionHash m_moveLeft    = 0;
    ActionHash m_moveRight   = 0;
    bool       m_moveBound   = false;

    // 触控
    std::array<TouchPoint, MAX_TOUCHES> m_touches{};

    // 内部工具
    bool ComputeActionDown(const ActionSlot& slot) const;
};

} // namespace Eng::service