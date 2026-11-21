#pragma once

#include <optional>

#include "Engine/ecs/EntityStore.h"

#include "game/core/ecs/EntityCommon.h"
#include "game/server/Components.h"

namespace game::server {

// ============================================================
// 服务端实体的形状：共享部分 + serverPlayer。
// 没有 renderable / chunkRender / chunkCoord / chunkData —— 不是"注册了
// 但不使用"，而是根本不存在这个字段，谁也写不出来。
// ============================================================
struct ServerEntity : game::core::ecs::EntityCommon {
    std::optional<ServerPlayerComponent> serverPlayer;

    // 同 ClientEntity：遮蔽是 EntityStore 契约要求的，不是笔误。
    void ResetComponents() {  // NOLINT(bugprone-derived-method-shadowing-base-method)
        game::core::ecs::EntityCommon::ResetComponents();
        serverPlayer.reset();
    }

    [[nodiscard]] bool IsPristine() const {  // NOLINT(bugprone-derived-method-shadowing-base-method)
        return game::core::ecs::EntityCommon::IsPristine() && !serverPlayer.has_value();
    }
};

using ServerEntityManager = Eng::ecs::EntityStore<ServerEntity>;

}  // namespace game::server