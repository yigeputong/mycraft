#pragma once
#include <cstdint>
#include <map>

namespace game {

enum BlockType : uint8_t {
    Air = 0,
    Stone, GrassBlock, Dirt, Sand, Water,
    Void
};

struct Block {
    BlockType type;
};

inline bool IsSolid(BlockType t) {
    return t != BlockType::Air && t != BlockType::Void;
}

// ==================== 组件类型 ====================
enum class DataComponentType : uint16_t {
    MaxStackSize,   // int, 默认 64
    // 未来: Damage, MaxDamage, ToolType, AttackDamage, ...
};

// ==================== 组件值容器 ====================
class DataComponentMap {
public:
    void set(DataComponentType type, int value) {
        m_ints[type] = value;
    }

    const int* get(DataComponentType type) const {
        auto it = m_ints.find(type);
        return it == m_ints.end() ? nullptr : &it->second;
    }

    bool has(DataComponentType type) const {
        return m_ints.count(type) > 0;
    }

private:
    std::map<DataComponentType, int> m_ints;
    // 未来加: std::map<DataComponentType, std::string> m_strings;
    //         std::map<DataComponentType, glm::vec3> m_vec3s;
};

// ==================== 物品堆叠 ====================
struct ItemStack {
    BlockType       type  = BlockType::Air;
    int             count = 0;
    DataComponentMap components;

    int getMaxStackSize() const {
        if (auto* v = components.get(DataComponentType::MaxStackSize))
            return *v;
        return 64;
    }

    bool isEmpty() const { return type == BlockType::Air || count <= 0; }
};

inline ItemStack MakeItemStack(BlockType type, int count = 1) {
    ItemStack s;
    s.type  = type;
    s.count = count;
    return s;
}

}