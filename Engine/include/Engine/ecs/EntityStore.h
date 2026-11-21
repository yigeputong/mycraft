#pragma once

#include <cassert>
#include <concepts>
#include <cstdint>
#include <deque>
#include <vector>

#include "Engine/ecs/Entity.h"

namespace Eng::ecs {

namespace detail {

// 断言钩子是可选的：EntityT 只要提供 IsPristine() 就会被检查，
// 不提供也能正常编译。引擎不应该强制业务实现一个 Debug 方法。
template <typename T>
concept HasPristineCheck = requires(const T& e) {
    { e.IsPristine() } -> std::convertible_to<bool>;
};

}  // namespace detail

// ============================================================
// EntityStore<EntityT>：实体存储算法。只认识三件事：
//   EntityT 有 id / active 字段，有 ResetComponents() 方法，可默认构造。
// 它不认识任何组件，也不知道 EntityT 长什么样。
//
// 对 EntityT 的契约：
//   1. Eng::ecs::EntityId id;      默认 INVALID_ENTITY
//   2. bool active;                默认 true
//   3. void ResetComponents();     幂等；清空全部可选组件，但不碰 id / active
//   4. 可默认构造
//
// 线程模型：本类**不是**线程安全的。容器是 std::deque：
//   push_back 不会让已有元素的引用失效，但会让全部迭代器失效。
//   因此「一边遍历一边在别的线程创建实体」仍然是未定义行为，
//   哪怕编译器不报错。跨线程访问必须由调用方自己加锁。
// ============================================================
template <typename EntityT>
class EntityStore {
public:
    EntityStore() = default;

    // 世界是唯一权威数据，禁止拷贝（拷贝出来的两个世界会各自分发相同的 ID）。
    EntityStore(const EntityStore&) = delete;
    EntityStore& operator=(const EntityStore&) = delete;

    EntityId CreateEntity() {
        uint32_t index = 0;
        uint32_t generation = 0;

        if (!m_freeIndices.empty()) {
            index = m_freeIndices.back();
            m_freeIndices.pop_back();
            generation = m_generations[index];
        } else {
            index = static_cast<uint32_t>(m_entities.size());
            m_entities.emplace_back();
            // 初始 generation 从 1 开始，保证首个实体的 ID 不为 0（见 Entity.h）。
            m_generations.push_back(1);
            generation = 1;
        }

        EntityT& e = m_entities[index];

        // 复用槽位前必须保证干净。断言失败说明有路径绕过 DestroyEntity
        // 释放了槽位，或者新增组件忘了在 ResetComponents() 里补一行。
        if constexpr (detail::HasPristineCheck<EntityT>) {
            assert(e.IsPristine() && "Entity slot is not pristine before reuse");
        }

        e.ResetComponents();
        e.id = MakeEntityId(index, generation);
        e.active = true;
        return e.id;
    }

    void DestroyEntity(EntityId id) {
        const uint32_t index = GetEntityIndex(id);
        if (index >= m_entities.size()) {
            return;  // 无效 ID，静默忽略
        }

        EntityT& e = m_entities[index];
        if (e.id != id || !e.active) {
            return;  // 已销毁或已被复用的旧 ID，静默忽略
        }

        e.active = false;

        // 立即清零：EntityT 可能持有大块资源（例如 ChunkComponent 持有
        // shared_ptr<Chunk>，约 320KB）。若等到槽位被复用时才释放，
        // 卸载后的内存在 free list 里会长期滞留。
        e.ResetComponents();

        // 注意：e.id 保持原值不清空。调用方可能在本函数之后仍用旧指针
        // 读取 e->id（例如 GameServer 断线回调里先 m_ecsToNetMap.erase(e->id)
        // 再销毁）。失效判定由 active 标志负责，GetEntity 已经先检查 active。
        m_generations[index]++;
        m_freeIndices.push_back(index);
    }

    // 只有 active 且 generation 匹配才返回非空。
    // 返回的是可写指针，是刻意的：系统需要原地修改组件。
    [[nodiscard]] EntityT* GetEntity(EntityId id) const {
        const uint32_t index = GetEntityIndex(id);
        if (index >= m_entities.size()) {
            return nullptr;
        }

        const EntityT& e = m_entities[index];
        if (e.active && m_generations[index] == GetEntityGeneration(id)) {
            return const_cast<EntityT*>(&e);
        }
        return nullptr;
    }

    // 暴露全部槽位，包含 inactive 的。调用方必须自己检查 active /
    // std::optional 是否持有值。遍历期间禁止创建实体（会扩容并让迭代器失效）。
    std::deque<EntityT>& GetEntities() { return m_entities; }
    const std::deque<EntityT>& GetEntities() const { return m_entities; }

    void Clear() {
        m_entities.clear();
        m_freeIndices.clear();
        m_generations.clear();
    }

private:
    std::deque<EntityT> m_entities;
    std::vector<uint32_t> m_freeIndices;
    std::vector<uint32_t> m_generations;
};

}  // namespace Eng::ecs