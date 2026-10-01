#pragma once
#include <array>
#include "game/core/Blocks.h"
#include "game/core/World.h"

namespace game::server {

class PerlinNoise {
public:
    // seed 决定随机排列，同一 seed 生成同一地图
    explicit PerlinNoise(uint32_t seed = 0);

    // 2D 噪声，返回 [-1, 1]
    float Noise2D(float x, float y) const;

    // 3D 噪声，返回 [-1, 1]
    float Noise3D(float x, float y, float z) const;

    // 分形叠加（fBm）：多个频率叠加，更自然
    float Fractal2D(float x, float y, int octaves = 4,
                    float persistence = 0.5f, float lacunarity = 2.0f) const;

private:
    std::array<int, 512> m_perm;

    static float Fade(float t) {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }
    static float Lerp(float a, float b, float t) {
        return a + t * (b - a);
    }
    static float Grad(int hash, float x, float y);
    static float Grad(int hash, float x, float y, float z);
};

class TerrainGenerator {
public:
    explicit TerrainGenerator(uint32_t seed = 0);
    Chunk GenerateChunk(int cx, int cz) const;

    static constexpr int SEA_LEVEL = 25;

private:
    PerlinNoise m_noise;

    int GetHeight(int x, int z) const;
};


}