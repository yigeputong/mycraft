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