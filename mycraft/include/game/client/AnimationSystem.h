#pragma once

#include "game/client/ClientEntity.h"
#include "game/core/ecs/System.h"

namespace game::client {

// ============================================================
// 表现层动画推进。这段逻辑原本寄生在 PhysicsSystem 里，是服务端
// 被迫携带 RenderableComponent 的唯一原因，现已搬出。
// 语义与原来逐字一致：必须有 velocity 才参与，速度归零则重置动画。
// ============================================================
class AnimationSystem : public game::core::ecs::ISystem<ClientEntityManager> {
public:
    void Update(ClientEntityManager& world, const game::core::ecs::SystemContext& ctx) override {
        for (auto& e : world.GetEntities()) {
            if (!e.active || !e.renderable || !e.velocity)
                continue;
            if (e.velocity->speed > 0.01f)
                e.renderable->animTime += ctx.dt;
            else
                e.renderable->animTime = 0.0f;
        }
    }
};

}  // namespace game::client