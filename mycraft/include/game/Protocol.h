#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace game::net {

enum class MessageType : uint8_t {
    // 客户端 → 服务端
    PlayerInput     = 0x10,

    // 服务端 → 客户端
    WorldState      = 0x20,
    PlayerId        = 0x21,
    PlayerJoined    = 0x22,
    PlayerLeft      = 0x23,
};

struct PlayerInput {
    glm::vec3 moveDir{0.0f};   // 世界方向（已归一化）
    glm::vec2 look{0.0f};      // yaw/pitch（弧度，用于服务端逻辑）
    bool jump = false;
};

struct PlayerState {
    uint32_t  id = 0;
    glm::vec3 position{0.0f};
    float     yaw = 0.0f;
    float     pitch = 0.0f;
};

}