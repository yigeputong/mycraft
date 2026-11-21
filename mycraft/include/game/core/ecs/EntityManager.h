#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <vector>

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
    std::optional<ServerPlayerComponent> serverPlayer;
    std::optional<RenderableComponent> renderable;
    std::optional<ChunkCoordComponent> chunkCoord;
    std::optional<ChunkComponent> chunkData;
    std::optional<ChunkRenderComponent> chunkRender;

    // ============================================================
    // 组件清零：实体销毁与槽位复用都必须经过这里。
    // ★ 新增组件时，必须在 ResetComponents() 与 IsPristine() 各补一行。
    //   历史 bug：区块实体被卸载后，复用的新实体继承了 chunkCoord / chunkData /
    //   chunkRender，形成幽灵区块，并让 shared_ptr<Chunk> 长期滞留。
    // ============================================================
    void ResetComponents() {
        transform = TransformComponent{};
        velocity.reset();
        aabb.reset();
        pigAI.reset();
        item.reset();
        netInterp.reset();
        player.reset();
        serverPlayer.reset();
        renderable.reset();
        chunkCoord.reset();
        chunkData.reset();
        chunkRender.reset();
    }

    // Debug 自检：不活跃的实体不应残留任何可选组件（transform 为必选字段，不参与判断）。
    [[nodiscard]] bool IsPristine() const {
        return !velocity.has_value() && !aabb.has_value() && !pigAI.has_value() && !item.has_value() &&
               !netInterp.has_value() && !player.has_value() && !serverPlayer.has_value() &&
               !renderable.has_value() && !chunkCoord.has_value() && !chunkData.has_value() &&
               !chunkRender.has_value();
    }
};

class EntityManager {
public:
    EntityManager() = default;

    EntityId CreateEntity();
    void DestroyEntity(EntityId id);

    Entity* GetEntity(EntityId id) const;
    // 提供安全的实体内存访问（供 System 使用）
    std::vector<Entity>& GetEntities() { return m_entities; }

    void Clear();

private:
    std::vector<Entity> m_entities;
    std::vector<uint32_t> m_freeIndices;
    std::vector<uint32_t> m_generations;
};

}  // namespace game::core::ecs