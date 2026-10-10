#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace game::net {

enum class MessageType : uint8_t {
    // 客户端 → 服务端
    PlayerInput     = 0x10,
    ChunkRequest    = 0x11,

    // 服务端 → 客户端
    PlayerId        = 0x20,
    WorldState      = 0x21,
    ChunkData       = 0x22, //| 4bype cx | 4byte cy | 4096byte data |
    BlockChange     = 0x23, //| 4bype bx | 4bype by | 4bype bz | 2bype blockId |
    EntitySpawn	    = 0x24, //[u32 id][vec3 pos][u16 blockType]
    EntityDestroy	= 0x25, //[u32 id][u8 reason] reason: 0=超时消失 1=被捡走
    EntityUpdate    = 0x26

};

struct PlayerInput {
    glm::vec3 moveDir{0.0f};   // 世界方向（已归一化）
    glm::vec2 look{0.0f};      // yaw/pitch（弧度，用于服务端逻辑）
    bool jump = false;
    bool dig   = false;   // 挖
    bool place = false;   // 放
    uint16_t placeBlock = 1; // 放的方块
    bool fly = false;
};

struct PlayerState {
    uint32_t  id = 0;
    glm::vec3 position{0.0f};
    float     yaw = 0.0f;
    float     pitch = 0.0f;
};

constexpr float PLAYER_MOVE_SPEED = 8.0f;

}