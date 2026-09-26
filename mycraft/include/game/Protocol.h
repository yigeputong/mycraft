#pragma once
#include <cstdint>

namespace game::net {

enum class MessageType : uint8_t {
    // 客户端 → 服务端
    PlayerInput     = 0x10,
    ChatMessage     = 0x11,
    BlockAction     = 0x12,

    // 服务端 → 客户端
    WorldState      = 0x20,
    ChunkData       = 0x21,
    PlayerJoined    = 0x22,
    PlayerLeft      = 0x23,
};

struct PlayerInput {
    float moveX, moveZ;
    float yaw, pitch;
    bool  jump;
};

}