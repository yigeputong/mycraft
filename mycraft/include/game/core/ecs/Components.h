#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "game/client/ModelLoader.h"  // 包含 RenderableComponent 和 LoadedModel

namespace game::core::ecs {

// 纯数据组件，不包含任何业务逻辑
struct TransformComponent {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};

struct VelocityComponent {
    glm::vec3 moveDir{0.0f};
    float speed = 0.0f;

    float yVelocity = 0.0f;  // 垂直下落速度
    bool onGround = false;   // 是否在地面上
};

struct AABBComponent {
    glm::vec3 halfExtents{0.45f, 0.45f, 0.45f};  // 默认猪的碰撞盒半长
};

struct PigAIComponent {
    enum class State : std::uint8_t { Idle, Walk };
    State state = State::Idle;

    float idleTimer = 2.0f;   // 发呆倒计时
    float walkTimer = 0.0f;   // 走路超时倒计时
    float stuckTimer = 0.0f;  // 卡死检测计时

    glm::vec3 targetPos{0.0f};
    float baseSpeed = 2.0f;
    uint32_t rngState = 1337;  // 独立随机种子
};

// 注意：RenderableComponent 已经定义在 game::client 命名空间下
// 为了 ECS 的纯粹性，我们可以直接用 client::RenderableComponent，或在此做一个别名
using client::RenderableComponent;

}  // namespace game::core::ecs