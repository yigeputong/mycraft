#include "game/server/DensityFunctionJson.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace game::server {

namespace {

std::shared_ptr<const std::vector<SplinePoint>> ParseSpline(const nlohmann::json& j) {
    auto pts = std::make_shared<std::vector<SplinePoint>>();
    pts->reserve(j.size());
    for (const auto& item : j) {
        SplinePoint p;
        p.input = item.at("input").get<float>();
        p.output = item.at("output").get<float>();
        pts->push_back(p);
    }
    return pts;
}

DFPtr ParseNode(const nlohmann::json& j, const PerlinNoise& noise) {
    const std::string type = j.at("type").get<std::string>();

    // ================== 基础数学节点 ==================
    if (type == "constant") {
        return std::make_shared<Constant>(j.at("value").get<float>());
    }
    if (type == "add") {
        return std::make_shared<Add>(ParseNode(j.at("a"), noise), ParseNode(j.at("b"), noise));
    }
    if (type == "mul") {
        return std::make_shared<Mul>(ParseNode(j.at("a"), noise), ParseNode(j.at("b"), noise));
    }
    if (type == "clamp") {
        return std::make_shared<Clamp>(ParseNode(j.at("input"), noise), j.at("min").get<float>(),
                                       j.at("max").get<float>());
    }
    if (type == "min") {
        return std::make_shared<Min>(ParseNode(j.at("argument1"), noise), ParseNode(j.at("argument2"), noise));
    }
    if (type == "max") {
        return std::make_shared<Max>(ParseNode(j.at("argument1"), noise), ParseNode(j.at("argument2"), noise));
    }

    // ================== 进阶映射节点 ==================
    if (type == "quarter_negative") {
        return std::make_shared<QuarterNegative>(ParseNode(j.at("argument"), noise));
    }
    if (type == "half_negative") {
        return std::make_shared<HalfNegative>(ParseNode(j.at("argument"), noise));
    }
    if (type == "y_clamped_gradient") {
        return std::make_shared<YClampedGradient>(j.at("from_y").get<int>(), j.at("to_y").get<int>(),
                                                  j.at("from_value").get<float>(), j.at("to_value").get<float>());
    }
    if (type == "range_choice") {
        return std::make_shared<RangeChoice>(
            ParseNode(j.at("input"), noise), j.at("min_inclusive").get<float>(), j.at("max_exclusive").get<float>(),
            ParseNode(j.at("when_in_range"), noise), ParseNode(j.at("when_out_of_range"), noise));
    }

    // ================== 噪声与样条 ==================
    if (type == "noise") {
        // 原版 JSON 常用 xz_scale 和 y_scale，这里为了兼容你的结构做了转换
        float scaleXZ = j.value("xz_scale", 1.0f);
        float scaleY = j.value("y_scale", 1.0f);

        auto node = std::make_shared<NoiseDF>(noise,
                                              scaleXZ / 80.0f,  // 模拟原版的采样频率系数，你可以根据实际情况调整
                                              scaleY / 160.0f, scaleXZ / 80.0f, j.value("octaves", 4));

        if (j.contains("shift_x") || j.contains("shift_z")) {
            DFPtr sx = j.contains("shift_x") ? ParseNode(j.at("shift_x"), noise) : nullptr;
            DFPtr sz = j.contains("shift_z") ? ParseNode(j.at("shift_z"), noise) : nullptr;
            node->SetShift(std::move(sx), std::move(sz));
        }
        return node;
    }
    if (type == "spline") {
        return std::make_shared<SplineDF>(ParseNode(j.at("input"), noise), ParseSpline(j.at("points")));
    }

    // ================== 动态/缓存包装节点（目前为空实现） ==================
    // 原版中的 flat_cache, cache_2d, cache_once, interpolated 等标记
    // 在实际计算树中，如果你不实现缓存，可以直接透传子节点
    if (type == "flat_cache" || type == "cache_2d" || type == "cache_once" || type == "interpolated") {
        return ParseNode(j.at("argument"), noise);
    }
    if (type == "squeeze") {
        auto child = ParseNode(j.at("argument"), noise);
        return std::make_shared<LambdaDF>([child](float x, float y, float z) {
            float d = child->Compute(x, y, z);
            if (d > 1.0f)
                return 1.0f;
            if (d < -1.0f)
                return -1.0f;
            return d - (d * d * d) / 3.0f;
        });
    }

    throw std::runtime_error("Unknown density node type: " + type);
}

}  // namespace

DFPtr DensityJsonFactory::Create(const nlohmann::json& j, const PerlinNoise& noise) {
    return ParseNode(j, noise);
}

}  // namespace game::server