#pragma once

#include <cstdint>

namespace Eng::ecs {

// ============================================================
// 实体句柄：64 位打包 ID
//     [高 32 位 Generation 版本号] [低 32 位 Index 槽位索引]
//
// Generation 在每次销毁时自增，用来把「已经失效的旧 ID」和
// 「复用同一槽位的新实体」彻底区分开：旧 ID 查不到新实体。
//
// INVALID_ENTITY 恒为 0。因此槽位的初始 generation 必须从 1 开始
// （见 EntityStore::CreateEntity），否则 index 0 的首个实体会拿到
// ID == 0，与「空句柄」撞值。
// ============================================================

using EntityId = uint64_t;

constexpr EntityId INVALID_ENTITY = 0;

[[nodiscard]] constexpr uint32_t GetEntityIndex(EntityId id) noexcept {
    return static_cast<uint32_t>(id & 0xFFFFFFFFull);
}

[[nodiscard]] constexpr uint32_t GetEntityGeneration(EntityId id) noexcept {
    // 位移量写 32u：字面量 32 是 int，会被 bugprone-signed-bitwise 判成「有符号操作数」
    return static_cast<uint32_t>(id >> 32u);
}

[[nodiscard]] constexpr EntityId MakeEntityId(uint32_t index, uint32_t generation) noexcept {
    return (static_cast<uint64_t>(generation) << 32u) | static_cast<uint64_t>(index);
}

}  // namespace Eng::ecs