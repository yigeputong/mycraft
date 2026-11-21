#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

#include "game/core/Blocks.h"  // ItemStack

namespace game::server {

// ============================================================
// 服务端专属组件：网络同步、背包、权威物理。
// 客户端看不到这个文件。
// ============================================================
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

}  // namespace game::server