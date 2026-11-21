#include "game/core/ecs/EntityManager.h"

#include <cassert>
#include <cmath>

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

    auto& e = m_entities[index];

    // 复用槽位前必须保证干净。若断言失败，说明有路径绕过 DestroyEntity 释放了槽位，
    // 或者新增组件忘了在 ResetComponents() 里补一行。
    assert(e.IsPristine() && "Entity slot is not pristine before reuse");

    e.ResetComponents();
    e.id = CreateEntityId(index, generation);
    e.active = true;
    return e.id;
}

void EntityManager::DestroyEntity(EntityId id) {
    uint32_t index = GetEntityIndex(id);
    if (index >= m_entities.size())
        return;
    auto& e = m_entities[index];
    if (e.id == id && e.active) {
        e.active = false;
        // 立即清零：ChunkComponent 持有 shared_ptr<Chunk>（约 320KB）。
        // 若等到槽位被复用时才释放，视距外卸载的区块内存会在 free list 里长期滞留。
        e.ResetComponents();
        // 注意：e.id 保持原值不清空。调用方可能在本函数之后仍用旧指针读取 e->id
        // （例如 GameServer 断线回调里先 m_ecsToNetMap.erase(e->id) 再销毁）。
        // 失效判定由 active 标志负责，GetEntity 已经先检查 active。
        m_generations[index]++;
        m_freeIndices.push_back(index);
    }
}

Entity* EntityManager::GetEntity(EntityId id) const {
    uint32_t index = GetEntityIndex(id);
    uint32_t generation = GetEntityGeneration(id);
    if (index >= m_entities.size())
        return nullptr;

    const auto& e = m_entities[index];
    if (e.active && m_generations[index] == generation) {
        return const_cast<Entity*>(&e);
    }
    return nullptr;
}

void EntityManager::Clear() {
    m_entities.clear();
    m_freeIndices.clear();
    m_generations.clear();
}

}  // namespace game::core::ecs