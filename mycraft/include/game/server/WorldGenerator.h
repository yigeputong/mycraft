#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "game/core/Blocks.h"
#include "game/core/World.h"

namespace game::server {

// ============================================================
// Perlin 噪声（保留你现在的实现，不动）
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
// 生物群系
// ============================================================
enum class Biome : uint8_t {
    Ocean,      // 海洋：海平面以下
    Beach,      // 沙滩：海岸带
    Plains,     // 平原：低海拔内陆
    Forest,     // 森林：中等湿度内陆
    Desert,     // 沙漠：低湿度高温
    Snow,       // 雪地：低温
    Mountain,   // 山地：高侵蚀度内陆
};

// ============================================================
// 生成阶段枚举（对齐文档的"分阶段生成"）
// ============================================================
enum class GenPhase : uint8_t {
    Biomes,      // 1. 计算生物群系（纯 2D 噪声）
    NoiseTerrain,// 2. 计算密度函数 → 填方块
    Surface,     // 3. 应用表面规则（草/沙/雪）
    Carvers,     // 4. 挖洞穴
    Features,    // 5. 地物（树、矿脉）
};

// ============================================================
// TerrainGenerator
// ============================================================
class TerrainGenerator {
public:
    explicit TerrainGenerator(uint32_t seed);

    // ★ 对外唯一入口：生成一个完整区块
    Chunk GenerateChunk(int cx, int cz) const;

    // ---- 各阶段的独立查询（用于调试 / 跨区块地物）----
    Biome    GetBiome(int wx, int wz) const;
    int      GetHeight(int wx, int wz) const;         // 地表高度
    bool     IsSolidAt(int wx, int wy, int wz) const; // 密度函数查询

private:
    PerlinNoise m_noise;

    // ---- 阶段 1：生物群系 ----
    Biome    ComputeBiome(float temp, float humid, float cont) const;
    void     ComputeBiomeField(int baseX, int baseZ,
                                std::array<std::array<Biome, 18>, 18>& out) const;
    void     ComputeHeightField(int baseX, int baseZ,
                                 const std::array<std::array<Biome, 18>, 18>& biomes,
                                 std::array<std::array<int, 18>, 18>& out) const;

    // ---- 阶段 2：密度地形 ----
    BlockType FillBlock(int wx, int wy, int wz, Biome biome, int surfaceY) const;

    // ---- 阶段 3：表面规则 ----
    BlockType ApplySurfaceRule(Biome biome, int wy, int surfaceY,
                                bool nearWater) const;

    // ---- 阶段 4：雕刻器 ----
    bool IsCarvedByCave(int wx, int wy, int wz) const;

    // ---- 阶段 5：矿脉 ----
    BlockType OreForPosition(int wx, int wy, int wz, BlockType base) const;
};

} // namespace game::server