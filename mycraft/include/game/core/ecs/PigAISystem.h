#pragma once

#include <cmath>
#include <functional>

#include "game/core/ecs/EntityCommon.h"
#include "game/core/ecs/System.h"

namespace game::core::ecs {

// 猪的随机数必须是 xorshift32（铁律 5），所以放在 detail 里 inline，
// 不进匿名命名空间，避免每个 TU 一份又各自 unused 警告。
namespace detail {

inline float RandomFloat(uint32_t& state, float range) {
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    float normalized = static_cast<float>(x) / 4294967296.0f;
    return (normalized * 2.0f - 1.0f) * range;
}

inline bool FindValidTarget(glm::vec3 currentPos, uint32_t& rngState,
                            const std::function<bool(int, int, int)>& isSolidAt, glm::vec3& outTarget) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        float dx = RandomFloat(rngState, 10.0f);
        float dz = RandomFloat(rngState, 10.0f);
        glm::vec3 testPos = currentPos + glm::vec3(dx, 0.0f, dz);
        int bx = static_cast<int>(std::floor(testPos.x));
        int by = static_cast<int>(std::floor(testPos.y));
        int bz = static_cast<int>(std::floor(testPos.z));

        if (isSolidAt(bx, by - 1, bz) && !isSolidAt(bx, by, bz) && !isSolidAt(bx, by + 1, bz)) {
            outTarget = testPos;
            return true;
        }
    }
    return false;
}

}  // namespace detail

template <typename WorldT>
class PigAISystem : public ISystem<WorldT> {
public:
    void Update(WorldT& world, const SystemContext& ctx) override {
        for (auto& e : world.GetEntities()) {
            if (!e.active || !e.pigAI || !e.velocity)
                continue;

            auto& ai = *e.pigAI;
            if (ai.state == PigAIComponent::State::Idle) {
                e.velocity->speed = 0.0f;
                ai.idleTimer -= ctx.dt;
                if (ai.idleTimer <= 0.0f) {
                    glm::vec3 target;
                    if (detail::FindValidTarget(e.transform.position, ai.rngState, ctx.isSolidAt, target)) {
                        ai.targetPos = target;
                        ai.state = PigAIComponent::State::Walk;
                        ai.walkTimer = 8.0f;
                        ai.stuckTimer = 0.0f;
                    } else {
                        ai.idleTimer = 1.0f;
                    }
                }
            } else if (ai.state == PigAIComponent::State::Walk) {
                glm::vec3 dir = ai.targetPos - e.transform.position;
                dir.y = 0.0f;
                float distance = glm::length(dir);
                ai.walkTimer -= ctx.dt;

                if (distance < 0.5f || ai.walkTimer <= 0.0f || ai.stuckTimer > 0.5f) {
                    ai.state = PigAIComponent::State::Idle;
                    ai.idleTimer = 1.0f + detail::RandomFloat(ai.rngState, 3.0f);
                    e.velocity->speed = 0.0f;
                    continue;
                }

                // 卡死检测
                if (e.velocity->speed < 0.1f)
                    ai.stuckTimer += ctx.dt;
                else
                    ai.stuckTimer = 0.0f;

                dir = glm::normalize(dir);
                e.velocity->moveDir = dir;
                e.velocity->speed = ai.baseSpeed;

                float targetYaw = glm::degrees(std::atan2(dir.x, dir.z));
                float yawDiff = targetYaw - e.transform.yaw;
                while (yawDiff > 180.0f)
                    yawDiff -= 360.0f;
                while (yawDiff < -180.0f)
                    yawDiff += 360.0f;
                float maxTurn = 360.0f * ctx.dt;
                if (std::abs(yawDiff) > maxTurn)
                    e.transform.yaw += (yawDiff > 0 ? maxTurn : -maxTurn);
                else
                    e.transform.yaw = targetYaw;
            }
        }
    }
};

}  // namespace game::core::ecs