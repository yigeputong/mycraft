#include "game/core/ecs/PhysicsSystem.h"

#include <cmath>

#include "game/core/ecs/EntityManager.h"

namespace game::core::ecs {

void PhysicsSystem::Update(EntityManager& world, const SystemContext& ctx) {
    constexpr float GRAVITY = -24.0f;
    constexpr float MAX_FALL_SPEED = -50.0f;
    constexpr float EPSILON = 0.001f;
    constexpr float JUMP_VELOCITY = 7.0f;
    constexpr float JUMP_CLEARANCE = 1.05f;

    for (auto& e : world.GetEntities()) {
        if (!e.active || !e.velocity || !e.aabb)
            continue;
        // 网络同步实体完全由服务端下发位置驱动，跳过本地物理
        if (e.netInterp)
            continue;

        auto& vel = *e.velocity;
        auto& aabb = *e.aabb;
        const float halfW = aabb.halfExtents.x;
        const float halfH = aabb.halfExtents.y;
        const float height = halfH * 2.0f;
        const float halfD = aabb.halfExtents.z;
        const float pivotOffsetY = aabb.positionIsFeet ? 0.0f : -halfH;

        auto checkCollision = [&](const glm::vec3& testPos) -> bool {
            const float footY = testPos.y + pivotOffsetY;
            int minX = static_cast<int>(std::floor(testPos.x - halfW + EPSILON));
            int maxX = static_cast<int>(std::floor(testPos.x + halfW - EPSILON));
            int minY = static_cast<int>(std::floor(footY + EPSILON));
            int maxY = static_cast<int>(std::floor(footY + height - EPSILON));
            int minZ = static_cast<int>(std::floor(testPos.z - halfD + EPSILON));
            int maxZ = static_cast<int>(std::floor(testPos.z + halfD - EPSILON));
            for (int x = minX; x <= maxX; ++x)
                for (int y = minY; y <= maxY; ++y)
                    for (int z = minZ; z <= maxZ; ++z)
                        if (ctx.isSolidAt(x, y, z))
                            return true;
            return false;
        };

        glm::vec3 displacement(0.0f);
        if (vel.speed > 0.01f) {
            displacement.x = vel.moveDir.x * vel.speed * ctx.dt;
            displacement.z = vel.moveDir.z * vel.speed * ctx.dt;
        }
        vel.yVelocity += GRAVITY * ctx.dt;
        if (vel.yVelocity < MAX_FALL_SPEED)
            vel.yVelocity = MAX_FALL_SPEED;
        displacement.y = vel.yVelocity * ctx.dt;

        glm::vec3 currentPos = e.transform.position;
        glm::vec3 nextPos = currentPos;
        bool hitX = false, hitZ = false;

        // 水平移动
        nextPos.x += displacement.x;
        if (checkCollision(nextPos)) {
            nextPos.x = currentPos.x;
            hitX = true;
        }
        nextPos.z += displacement.z;
        if (checkCollision(nextPos)) {
            nextPos.z = currentPos.z;
            hitZ = true;
        }

        // 撞墙跳跃（仅限脚底枢轴）
        if ((hitX || hitZ) && vel.onGround && aabb.positionIsFeet) {
            glm::vec3 jumpTest = nextPos;
            jumpTest.y += JUMP_CLEARANCE;
            if (!checkCollision(jumpTest)) {
                vel.yVelocity = JUMP_VELOCITY;
                vel.onGround = false;
                nextPos.x = currentPos.x + displacement.x;
                nextPos.z = currentPos.z + displacement.z;
            }
        }

        // 垂直移动
        vel.onGround = false;
        nextPos.y += displacement.y;
        if (checkCollision(nextPos)) {
            if (displacement.y < 0.0f) {
                vel.onGround = true;
                nextPos.y = std::ceil(nextPos.y + pivotOffsetY - EPSILON) - pivotOffsetY;
                vel.yVelocity = 0.0f;
            } else if (displacement.y > 0.0f) {
                nextPos.y = currentPos.y;
                vel.yVelocity = 0.0f;
            }
        }

        // 卡死检测
        if (vel.speed > 0.01f) {
            float movedDistSq = (nextPos.x - currentPos.x) * (nextPos.x - currentPos.x) +
                                (nextPos.z - currentPos.z) * (nextPos.z - currentPos.z);
            if (movedDistSq < 1e-6f)
                vel.speed = 0.0f;
        }
        e.transform.position = nextPos;

        // 顺便在这里推进动画时间（原 UpdateAnimation 逻辑）
        if (e.renderable) {
            if (vel.speed > 0.01f)
                e.renderable->animTime += ctx.dt;
            else
                e.renderable->animTime = 0.0f;
        }
    }
}

}  // namespace game::core::ecs