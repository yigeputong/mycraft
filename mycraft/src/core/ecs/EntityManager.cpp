#include "game/core/ecs/EntityManager.h"

#include <cmath>

#include "Engine/tool/Log.h"

namespace game::core::ecs {

// 手搓的 xorshift32，保证跨平台绝对一致
static uint32_t XorShift32(uint32_t& state) {
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

static float RandomFloat(uint32_t& state, float range) {
    float normalized = static_cast<float>(XorShift32(state)) / 4294967296.0f;
    return (normalized * 2.0f - 1.0f) * range;
}

EntityId EntityManager::CreateEntity() {
    uint32_t index;
    uint32_t generation = 0;

    if (!m_freeIndices.empty()) {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
        generation = m_generations[index];
    } else {
        index = static_cast<uint32_t>(m_entities.size());
        m_entities.emplace_back();
        m_generations.push_back(0);
    }

    EntityId id = CreateEntityId(index, generation);
    auto& e = m_entities[index];
    e.id = id;
    e.active = true;

    e.transform = {};
    e.velocity.reset();
    e.aabb.reset();
    e.pigAI.reset();
    e.renderable.reset();

    return id;
}

void EntityManager::DestroyEntity(EntityId id) {
    uint32_t index = GetEntityIndex(id);
    if (index >= m_entities.size())
        return;

    auto& e = m_entities[index];
    if (e.id == id && e.active) {
        e.active = false;
        m_generations[index]++;  // 版本号递增
        m_freeIndices.push_back(index);
    }
}

Entity* EntityManager::GetEntity(EntityId id) {
    uint32_t index = GetEntityIndex(id);
    uint32_t generation = GetEntityGeneration(id);
    if (index >= m_entities.size())
        return nullptr;

    auto& e = m_entities[index];
    if (e.active && m_generations[index] == generation) {
        return &e;
    }
    return nullptr;
}

// =========================================================
// 1. 新增辅助函数：模仿 MC 的 LandRandomPos 合法选点
// 放在 EntityManager::CreateEntity() 之前即可
// =========================================================
static bool FindValidTarget(glm::vec3 currentPos, uint32_t& rngState,
                            const std::function<bool(int, int, int)>& isSolidAt, glm::vec3& outTarget) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        float dx = RandomFloat(rngState, 10.0f);
        float dz = RandomFloat(rngState, 10.0f);
        glm::vec3 testPos = currentPos + glm::vec3(dx, 0.0f, dz);

        int bx = static_cast<int>(std::floor(testPos.x));
        int by = static_cast<int>(std::floor(testPos.y));
        int bz = static_cast<int>(std::floor(testPos.z));

        // 规则：脚下是实心，中间是空气，头顶是空气
        if (isSolidAt(bx, by - 1, bz) && !isSolidAt(bx, by, bz) && !isSolidAt(bx, by + 1, bz)) {
            outTarget = testPos;
            return true;
        }
    }
    return false;
}

// =========================================================
// 2. 替换 EntityManager::Update
// =========================================================
void EntityManager::Update(float dt, const std::function<bool(int, int, int)>& isSolidAt) {
    UpdatePigAI(dt, isSolidAt);  // ★ 注意这里传了回调
    UpdateMovement(dt, isSolidAt);
    UpdateAnimation(dt);
}

