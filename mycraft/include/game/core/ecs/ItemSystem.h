#pragma once
#include "game/core/ecs/System.h"

namespace game::core::ecs {

class ItemSystem : public ISystem {
public:
    void Update(EntityManager& world, const SystemContext& ctx) override;
};

}  // namespace game::core::ecs