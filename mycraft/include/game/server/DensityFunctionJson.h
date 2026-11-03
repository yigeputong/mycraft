#pragma once
#include "game/server/DensityFunction.h"
#include <nlohmann/json_fwd.hpp>

namespace game::server {

// 从 JSON 递归构造密度节点树
// 支持的节点：constant / add / mul / clamp / noise / spline
// spline 节点的 points 由 SplineDF 自身持有 shared_ptr，无外部生命周期依赖
class DensityJsonFactory {
public:
    static DFPtr Create(const nlohmann::json& j, const PerlinNoise& noise);
};

} // namespace game::server