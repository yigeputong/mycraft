#pragma once
#include "game/core/ecs/System.h"

namespace game::core::ecs {

class PhysicsSystem : public ISystem {
public:
    void Update(EntityManager& world, const SystemContext& ctx) override;
};

}  // namespace game::core::ecs