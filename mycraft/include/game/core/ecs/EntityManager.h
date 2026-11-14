#pragma once

#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

#include "Engine/client/SceneRenderer.h"
#include "Engine/service/IRenderService.h"  // 需要 ShaderPtr

#include "game/core/ecs/Components.h"
#include "game/core/ecs/Entity.h"

namespace game::core::ecs {

struct Entity {
    EntityId id = INVALID_ENTITY;
    bool active = true;

    // 必备组件
    TransformComponent transform;

    // 可选组件
    std::optional<VelocityComponent> velocity;
    std::optional<AABBComponent> aabb;
    std::optional<PigAIComponent> pigAI;
    std::optional<client::RenderableComponent> renderable;
};

class EntityManager {
public:
    EntityManager() = default;

    // 生命周期
    EntityId CreateEntity();
    void DestroyEntity(EntityId id);
    Entity* GetEntity(EntityId id);

    // 流水线调度
    void Update(float dt, const std::function<bool(int, int, int)>& isSolidAt);

    // ★ 渲染签名：补上 entityShader 参数
    void Render(const glm::mat4& viewProj, Eng::client::SceneRenderer* renderer, Eng::service::ShaderPtr entityShader);

private:
    std::vector<Entity> m_entities;
    std::vector<uint32_t> m_freeIndices;
    std::vector<uint32_t> m_generations;  // 记录每个 Index 的历史版本号

    // ====== 核心系统 ======
    void UpdatePigAI(float dt, const std::function<bool(int, int, int)>& isSolidAt);
    void UpdateMovement(float dt, const std::function<bool(int, int, int)>& isSolidAt);
    void UpdateAnimation(float dt);

    // ★ 补全漏掉的动画计算和递归渲染
    void ApplyAnimationToNodes(client::ModelNode* node, const client::AnimationClip& clip, float animTime);
    void RenderNode(client::ModelNode* node, const glm::mat4& parentTransform, const client::RenderableComponent& rc,
                    Eng::client::SceneRenderer* renderer, Eng::service::ShaderPtr entityShader);
};

}  // namespace game::core::ecs