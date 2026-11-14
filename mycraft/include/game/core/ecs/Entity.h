#pragma once
#include <cstdint>

namespace game::core::ecs {

// 采用 64 位 ID：[高 32 位: Generation 版本号] [低 32 位: Index 索引]
using EntityId = uint64_t;
constexpr EntityId INVALID_ENTITY = 0;

inline uint32_t GetEntityIndex(EntityId id) {
    return static_cast<uint32_t>(id & 0xFFFFFFFF);
}

inline uint32_t GetEntityGeneration(EntityId id) {
    return static_cast<uint32_t>(id >> 32);
}

inline EntityId CreateEntityId(uint32_t index, uint32_t generation) {
    return (static_cast<uint64_t>(generation) << 32) | index;
}

}  // namespace game::core::ecs