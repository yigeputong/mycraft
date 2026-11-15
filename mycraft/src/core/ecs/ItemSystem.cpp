#include "game/core/ecs/ItemSystem.h"

#include "game/core/Physics.h"  // 需要 PLAYER_EYE
#include "game/core/ecs/EntityManager.h"

namespace game::core::ecs {

void ItemSystem::Update(EntityManager& world, const SystemContext& ctx) {
    constexpr float PICKUP_DURATION = 0.2f;
    constexpr float PICKUP_TARGET_EYE_RATIO = 0.5f;

    for (auto& e : world.GetEntities()) {
        if (!e.active || !e.item)
            continue;
        auto& item = *e.item;

        if (item.pickupDelay > 0.0f)
            item.pickupDelay -= ctx.dt;

        // 1. 网络位置插值
        if (e.netInterp && e.netInterp->interpT < 1.0f) {
            e.netInterp->interpT += ctx.dt * 20.0f;
            if (e.netInterp->interpT > 1.0f)
                e.netInterp->interpT = 1.0f;
            e.transform.position = glm::mix(e.netInterp->prevPos, e.netInterp->targetPos, e.netInterp->interpT);
        }

        // 2. 拾取吸附动画
        if (item.isPickingUp) {
            item.pickupT += ctx.dt / PICKUP_DURATION;
            if (item.pickupT >= 1.0f) {
                world.DestroyEntity(e.id);
                continue;
            }
            float t = item.pickupT * item.pickupT;
            glm::vec3 target = ctx.playerPos + glm::vec3(0.0f, PLAYER_EYE * PICKUP_TARGET_EYE_RATIO, 0.0f);
            e.transform.position = glm::mix(item.pickupStart, target, t);
        } else {
            // 3. 浮动动画
            item.bobTime += ctx.dt;
        }
    }
}

}  // namespace game::core::ecs