// =========================================================
// 3. 替换 EntityManager::UpdatePigAI
// =========================================================
void EntityManager::UpdatePigAI(float dt, const std::function<bool(int, int, int)>& isSolidAt) {
    for (auto& e : m_entities) {
        if (!e.active || !e.pigAI || !e.velocity)
            continue;
        auto& ai = *e.pigAI;

        if (ai.state == PigAIComponent::State::Idle) {
            e.velocity->speed = 0.0f;
            ai.idleTimer -= dt;

            if (ai.idleTimer <= 0.0f) {
                glm::vec3 target;
                // 尝试找合法目标点
                if (FindValidTarget(e.transform.position, ai.rngState, isSolidAt, target)) {
                    ai.targetPos = target;
                    ai.state = PigAIComponent::State::Walk;
                    ai.walkTimer = 8.0f;  // 最多走 8 秒
                    ai.stuckTimer = 0.0f;
                } else {
                    ai.idleTimer = 1.0f;  // 没找到合法点，呆 1 秒再试
                }
            }
        } else if (ai.state == PigAIComponent::State::Walk) {
            glm::vec3 dir = ai.targetPos - e.transform.position;
            dir.y = 0.0f;
            float distance = glm::length(dir);

            ai.walkTimer -= dt;

            // 结束条件：到达 / 超时 / 卡死
            if (distance < 0.5f || ai.walkTimer <= 0.0f || ai.stuckTimer > 0.5f) {
                ai.state = PigAIComponent::State::Idle;
                ai.idleTimer = 1.0f + RandomFloat(ai.rngState, 3.0f);
                e.velocity->speed = 0.0f;
                continue;
            }

            // 卡死检测累加
            if (e.velocity->speed < 0.1f) {
                ai.stuckTimer += dt;
            } else {
                ai.stuckTimer = 0.0f;
            }

            // 执行移动
            dir = glm::normalize(dir);
            e.velocity->moveDir = dir;
            e.velocity->speed = ai.baseSpeed;

            // 平滑转向
            float targetYaw = glm::degrees(std::atan2(dir.x, dir.z));
            float yawDiff = targetYaw - e.transform.yaw;
            while (yawDiff > 180.0f)
                yawDiff -= 360.0f;
            while (yawDiff < -180.0f)
                yawDiff += 360.0f;

            float maxTurn = 360.0f * dt;
            if (std::abs(yawDiff) > maxTurn) {
                e.transform.yaw += (yawDiff > 0 ? maxTurn : -maxTurn);
            } else {
                e.transform.yaw = targetYaw;
            }
        }
    }
}

// =========================================================
// 4. 替换 EntityManager::UpdateMovement
// （把之前带重力、自动跳跃、Epsilon的版本换上，并在末尾加上撞墙信号）
// =========================================================
void EntityManager::UpdateMovement(float dt, const std::function<bool(int, int, int)>& isSolidAt) {
    constexpr float GRAVITY = -24.0f;
    constexpr float MAX_FALL_SPEED = -50.0f;
    constexpr float EPSILON = 0.001f;

    for (auto& e : m_entities) {
        if (!e.active || !e.velocity || !e.aabb)
            continue;

        auto& vel = *e.velocity;
        auto& aabb = *e.aabb;

        float halfW = aabb.halfExtents.x;
        float height = aabb.halfExtents.y * 2.0f;
        float halfD = aabb.halfExtents.z;

        auto checkCollision = [&](const glm::vec3& testPos) -> bool {
            int minX = static_cast<int>(std::floor(testPos.x - halfW + EPSILON));
            int maxX = static_cast<int>(std::floor(testPos.x + halfW - EPSILON));
            int minY = static_cast<int>(std::floor(testPos.y + EPSILON));
            int maxY = static_cast<int>(std::floor(testPos.y + height - EPSILON));
            int minZ = static_cast<int>(std::floor(testPos.z - halfD + EPSILON));
            int maxZ = static_cast<int>(std::floor(testPos.z + halfD - EPSILON));

            for (int x = minX; x <= maxX; ++x)
                for (int y = minY; y <= maxY; ++y)
                    for (int z = minZ; z <= maxZ; ++z)
                        if (isSolidAt(x, y, z))
                            return true;
            return false;
        };

        glm::vec3 displacement(0.0f);
        if (vel.speed > 0.01f) {
            displacement.x = vel.moveDir.x * vel.speed * dt;
            displacement.z = vel.moveDir.z * vel.speed * dt;
        }

        vel.yVelocity += GRAVITY * dt;
        if (vel.yVelocity < MAX_FALL_SPEED)
            vel.yVelocity = MAX_FALL_SPEED;
        displacement.y = vel.yVelocity * dt;

        glm::vec3 currentPos = e.transform.position;
        glm::vec3 nextPos = currentPos;
        bool hitX = false, hitZ = false;

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

        if ((hitX || hitZ) && vel.onGround) {
            glm::vec3 jumpTest = nextPos;
            jumpTest.y += 1.05f;
            if (!checkCollision(jumpTest)) {
                vel.yVelocity = 7.0f;
                vel.onGround = false;
                nextPos.x = currentPos.x + displacement.x;
                nextPos.z = currentPos.z + displacement.z;
            }
        }

        vel.onGround = false;
        nextPos.y += displacement.y;
        if (checkCollision(nextPos)) {
            if (displacement.y < 0.0f) {
                vel.onGround = true;
                nextPos.y = std::ceil(nextPos.y - EPSILON);
                vel.yVelocity = 0.0f;
            } else if (displacement.y > 0.0f) {
                nextPos.y = currentPos.y;
                vel.yVelocity = 0.0f;
            }
        }

        // ★ 关键：如果水平被挡住，且没跳上去，速度清零（触发 AI 的 stuckTimer）
        if (vel.speed > 0.01f) {
            float movedDistSq = (nextPos.x - currentPos.x) * (nextPos.x - currentPos.x) +
                                (nextPos.z - currentPos.z) * (nextPos.z - currentPos.z);
            if (movedDistSq < 1e-6f) {
                vel.speed = 0.0f;
            }
        }

        e.transform.position = nextPos;
    }
}

