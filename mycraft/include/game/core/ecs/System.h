#pragma once
#include <functional>
#include <glm/glm.hpp>

#include "Engine/client/SceneRenderer.h"
#include "Engine/service/IRenderService.h"

#include "game/core/BlockRegistry.h"

namespace game::core::ecs {

class EntityManager;

// 系统上下文：传递外部环境依赖（不污染实体本身）
struct SystemContext {
    float dt = 0.0f;
    glm::vec3 playerPos{0.0f};
    std::function<bool(int, int, int)> isSolidAt;
};

// 渲染专用上下文，避免 Update 阶段携带无用图形指针
struct RenderContext {
    glm::mat4 viewProj;
    Eng::client::SceneRenderer* renderer = nullptr;
    Eng::service::ShaderPtr entityShader;
    Eng::service::TexturePtr atlasTexture;
    std::function<const Eng::client::RenderMesh&(BlockId)> getItemMeshFunc;
};

// 系统基类
class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void Update(EntityManager& world, const SystemContext& ctx) {}
    virtual void Render(EntityManager& world, const RenderContext& ctx) {}
};

}  // namespace game::core::ecs