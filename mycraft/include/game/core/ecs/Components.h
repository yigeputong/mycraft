#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

#include "game/client/ModelLoader.h"
#include "game/core/Blocks.h"

namespace game {
struct Chunk;
}

namespace game::core::ecs {

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

struct NetInterpolationComponent {
    glm::vec3 prevPos{0.0f};
    glm::vec3 targetPos{0.0f};
    float interpT = 1.0f;
};

// 客户端专属：平滑插值、预测误差等
struct PlayerComponent {
    glm::vec3 velocity{0.0f};
    bool onGround = false;
    bool flyMode = true;
    float jumpHoldTimer = 0.0f;
    glm::vec3 positionError{0.0f};
};

// 服务端专属：网络同步、背包、权威物理
struct ServerPlayerComponent {
    uint32_t clientId = 0;
    std::string name;
    std::array<ItemStack, 36> inventory{};
    int32_t inventoryStateId = 0;
    ItemStack heldItem{};

    // 输入缓存
    bool jumpInput = false;
    bool flyMode = false;
    glm::vec3 moveInput{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;

    // 物理权威缓存 (给 StepPlayer 用)
    glm::vec3 velocity{0.0f};
    bool onGround = false;
};

struct RenderableComponent {
    client::LoadedModel* model = nullptr;
    std::string currentClip = "Walk";
    float animTime = 0.0f;
    BlockId blockType = AIR_ID;
};

// 区块坐标组件
struct ChunkCoordComponent {
    int cx = 0;
    int cz = 0;
};

// 区块数据组件（持有数据与Mesh版本）
struct ChunkComponent {
    std::shared_ptr<Chunk> data = nullptr;  // 共享区块数据
    uint32_t meshVersion = 0;               // 用于校验异步建网格时的版本冲突
    bool isMeshDirty = false;               // 是否需要重新建网格
};

// 区块渲染组件（客户端专用）
struct ChunkRenderComponent {
    // 未来这里会存 VBO/IBO 的 offset、indexCount 等
    uint32_t indexCount = 0;
    bool isUploaded = false;
};

}  // namespace game::core::ecs