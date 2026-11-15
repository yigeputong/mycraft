#pragma once

#include <glm/glm.hpp>
#include <string>

#include "game/client/ModelLoader.h"

namespace game::core::ecs {

struct TransformComponent {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};

struct VelocityComponent {
    glm::vec3 moveDir{0.0f};
    float speed = 0.0f;
    float yVelocity = 0.0f;
    bool onGround = false;
};

struct AABBComponent {
    glm::vec3 halfExtents{0.45f, 0.45f, 0.45f};
    // position 语义：true = 脚底（玩家/猪），false = 几何中心（掉落物）
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

// ★ 新增：掉落物组件
struct ItemComponent {
    BlockId itemType = AIR_ID;
    int count = 1;
    float pickupDelay = 0.5f;
    float bobTime = 0.0f;

    bool isPickingUp = false;
    float pickupT = 0.0f;
    glm::vec3 pickupStart{0.0f};
    uint32_t pickerId = 0;
};

// ★ 新增：网络插值组件
struct NetInterpolationComponent {
    glm::vec3 prevPos{0.0f};
    glm::vec3 targetPos{0.0f};
    float interpT = 1.0f;
};

// ★ 改造：渲染组件支持方块和模型
struct RenderableComponent {
    client::LoadedModel* model = nullptr;
    std::string currentClip = "Walk";
    float animTime = 0.0f;

    BlockId blockType = AIR_ID;  // 如果是方块掉落物，这里存 BlockId
};

}  // namespace game::core::ecs