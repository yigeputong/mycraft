#pragma once
#include "game/core/BlockRegistry.h"
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

// C -> S: 点击格子请求
struct ClickContainerPacket {
    int32_t stateId;  // 同步状态
    int16_t slot;     // 0-8 快捷栏, 9-35 主背包, -1 表示丢弃鼠标物品
    uint8_t button;   // 0=左键, 1=右键
    uint8_t mode;     // 0=普通点击, 4=丢弃 (Q)
};

} // namespace game