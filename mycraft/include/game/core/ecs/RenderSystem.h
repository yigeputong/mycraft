#pragma once
#include "game/core/ecs/Components.h"
#include "game/core/ecs/System.h"

namespace game::core::ecs {

class RenderSystem : public ISystem {
public:
    void Render(EntityManager& world, const RenderContext& ctx) override;

private:
    void RenderNode(client::ModelNode* node, const glm::mat4& parentTransform, const RenderableComponent& rc,
                    Eng::client::SceneRenderer* renderer, const Eng::service::ShaderPtr& entityShader);
    void ApplyAnimationToNodes(client::ModelNode* node, const client::AnimationClip& clip, float animTime);
};

}  // namespace game::core::ecs