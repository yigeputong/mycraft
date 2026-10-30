#pragma once
#include "BlockRegistry.h"
#include <map>

namespace game {

// ==================== 组件类型 ====================
enum class DataComponentType : std::uint8_t {
    MaxStackSize,
};

// ==================== 组件值容器 ====================
class DataComponentMap {
public:
    void set(DataComponentType type, int value) { m_ints[type] = value; }
    const int* get(DataComponentType type) const {
        auto it = m_ints.find(type);
        return it == m_ints.end() ? nullptr : &it->second;
    }
    bool has(DataComponentType type) const { return m_ints.count(type) > 0; }
private:
    std::map<DataComponentType, int> m_ints;
};

// ==================== 物品堆叠 ====================
struct ItemStack {
    BlockId         type  = AIR_ID;
    int             count = 0;
    DataComponentMap components;

    int getMaxStackSize() const {
        if (auto* v = components.get(DataComponentType::MaxStackSize)) return *v;
        return 64;
    }
    bool isEmpty() const { return type == AIR_ID || count <= 0; }
};

inline ItemStack MakeItemStack(BlockId type, int count = 1) {
    ItemStack s;
    s.type  = type;
    s.count = count;
    return s;
}

} // namespace game