#pragma once
#include <glm/glm.hpp>
#include <optional>

#include "Engine/ecs/EntityStore.h"

#include "game/core/ecs/ComponentsShared.h"

namespace game::core::ecs {

// 引擎句柄在业务命名空间下的别名（沿用 M1 的约定，业务代码不用改写法）。
using Eng::ecs::EntityId;
using Eng::ecs::GetEntityGeneration;
using Eng::ecs::GetEntityIndex;
using Eng::ecs::INVALID_ENTITY;
using Eng::ecs::MakeEntityId;

// ============================================================
// 两端实体的**公共部分**：客户端与服务端各有自己的实体类型，
// 但都继承这里，所以共享组件的定义永远只有一份。
// ★ 派生类必须：ResetComponents() 先调 EntityCommon::ResetComponents()，
//   IsPristine() 先调 EntityCommon::IsPristine()，再补自己的字段。
//   漏了会回到"幽灵区块"那类 bug。EntityStore 的 assert 会当场抓住。
// ============================================================
struct EntityCommon {
    EntityId id = INVALID_ENTITY;
    bool active = true;

    TransformComponent transform;

    std::optional<VelocityComponent> velocity;
    std::optional<AABBComponent> aabb;
    std::optional<PigAIComponent> pigAI;
    std::optional<ItemComponent> item;
    std::optional<NetInterpolationComponent> netInterp;

    void ResetComponents() {
        transform = TransformComponent{};
        velocity.reset();
        aabb.reset();
        pigAI.reset();
        item.reset();
        netInterp.reset();
    }

    [[nodiscard]] bool IsPristine() const {
        return !velocity.has_value() && !aabb.has_value() && !pigAI.has_value() && !item.has_value() &&
               !netInterp.has_value();
    }
};

}  // namespace game::core::ecs