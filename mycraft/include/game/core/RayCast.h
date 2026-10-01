#pragma once
#include "./Blocks.h"
#include <glm/glm.hpp>
#include <cmath>

namespace game {

struct RayHit {
    bool  hit = false;
    int   bx = 0, by = 0, bz = 0;   // 击中的方块（世界坐标）
    int   nx = 0, ny = 0, nz = 0;   // 命中面的法线
    float dist = 0.0f;
};

// Amanatides & Woo voxel traversal
// getBlock(x,y,z) -> BlockType（调用方注入，避免依赖具体世界实现）
template<typename GetBlockFn>
RayHit RayCast(glm::vec3 origin, glm::vec3 dir, float maxDist, GetBlockFn getBlock) {
    if (glm::length(dir) < 1e-6f) return {};
    dir = glm::normalize(dir);

    int x = (int)std::floor(origin.x);
    int y = (int)std::floor(origin.y);
    int z = (int)std::floor(origin.z);

    int stepX = (dir.x > 0) - (dir.x < 0);
    int stepY = (dir.y > 0) - (dir.y < 0);
    int stepZ = (dir.z > 0) - (dir.z < 0);

    constexpr float INF = 1e30f;
    float tDeltaX = (dir.x != 0) ? std::abs(1.0f / dir.x) : INF;
    float tDeltaY = (dir.y != 0) ? std::abs(1.0f / dir.y) : INF;
    float tDeltaZ = (dir.z != 0) ? std::abs(1.0f / dir.z) : INF;

    auto firstBoundary = [](float o, float d) {
        if (d > 0) return std::floor(o) + 1.0f - o;
        if (d < 0) return o - std::floor(o);
        return 1e30f;
    };
    float tMaxX = (dir.x != 0) ? firstBoundary(origin.x, dir.x) / std::abs(dir.x) : INF;
    float tMaxY = (dir.y != 0) ? firstBoundary(origin.y, dir.y) / std::abs(dir.y) : INF;
    float tMaxZ = (dir.z != 0) ? firstBoundary(origin.z, dir.z) / std::abs(dir.z) : INF;

    int nx = 0, ny = 0, nz = 0;
    float t = 0.0f;

    while (t <= maxDist) {
        BlockType b = getBlock(x, y, z);
        if (b != BlockType::Air && b != BlockType::Water) {
            return { true, x, y, z, nx, ny, nz, t };
        }

        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            x += stepX; t = tMaxX; tMaxX += tDeltaX;
            nx = -stepX; ny = 0; nz = 0;
        } else if (tMaxY < tMaxZ) {
            y += stepY; t = tMaxY; tMaxY += tDeltaY;
            nx = 0; ny = -stepY; nz = 0;
        } else {
            z += stepZ; t = tMaxZ; tMaxZ += tDeltaZ;
            nx = 0; ny = 0; nz = -stepZ;
        }
    }
    return {};
}

} // namespace game