void EntityManager::UpdateAnimation(float dt) {
    for (auto& e : m_entities) {
        if (!e.active || !e.renderable || !e.pigAI)
            continue;
        if (e.pigAI->state == PigAIComponent::State::Walk) {
            e.renderable->animTime += dt;
        } else {
            e.renderable->animTime = 0.0f;
        }
    }
}

void EntityManager::ApplyAnimationToNodes(client::ModelNode* node, const client::AnimationClip& clip, float animTime) {
    node->currentAnimTransform = glm::mat4(1.0f);
    for (const auto& channel : clip.channels) {
        if (channel.nodeName == node->name && channel.keyframes.size() >= 2) {
            size_t idx1 = 0;
            for (size_t i = 0; i < channel.keyframes.size(); ++i) {
                if (channel.keyframes[i].time <= animTime)
                    idx1 = i;
                else
                    break;
            }
            size_t idx2 = (idx1 + 1) % channel.keyframes.size();

            const auto& k1 = channel.keyframes[idx1];
            const auto& k2 = channel.keyframes[idx2];

            float timeDiff = k2.time - k1.time;
            if (timeDiff < 0.0f && clip.duration > 0.0f)
                timeDiff += clip.duration;
            float t = (timeDiff > 0.0f) ? (animTime - k1.time) / timeDiff : 0.0f;
            if (t < 0.0f)
                t += 1.0f;

            glm::quat finalRot = glm::slerp(k1.rotation, k2.rotation, t);
            node->currentAnimTransform = glm::mat4_cast(finalRot);
            break;
        }
    }
    for (const auto& child : node->children) {
        ApplyAnimationToNodes(child.get(), clip, animTime);
    }
}

void EntityManager::Render(const glm::mat4& viewProj, Eng::client::SceneRenderer* renderer,
                           Eng::service::ShaderPtr entityShader) {
    for (auto& e : m_entities) {
        if (!e.active || !e.renderable)
            continue;

        glm::mat4 worldMat = glm::translate(glm::mat4(1.0f), e.transform.position);
        worldMat = glm::rotate(worldMat, glm::radians(e.transform.yaw), glm::vec3(0, 1, 0));

        auto& rc = *e.renderable;
        if (rc.model) {
            auto it = rc.model->clips.find(rc.currentClip);
            if (it != rc.model->clips.end()) {
                const auto& clip = it->second;
                float loopTime = (clip.duration > 0.0f) ? std::fmod(rc.animTime, clip.duration) : 0.0f;
                ApplyAnimationToNodes(rc.model->rootNode.get(), clip, loopTime);
            }
            RenderNode(rc.model->rootNode.get(), worldMat, rc, renderer, entityShader);
        }
    }
}

void EntityManager::RenderNode(client::ModelNode* node, const glm::mat4& parentTransform,
                               const client::RenderableComponent& rc, Eng::client::SceneRenderer* renderer,
                               Eng::service::ShaderPtr entityShader) {
    glm::mat4 localMat = node->baseTransform * node->currentAnimTransform;
    glm::mat4 worldMat = parentTransform * localMat;

    for (uint32_t idx : node->meshIndices) {
        renderer->SubmitMesh(rc.model->meshes[idx], entityShader, rc.model->texture, {.model = worldMat});
    }

    for (const auto& child : node->children) {
        RenderNode(child.get(), worldMat, rc, renderer, entityShader);
    }
}

}  // namespace game::core::ecs