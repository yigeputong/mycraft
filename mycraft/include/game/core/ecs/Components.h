#pragma once

#include <glm/glm.hpp>
#include <string>

#include "game/client/ModelLoader.h"

namespace game::core::ecs {

struct TransformComponent {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;    // 水平朝向（度）。约定：front = (cos yaw, 0, sin yaw)
    float pitch = 0.0f;  // 垂直朝向（度）。仅玩家相机使用，其他实体默认 0
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

// 掉落物组件
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

// 网络插值组件
struct NetInterpolationComponent {
    glm::vec3 prevPos{0.0f};
    glm::vec3 targetPos{0.0f};
    float interpT = 1.0f;
};

// ★ 新增：玩家组件
// 玩家物理权威走 game::StepPlayer，velocity 是完整三维向量，
// 不复用 VelocityComponent（那是猪的 moveDir+speed 拆分语义）。
struct PlayerComponent {
    glm::vec3 velocity{0.0f};  // StepPlayer 权威速度
    bool onGround = false;
    bool flyMode = true;
    float jumpHoldTimer = 0.0f;     // 跳跃输入保持窗口
    glm::vec3 positionError{0.0f};  // 服务端校正残差（平滑叠加到 transform）
};

// 渲染组件：模型（猪）与方块（掉落物）二选一
struct RenderableComponent {
    client::LoadedModel* model = nullptr;
    std::string currentClip = "Walk";
    float animTime = 0.0f;

    BlockId blockType = AIR_ID;
};

}  // namespace game::core::ecs