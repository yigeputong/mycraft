#pragma once
#include <cstdint>
#include <glm/glm.hpp>

#include "game/core/BlockRegistry.h"

namespace game::core::ecs {

// ============================================================
// 两端共享的组件：客户端和服务端都声明、都写。
// ★ 硬规矩：任何带模型 / 网格 / 渲染后端类型的组件都不许进来。
//   这个头就是"服务端能不能裁掉 assimp / Vulkan"的分界线。
// ============================================================

struct TransformComponent {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct VelocityComponent {
    glm::vec3 moveDir{0.0f};
    float speed = 0.0f;
    float yVelocity = 0.0f;
    bool onGround = false;
};

struct AABBComponent {
    glm::vec3 halfExtents{0.45f, 0.45f, 0.45f};
    bool positionIsFeet = true;
};

struct PigAIComponent {
    enum class State : std::uint8_t { Idle, Walk };
    State state = State::Idle;
    float idleTimer = 2.0f;
    float walkTimer = 0.0f;
    float stuckTimer = 0.0f;
    glm::vec3 targetPos{0.0f};
    float baseSpeed = 2.0f;
    uint32_t rngState = 1337;
};

struct ItemComponent {
    BlockId itemType = AIR_ID;
    int count = 1;
    float pickupDelay = 0.5f;
    float bobTime = 0.0f;
    float age = 0.0f;
    bool isPickingUp = false;
    float pickupT = 0.0f;
    glm::vec3 pickupStart{0.0f};
    uint32_t pickerId = 0;
};

// 网络插值属于网络语义、不是渲染语义，所以留在共享头：
// 服务端永远不会设置它，但 PhysicsSystem 要读它来决定"这个实体不由本地模拟"。
struct NetInterpolationComponent {
    glm::vec3 prevPos{0.0f};
    glm::vec3 targetPos{0.0f};
    float interpT = 1.0f;
};

}  // namespace game::core::ecs