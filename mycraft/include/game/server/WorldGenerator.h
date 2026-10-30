#pragma once
#include <array>
#include <cstdint>
#include "game/core/BlockRegistry.h" // ★ 引入注册表
#include "game/core/World.h"

namespace game::server {

// ============================================================
// Perlin 噪声
// ============================================================
class PerlinNoise {
public:
    explicit PerlinNoise(uint32_t seed);
    float Noise2D(float x, float y) const;
    float Noise3D(float x, float y, float z) const;
    float Fractal2D(float x, float y, int octaves,
                    float persistence = 0.5f, float lacunarity = 2.0f) const;
    float Fractal3D(float x, float y, float z, int octaves,
                    float persistence = 0.5f, float lacunarity = 2.0f) const;

private:
    std::array<int, 512> m_perm{};
    static float Fade(float t);
    static float Lerp(float a, float b, float t);
    static float Grad(int hash, float x, float y);
    static float Grad(int hash, float x, float y, float z);
};

// ============================================================
// 生物群系与阶段（保持不变）
// ============================================================
enum class Biome : uint8_t {
    Ocean, Beach, Plains, Forest, Desert, Snow, Mountain,
};

enum class GenPhase : uint8_t {
    Biomes, NoiseTerrain, Surface, Carvers, Features,
};

// ============================================================
// TerrainGenerator
// ============================================================
class TerrainGenerator {
public:
    // ★ 注入注册表
    explicit TerrainGenerator(uint32_t seed, const BlockRegistry& reg);

    Chunk GenerateChunk(int cx, int cz) const;

    Biome    GetBiome(int wx, int wz) const;
    int      GetHeight(int wx, int wz) const;
    bool     IsSolidAt(int wx, int wy, int wz) const;
    int      GetSurfaceY(int wx, int wz) const;

private:
    PerlinNoise m_noise;
    const BlockRegistry& m_registry; // ★ 注册表引用

    // ★ 缓存常用方块的 ID，避免生成时反复查询
    BlockId ID_STONE;
    BlockId ID_DIRT;
    BlockId ID_GRASS;
    BlockId ID_SAND;
    BlockId ID_WATER;
    BlockId ID_SNOW;
    BlockId ID_WOOD;
    BlockId ID_LEAVES;
    BlockId ID_TALLGRASS;
    BlockId ID_POPPY;
    BlockId ID_DANDELION;

    // ---- 阶段 1：生物群系 ----
    Biome    ComputeBiome(float temp, float humid, float cont) const;
    void     ComputeBiomeField(int baseX, int baseZ,
                                std::array<std::array<Biome, 18>, 18>& out) const;
    void     ComputeHeightField(int baseX, int baseZ,
                                 const std::array<std::array<Biome, 18>, 18>& biomes,
                                 std::array<std::array<int, 18>, 18>& out) const;

    // ---- 阶段 3：表面规则 ----
    BlockId  ApplySurfaceRule(Biome biome, int wy, int surfaceY, bool nearWater) const; // ★

    // ---- 阶段 4：雕刻器 ----
    bool IsCarvedByCave(int wx, int wy, int wz, int surfaceY) const;
    float SampleCaveDiff(int wx, int wy, int wz) const;
    bool  IsCaveAt(float diff, int ly, int surfaceY) const;

    // ---- 阶段 5：矿脉 ----
    BlockId OreForPosition(int wx, int wy, int wz, BlockId base) const; // ★

    struct DensityParams {
        float temp      = 0.0f;
        float humid     = 0.0f;
        float cont      = 0.0f;
        float erosion   = 0.0f;
        float weirdness = 0.0f;
    };

    DensityParams ComputeParams(int wx, int wz) const;
    float ComputeDensity(int wx, int wy, int wz, const DensityParams& p) const;
    Biome BiomeAt(const DensityParams& p, int wx, int wz, int surfaceY) const;

    // ---- 树 ----
    void GenerateTrees(Chunk& chunk,
                        const std::array<std::array<int, 16>, 16>& surfaceYs) const;
    bool CanPlaceTree(int wx, int wz, int surfaceY, Biome biome) const;
    void PlaceTree(Chunk& chunk, int wx, int wy, int wz, int height) const;
    uint32_t TreeHash(int cx, int cz, int idx) const;
};

} // namespace game::server