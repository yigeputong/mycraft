#pragma once

#include <functional>
#include <glm/glm.hpp>

#include "Engine/client/SceneRenderer.h"
#include "Engine/service/IRenderService.h"

#include "game/client/ClientEntity.h"
#include "game/core/BlockRegistry.h"

namespace game::client {

// ============================================================
// 渲染专用上下文：只在渲染阶段构造。相机、着色器、图元批处理
// 这些指针不该在 Update 阶段跟着 tick 上下文一起流动。
// ============================================================
struct RenderContext {
    glm::mat4 viewProj{1.0f};
    Eng::client::SceneRenderer* renderer = nullptr;
    Eng::service::ShaderPtr entityShader;
    Eng::service::TexturePtr atlasTexture;
    std::function<const Eng::client::RenderMesh&(game::BlockId)> getItemMeshFunc;
};

// ============================================================
// 渲染系统：**不是** ISystemT 的实现者。
// 它不参与 tick 调度，由渲染阶段直接调用；没有 Update 方法，
// 也不该有 —— 渲染永远不该被 tick 循环驱动。
// ============================================================
class RenderSystem {
public:
    void Render(ClientEntityManager& world, const RenderContext& ctx);

private:
    void RenderNode(ModelNode* node, const glm::mat4& parentTransform, const RenderableComponent& rc,
                    Eng::client::SceneRenderer* renderer, const Eng::service::ShaderPtr& entityShader);

    void ApplyAnimationToNodes(ModelNode* node, const AnimationClip& clip, float animTime);
};

}  // namespace game::client