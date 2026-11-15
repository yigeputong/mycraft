#pragma once

#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

#include "Engine/client/SceneRenderer.h"
#include "Engine/service/IRenderService.h"

#include "game/core/ecs/Components.h"
#include "game/core/ecs/Entity.h"

namespace game::core::ecs {

struct Entity {
    EntityId id = INVALID_ENTITY;
    bool active = true;

    TransformComponent transform;

    std::optional<VelocityComponent> velocity;
    std::optional<AABBComponent> aabb;
    std::optional<PigAIComponent> pigAI;
    std::optional<ItemComponent> item;
    std::optional<NetInterpolationComponent> netInterp;
    std::optional<PlayerComponent> player;
    std::optional<RenderableComponent> renderable;
};

class EntityManager {
public:
    EntityManager() = default;

    EntityId CreateEntity();
    void DestroyEntity(EntityId id);
    Entity* GetEntity(EntityId id);
    void Clear();

    void Update(float dt, const std::function<bool(int, int, int)>& isSolidAt, const glm::vec3& playerPos);

    void Render(const glm::mat4& viewProj, Eng::client::SceneRenderer* renderer,
                const Eng::service::ShaderPtr& entityShader, const Eng::service::TexturePtr& atlasTexture,
                const std::function<const Eng::client::RenderMesh&(BlockId)>& getItemMeshFunc);

private:
    std::vector<Entity> m_entities;
    std::vector<uint32_t> m_freeIndices;
    std::vector<uint32_t> m_generations;

    void UpdatePigAI(float dt, const std::function<bool(int, int, int)>& isSolidAt);
    void UpdateItems(float dt, const glm::vec3& playerPos);
    void UpdateMovement(float dt, const std::function<bool(int, int, int)>& isSolidAt);
    void UpdateAnimation(float dt);

    void ApplyAnimationToNodes(client::ModelNode* node, const client::AnimationClip& clip, float animTime);

    // ★ 修复：rc 参数类型改为 ecs 命名空间内的 RenderableComponent（即本命名空间）
    void RenderNode(client::ModelNode* node, const glm::mat4& parentTransform, const RenderableComponent& rc,
                    Eng::client::SceneRenderer* renderer, const Eng::service::ShaderPtr& entityShader);
};

}  // namespace game::core::ecs