#pragma once

#include <functional>
#include <glm/glm.hpp>

#include "Engine/ecs/ISystem.h"
#include "Engine/ecs/SystemScheduler.h"

namespace game::core::ecs {

// ============================================================
// tick 上下文：系统需要的外部环境。刻意保持极窄 ——
// 系统能看见什么，就决定了它能偷偷依赖什么。
// 注意：这里**没有实体类型**，所以 System.h 不再依赖任何实体定义。
// ============================================================
struct SystemContext {
    float dt = 0.0f;
    glm::vec3 playerPos{0.0f};
    std::function<bool(int, int, int)> isSolidAt;
};

// ============================================================
// 一份系统实现，两端各实例化一次：
//   game::core::ecs::PhysicsSystem<game::client::ClientEntityManager>
//   game::core::ecs::PhysicsSystem<game::server::ServerEntityManager>
// 这就是"客户端与服务端完全一致的实体注册表"的落地形式。
// ============================================================
template <typename WorldT>
using ISystem = Eng::ecs::ISystemT<WorldT, SystemContext>;

template <typename WorldT>
using SystemScheduler = Eng::ecs::SystemScheduler<WorldT, SystemContext>;

}  // namespace game::core::ecs