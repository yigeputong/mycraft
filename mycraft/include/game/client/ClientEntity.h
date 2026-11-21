#pragma once

#include <optional>

#include "Engine/ecs/EntityStore.h"

#include "game/client/Components.h"
#include "game/core/ecs/EntityCommon.h"
#include "game/core/ecs/System.h"

namespace game::client {

// ============================================================
// 客户端实体的形状：共享部分 + 玩家 + 渲染 + 区块。
// 服务端对应的是 game/server/ServerEntity.h —— 那份没有 player /
// renderable / chunkCoord / chunkData，所以物理上不可能 include 到渲染后端。
// ============================================================
struct ClientEntity : game::core::ecs::EntityCommon {
    std::optional<PlayerComponent> player;
    std::optional<RenderableComponent> renderable;
    std::optional<ChunkCoordComponent> chunkCoord;
    std::optional<ChunkComponent> chunkData;

    // 故意遮蔽基类同名方法：Eng::ecs::EntityStore 的契约就要求这两个名字，
    // 改名等于让复用槽位时的清零链断掉。NOLINT 说明这是设计而非疏忽。
    void ResetComponents() {  // NOLINT(bugprone-derived-method-shadowing-base-method)
        game::core::ecs::EntityCommon::ResetComponents();
        player.reset();
        renderable.reset();
        chunkCoord.reset();
        chunkData.reset();
    }

    [[nodiscard]] bool IsPristine() const {  // NOLINT(bugprone-derived-method-shadowing-base-method)
        return game::core::ecs::EntityCommon::IsPristine() && !player.has_value() && !renderable.has_value() &&
               !chunkCoord.has_value() && !chunkData.has_value();
    }
};

using ClientEntityManager = Eng::ecs::EntityStore<ClientEntity>;
using ClientSystemScheduler = game::core::ecs::SystemScheduler<ClientEntityManager>;

}  // namespace game::client