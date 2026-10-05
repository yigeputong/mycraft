#pragma once
#include <glm/glm.hpp>
#include "../core/Blocks.h"

namespace game {

struct Entity {
    uint32_t  id;                   // 服务端分配，客户端用
    glm::vec3 position;
    glm::vec3 velocity;
    uint32_t  age = 0;              // ticks
    uint32_t  pickupDelay = 0;      // ticks，>0 时不可捡

    virtual ~Entity() = default;
    virtual void Tick(float dt) {}  // 物理、生命周期
};

struct ItemEntity : Entity {
    ItemStack stack;                // 承载的物品
    // 物品实体特有的字段
};

}