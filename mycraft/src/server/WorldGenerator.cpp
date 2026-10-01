#include "game/server/WorldGenerator.h"
#include <numeric>
#include <random>
#include <cmath>
#include <print>

namespace game::server {

PerlinNoise::PerlinNoise(uint32_t seed) {
    // 初始化 0~255
    std::array<int, 256> p;
    std::iota(p.begin(), p.end(), 0);

    // 用 seed 打乱
    std::mt19937 rng(seed);
    std::shuffle(p.begin(), p.end(), rng);

    // 复制两份，避免索引越界
    for (int i = 0; i < 256; ++i) {
        m_perm[i] = p[i];
        m_perm[i + 256] = p[i];
    }
}

float PerlinNoise::Grad(int hash, float x, float y) {
    // 8 个方向
    switch (hash & 7) {
        case 0: return  x + y;
        case 1: return  x - y;
        case 2: return -x + y;
        case 3: return -x - y;
        case 4: return  x;
        case 5: return -x;
        case 6: return  y;
        case 7: return -y;
    }
    return 0;
}

float PerlinNoise::Grad(int hash, float x, float y, float z) {
    switch (hash & 15) {
        case 0:  return  x + y;
        case 1:  return -x + y;
        case 2:  return  x - y;
        case 3:  return -x - y;
        case 4:  return  x + z;
        case 5:  return -x + z;
        case 6:  return  x - z;
        case 7:  return -x - z;
        case 8:  return  y + z;
        case 9:  return -y + z;
        case 10: return  y - z;
        case 11: return -y - z;
        case 12: return  y + x;
        case 13: return -y + z;
        case 14: return  y - x;
        case 15: return -y - z;
    }
    return 0;
}

float PerlinNoise::Noise2D(float x, float y) const {
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;

    x -= std::floor(x);
    y -= std::floor(y);

    float u = Fade(x);
    float v = Fade(y);

    int aa = m_perm[m_perm[X] + Y];
    int ab = m_perm[m_perm[X] + Y + 1];
    int ba = m_perm[m_perm[X + 1] + Y];
    int bb = m_perm[m_perm[X + 1] + Y + 1];

    float res = Lerp(
        Lerp(Grad(aa, x, y),     Grad(ba, x - 1, y),     u),
        Lerp(Grad(ab, x, y - 1), Grad(bb, x - 1, y - 1), u),
        v
    );

    return res;   // 大约 [-0.7, 0.7]，可以乘个系数归一化
}

float PerlinNoise::Noise3D(float x, float y, float z) const {
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;
    int Z = static_cast<int>(std::floor(z)) & 255;

    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);

    float u = Fade(x);
    float v = Fade(y);
    float w = Fade(z);

    int A  = m_perm[X] + Y;
    int AA = m_perm[A] + Z;
    int AB = m_perm[A + 1] + Z;
    int B  = m_perm[X + 1] + Y;
    int BA = m_perm[B] + Z;
    int BB = m_perm[B + 1] + Z;

    float res = Lerp(
        Lerp(
            Lerp(Grad(m_perm[AA],     x,     y,     z),
                 Grad(m_perm[BA],     x - 1, y,     z), u),
            Lerp(Grad(m_perm[AB],     x,     y - 1, z),
                 Grad(m_perm[BB],     x - 1, y - 1, z), u),
            v),
        Lerp(
            Lerp(Grad(m_perm[AA + 1], x,     y,     z - 1),
                 Grad(m_perm[BA + 1], x - 1, y,     z - 1), u),
            Lerp(Grad(m_perm[AB + 1], x,     y - 1, z - 1),
                 Grad(m_perm[BB + 1], x - 1, y - 1, z - 1), u),
            v),
        w
    );

    return res;
}

