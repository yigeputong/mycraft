#include "game/core/ecs/EntityManager.h"

#include <cmath>

#include "game/core/Physics.h"

namespace game::core::ecs {

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

static bool FindValidTarget(glm::vec3 currentPos, uint32_t& rngState,
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
    e.item.reset();
    e.netInterp.reset();
    e.player.reset();
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
        m_generations[index]++;
        m_freeIndices.push_back(index);
    }
}

Entity* EntityManager::GetEntity(EntityId id) {
    uint32_t index = GetEntityIndex(id);
    uint32_t generation = GetEntityGeneration(id);
    if (index >= m_entities.size())
        return nullptr;
    auto& e = m_entities[index];
    if (e.active && m_generations[index] == generation)
        return &e;
    return nullptr;
}

void EntityManager::Clear() {
    m_entities.clear();
    m_freeIndices.clear();
    m_generations.clear();
}

void EntityManager::Update(float dt, const std::function<bool(int, int, int)>& isSolidAt, const glm::vec3& playerPos) {
    UpdatePigAI(dt, isSolidAt);
    UpdateItems(dt, playerPos);
    UpdateMovement(dt, isSolidAt);
    UpdateAnimation(dt);
}

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
                if (FindValidTarget(e.transform.position, ai.rngState, isSolidAt, target)) {
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
            ai.walkTimer -= dt;
            if (distance < 0.5f || ai.walkTimer <= 0.0f || ai.stuckTimer > 0.5f) {
                ai.state = PigAIComponent::State::Idle;
                ai.idleTimer = 1.0f + RandomFloat(ai.rngState, 3.0f);
                e.velocity->speed = 0.0f;
                continue;
            }
            if (e.velocity->speed < 0.1f)
                ai.stuckTimer += dt;
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
            float maxTurn = 360.0f * dt;
            if (std::abs(yawDiff) > maxTurn)
                e.transform.yaw += (yawDiff > 0 ? maxTurn : -maxTurn);
            else
                e.transform.yaw = targetYaw;
        }
    }
}

void EntityManager::UpdateItems(float dt, const glm::vec3& playerPos) {
    constexpr float PICKUP_DURATION = 0.2f;
    constexpr float PICKUP_TARGET_EYE_RATIO = 0.5f;  // 飞向眼睛下方

    for (auto& e : m_entities) {
        if (!e.active || !e.item)
            continue;
        auto& item = *e.item;

        if (item.pickupDelay > 0.0f)
            item.pickupDelay -= dt;

        // 网络位置插值
        if (e.netInterp && e.netInterp->interpT < 1.0f) {
            e.netInterp->interpT += dt * 20.0f;
            if (e.netInterp->interpT > 1.0f)
                e.netInterp->interpT = 1.0f;
            e.transform.position = glm::mix(e.netInterp->prevPos, e.netInterp->targetPos, e.netInterp->interpT);
        }

        // 拾取吸附动画
        if (item.isPickingUp) {
            item.pickupT += dt / PICKUP_DURATION;
            if (item.pickupT >= 1.0f) {
                DestroyEntity(e.id);
                continue;
            }
            float t = item.pickupT * item.pickupT;
            glm::vec3 target = playerPos + glm::vec3(0.0f, PLAYER_EYE * PICKUP_TARGET_EYE_RATIO, 0.0f);
            e.transform.position = glm::mix(item.pickupStart, target, t);
        } else {
            item.bobTime += dt;
        }
    }
}

void EntityManager::UpdateMovement(float dt, const std::function<bool(int, int, int)>& isSolidAt) {
    constexpr float GRAVITY = -24.0f;
    constexpr float MAX_FALL_SPEED = -50.0f;
    constexpr float EPSILON = 0.001f;
    constexpr float JUMP_VELOCITY = 7.0f;
    constexpr float JUMP_CLEARANCE = 1.05f;

    for (auto& e : m_entities) {
        if (!e.active || !e.velocity || !e.aabb)
            continue;
        // 网络同步实体完全由服务端下发位置驱动，跳过本地物理，避免插值被覆盖
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

        // 撞墙跳跃仅对脚底枢轴实体生效，掉落物不跳
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

        if (vel.speed > 0.01f) {
            float movedDistSq = (nextPos.x - currentPos.x) * (nextPos.x - currentPos.x) +
                                (nextPos.z - currentPos.z) * (nextPos.z - currentPos.z);
            if (movedDistSq < 1e-6f)
                vel.speed = 0.0f;
        }
        e.transform.position = nextPos;
    }
}

