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
        p.input  = item.at("input").get<float>();
        p.output = item.at("output").get<float>();
        pts->push_back(p);
    }
    return pts;
}

DFPtr ParseNode(const nlohmann::json& j, const PerlinNoise& noise) {
    const std::string type = j.at("type").get<std::string>();

    if (type == "constant") {
        return std::make_shared<Constant>(j.at("value").get<float>());
    }
    if (type == "add") {
        return std::make_shared<Add>(
            ParseNode(j.at("a"), noise),
            ParseNode(j.at("b"), noise));
    }
    if (type == "mul") {
        return std::make_shared<Mul>(
            ParseNode(j.at("a"), noise),
            ParseNode(j.at("b"), noise));
    }
    if (type == "clamp") {
        return std::make_shared<Clamp>(
            ParseNode(j.at("input"), noise),
            j.at("min").get<float>(),
            j.at("max").get<float>());
    }
    if (type == "noise") {
        auto node = std::make_shared<NoiseDF>(
            noise,
            j.at("freqX").get<float>(),
            j.at("freqY").get<float>(),
            j.at("freqZ").get<float>(),
            j.at("octaves").get<int>());
        if (j.contains("shiftX") || j.contains("shiftZ")) {
            DFPtr sx = j.contains("shiftX") ? ParseNode(j.at("shiftX"), noise) : nullptr;
            DFPtr sz = j.contains("shiftZ") ? ParseNode(j.at("shiftZ"), noise) : nullptr;
            node->SetShift(std::move(sx), std::move(sz));
        }
        return node;
    }
    if (type == "spline") {
        return std::make_shared<SplineDF>(
            ParseNode(j.at("input"), noise),
            ParseSpline(j.at("points")));
    }

    throw std::runtime_error("Unknown density node type: " + type);
}

} // namespace

DFPtr DensityJsonFactory::Create(const nlohmann::json& j, const PerlinNoise& noise) {
    return ParseNode(j, noise);
}

} // namespace game::server