float PerlinNoise::Fractal2D(float x, float y, int octaves,
                              float persistence, float lacunarity) const {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        total += Noise2D(x * frequency, y * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / maxValue;   // 归一化到 [-1, 1]
}



TerrainGenerator::TerrainGenerator(uint32_t seed)
    : m_noise(seed) {}

int TerrainGenerator::GetHeight(int x, int z) const {
    constexpr float FREQ      = 0.012f;
    constexpr float MASK_FREQ = 0.004f;
    constexpr int   OCTAVES   = 6;

    // 平原：基准 28，起伏 ±3    →  25 ~ 31（海平面 25 边上）
    constexpr int PLAINS_BASE = 28;
    constexpr int PLAINS_AMP  = 3;

    // 山地：基准 30，起伏 ±28   →  2 ~ 58（有山有深谷）
    constexpr int MTN_BASE    = 30;
    constexpr int MTN_AMP     = 28;

    // ---- mask：平原(0) 还是山地(1) ----
    float mask = m_noise.Fractal2D(x * MASK_FREQ, z * MASK_FREQ, 3);
    mask = std::clamp(mask * 2.0f + 0.5f, 0.0f, 1.0f);
    mask = mask * mask * mask * mask;    // 提高平原比例

    // ---- 高度噪声 ----
    float n = m_noise.Fractal2D(x * FREQ, z * FREQ, OCTAVES);
    n = std::clamp(n * 2.5f, -1.0f, 1.0f);
    float shaped = std::copysign(std::pow(std::abs(n), 0.6f), n);

    // ---- 基准和振幅都随 mask 插值 ----
    float base = PLAINS_BASE + mask * (MTN_BASE - PLAINS_BASE);
    float amp  = PLAINS_AMP  + mask * (MTN_AMP  - PLAINS_AMP);

    int h = static_cast<int>(base + shaped * amp);
    return std::clamp(h, 1, CHUNK_SIZE_Y - 2);
}

Chunk TerrainGenerator::GenerateChunk(int cx, int cz) const {
    Chunk chunk;
    chunk.chunk_x = cx;
    chunk.chunk_z = cz;

    const int baseX = cx * CHUNK_SIZE_X;
    const int baseZ = cz * CHUNK_SIZE_Z;

    constexpr int PAD = 1;
    constexpr int W   = CHUNK_SIZE_X + PAD * 2;
    constexpr int H   = CHUNK_SIZE_Z + PAD * 2;
    static thread_local std::array<std::array<int, H>, W> heights;

    for (int x = 0; x < W; ++x)
        for (int z = 0; z < H; ++z)
            heights[x][z] = GetHeight(baseX + x - PAD, baseZ + z - PAD);

    // ---- 主生成循环 ----
    for (int lx = 0; lx < CHUNK_SIZE_X; ++lx) {
        for (int lz = 0; lz < CHUNK_SIZE_Z; ++lz) {
            const int height = heights[lx + PAD][lz + PAD];

            // ---- 判断这一列是否是"沙滩" ----
            bool nearWater = (height < SEA_LEVEL);
            if (!nearWater) {
                for (int dx = -PAD; dx <= PAD && !nearWater; ++dx) {
                    for (int dz = -PAD; dz <= PAD && !nearWater; ++dz) {
                        if (heights[lx + PAD + dx][lz + PAD + dz] < SEA_LEVEL)  // ★ < 
                            nearWater = true;
                    }
                }
            }

            // ---- 填充整列 ----
            for (int ly = 0; ly < CHUNK_SIZE_Y; ++ly) {
                BlockType type = BlockType::Air;

                if (ly > height) {
                    type = (ly <= SEA_LEVEL) ? BlockType::Water : BlockType::Air;
                } else if (ly == height) {
                    type = nearWater ? BlockType::Sand : BlockType::GrassBlock;
                } else if (ly >= height - 3) {
                    type = BlockType::Dirt;
                } else {
                    type = BlockType::Stone;
                }

                chunk.blocks[ChunkIndex(lx, ly, lz)] = type;
            }
        }
    }

    // ---- 调试日志（在循环外）----
    int minH = 999, maxH = -999;
    for (int lx = 0; lx < CHUNK_SIZE_X; ++lx)
        for (int lz = 0; lz < CHUNK_SIZE_Z; ++lz) {
            int h = heights[lx + PAD][lz + PAD];
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }
    std::println("[Terrain] chunk({},{}) h_range={}~{}", cx, cz, minH, maxH);

    return chunk;   // ★ 移到循环外
}

}