void EntityManager::UpdateAnimation(float dt) {
    for (auto& e : m_entities) {
        if (!e.active || !e.renderable || !e.velocity)
            continue;
        if (e.velocity->speed > 0.01f)
            e.renderable->animTime += dt;
        else
            e.renderable->animTime = 0.0f;
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
    for (const auto& child : node->children)
        ApplyAnimationToNodes(child.get(), clip, animTime);
}

void EntityManager::Render(const glm::mat4& viewProj, Eng::client::SceneRenderer* renderer,
                           const Eng::service::ShaderPtr& entityShader, const Eng::service::TexturePtr& atlasTexture,
                           const std::function<const Eng::client::RenderMesh&(BlockId)>& getItemMeshFunc) {
    [[maybe_unused]] const glm::mat4& vp = viewProj;  // 预留给未来视锥剔除

    std::unordered_map<BlockId, std::vector<Eng::service::rhi::PerObjectData>> itemInstances;

    for (auto& e : m_entities) {
        if (!e.active || !e.renderable)
            continue;
        auto& rc = *e.renderable;

        // 渲染猪（骨骼模型）
        if (rc.model) {
            glm::mat4 worldMat = glm::translate(glm::mat4(1.0f), e.transform.position);
            worldMat = glm::rotate(worldMat, glm::radians(e.transform.yaw), glm::vec3(0, 1, 0));
            auto it = rc.model->clips.find(rc.currentClip);
            if (it != rc.model->clips.end()) {
                const auto& clip = it->second;
                float loopTime = (clip.duration > 0.0f) ? std::fmod(rc.animTime, clip.duration) : 0.0f;
                ApplyAnimationToNodes(rc.model->rootNode.get(), clip, loopTime);
            }
            RenderNode(rc.model->rootNode.get(), worldMat, rc, renderer, entityShader);
        }

        // 收集掉落物实例化数据
        if (e.item && rc.blockType != AIR_ID) {
            // 用实体索引做相位，避免 uint64 高位参与 sin 导致精度爆炸
            const float entityPhase = static_cast<float>(GetEntityIndex(e.id)) * 0.5f;

            Eng::service::rhi::PerObjectData data;
            data.model = glm::translate(glm::mat4(1.0f), e.transform.position);
            data.model = glm::rotate(data.model, e.item->bobTime * 2.0f, glm::vec3(0, 1, 0));
            data.model = glm::translate(
                data.model, glm::vec3(0.0f, std::sin(e.item->bobTime * 3.0f + entityPhase) * 0.1f + 0.1f, 0.0f));

            if (e.item->isPickingUp) {
                float scale = 1.0f - e.item->pickupT;
                data.model = glm::scale(data.model, glm::vec3(0.25f * scale));
            } else {
                data.model = glm::scale(data.model, glm::vec3(0.25f));
            }

            itemInstances[rc.blockType].push_back(data);
        }
    }

    // 提交掉落物批量渲染，传 atlas 纹理
    for (const auto& [type, perObjects] : itemInstances) {
        const auto& mesh = getItemMeshFunc(type);
        if (mesh.vbo) {
            renderer->SubmitInstancedMesh(mesh, entityShader, atlasTexture, perObjects);
        }
    }
}

void EntityManager::RenderNode(client::ModelNode* node, const glm::mat4& parentTransform, const RenderableComponent& rc,
                               Eng::client::SceneRenderer* renderer, const Eng::service::ShaderPtr& entityShader) {
    glm::mat4 localMat = node->baseTransform * node->currentAnimTransform;
    glm::mat4 worldMat = parentTransform * localMat;
    for (uint32_t idx : node->meshIndices) {
        renderer->SubmitMesh(rc.model->meshes[idx], entityShader, rc.model->texture, {.model = worldMat});
    }
    for (const auto& child : node->children)
        RenderNode(child.get(), worldMat, rc, renderer, entityShader);
}

}  // namespace game::core::ecs