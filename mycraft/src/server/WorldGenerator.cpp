#include "game/server/WorldGenerator.h"
#include <numeric>
#include <random>

namespace game::server {

// ============================================================
// Perlin（保留你现有实现，只在下面加个 Fractal3D）
// ============================================================
PerlinNoise::PerlinNoise(uint32_t seed) {
    std::array<int, 256> p;
    std::iota(p.begin(), p.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(p.begin(), p.end(), rng);
    for (int i = 0; i < 256; ++i) {
        m_perm[i] = p[i];
        m_perm[i + 256] = p[i];
    }
}

float PerlinNoise::Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
float PerlinNoise::Lerp(float a, float b, float t) { return a + t * (b - a); }

float PerlinNoise::Grad(int hash, float x, float y) {
    switch (hash & 7) {
        case 0: return  x + y; case 1: return  x - y;
        case 2: return -x + y; case 3: return -x - y;
        case 4: return  x;     case 5: return -x;
        case 6: return  y;     case 7: return -y;
    }
    return 0;
}

float PerlinNoise::Grad(int hash, float x, float y, float z) {
    switch (hash & 15) {
        case 0:  return  x + y; case 1:  return -x + y;
        case 2:  return  x - y; case 3:  return -x - y;
        case 4:  return  x + z; case 5:  return -x + z;
        case 6:  return  x - z; case 7:  return -x - z;
        case 8:  return  y + z; case 9:  return -y + z;
        case 10: return  y - z; case 11: return -y - z;
        case 12: return  y + x; case 13: return -y + z;
        case 14: return  y - x; case 15: return -y - z;
    }
    return 0;
}

float PerlinNoise::Noise2D(float x, float y) const {
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;
    x -= std::floor(x);
    y -= std::floor(y);
    float u = Fade(x), v = Fade(y);
    int aa = m_perm[m_perm[X] + Y],     ab = m_perm[m_perm[X] + Y + 1];
    int ba = m_perm[m_perm[X + 1] + Y], bb = m_perm[m_perm[X + 1] + Y + 1];
    return Lerp(Lerp(Grad(aa, x, y),     Grad(ba, x - 1, y),     u),
                Lerp(Grad(ab, x, y - 1), Grad(bb, x - 1, y - 1), u), v);
}

float PerlinNoise::Noise3D(float x, float y, float z) const {
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;
    int Z = static_cast<int>(std::floor(z)) & 255;
    x -= std::floor(x); y -= std::floor(y); z -= std::floor(z);
    float u = Fade(x), v = Fade(y), w = Fade(z);
    int A  = m_perm[X] + Y,       AA = m_perm[A] + Z,     AB = m_perm[A + 1] + Z;
    int B  = m_perm[X + 1] + Y,   BA = m_perm[B] + Z,     BB = m_perm[B + 1] + Z;
    return Lerp(
        Lerp(Lerp(Grad(m_perm[AA],     x,     y,     z),
                  Grad(m_perm[BA],     x - 1, y,     z), u),
             Lerp(Grad(m_perm[AB],     x,     y - 1, z),
                  Grad(m_perm[BB],     x - 1, y - 1, z), u), v),
        Lerp(Lerp(Grad(m_perm[AA + 1], x,     y,     z - 1),
                  Grad(m_perm[BA + 1], x - 1, y,     z - 1), u),
             Lerp(Grad(m_perm[AB + 1], x,     y - 1, z - 1),
                  Grad(m_perm[BB + 1], x - 1, y - 1, z - 1), u), v), w);
}

float PerlinNoise::Fractal2D(float x, float y, int octaves,
                              float persistence, float lacunarity) const {
    float total = 0, freq = 1, amp = 1, maxV = 0;
    for (int i = 0; i < octaves; ++i) {
        total += Noise2D(x * freq, y * freq) * amp;
        maxV  += amp;
        amp   *= persistence;
        freq  *= lacunarity;
    }
    return total / maxV;
}

float PerlinNoise::Fractal3D(float x, float y, float z, int octaves,
                              float persistence, float lacunarity) const {
    float total = 0, freq = 1, amp = 1, maxV = 0;
    for (int i = 0; i < octaves; ++i) {
        total += Noise3D(x * freq, y * freq, z * freq) * amp;
        maxV  += amp;
        amp   *= persistence;
        freq  *= lacunarity;
    }
    return total / maxV;
}

// ============================================================
// TerrainGenerator 常量（集中调参）
// ============================================================
namespace {
    // ---- 噪声频率 ----
    constexpr float TEMP_FREQ  = 0.0025f;   // 从 0.0018 提高
    constexpr float HUMID_FREQ = 0.0030f;   // 从 0.0022 提高
    constexpr float CONT_FREQ  = 0.0035f;   // ★ 从 0.0012 提高近 3 倍

    // ---- 高度参数 ----
    constexpr int OCEAN_FLOOR = 30;   // 深海底部
    constexpr int COAST_BASE  = 60;   // 海岸（略低于海平面）
    constexpr int INLAND_BASE = 70;   // 内陆基准
    constexpr int MTN_PEAK    = 140;  // 山峰最高

    constexpr int PLAINS_AMP = 8;
    constexpr int MTN_AMP    = 45;

    // ---- 大陆性阈值 ----
    constexpr float CONT_OCEAN_MAX = -0.3f;   // < -0.3 → 海洋
    constexpr float CONT_INLAND_MIN = 0.25f;  // > 0.25 → 内陆

    // ---- 洞穴 ----
    constexpr float CAVE_SCALE       = 0.06f;   // 3D 噪声尺度
    constexpr float CHEESE_THRESHOLD = 0.72f;   // 芝士洞穴阈值
    constexpr float SPAGHETTI_WIDTH  = 0.08f;   // 意面洞穴宽度

    // ---- 矿脉 ----
    constexpr float ORE_SCALE = 0.14f;

    // 工具
    float Smoothstep(float a, float b, float t) {
        t = std::clamp((t - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    float Remap(float v, float a, float b, float c, float d) {
        return c + (d - c) * std::clamp((v - a) / (b - a), 0.0f, 1.0f);
    }
}

TerrainGenerator::TerrainGenerator(uint32_t seed) : m_noise(seed) {}

// ============================================================
// 阶段 1：生物群系
// ============================================================
Biome TerrainGenerator::GetBiome(int wx, int wz) const {
    float temp  = m_noise.Fractal2D(wx * TEMP_FREQ  + 1500.0f,
                                     wz * TEMP_FREQ  + 1500.0f, 3);
    float humid = m_noise.Fractal2D(wx * HUMID_FREQ + 3000.0f,
                                     wz * HUMID_FREQ + 3000.0f, 3);
    float cont  = m_noise.Fractal2D(wx * CONT_FREQ  + 5000.0f,
                                     wz * CONT_FREQ  + 5000.0f, 4);

    // ★ 关键：Perlin 归一化后实际集中在小范围，放大后才有海洋/山地
    cont  = std::clamp(cont  * 4.0f, -1.0f, 1.0f);
    temp  = std::clamp(temp  * 1.8f, -1.0f, 1.0f);
    humid = std::clamp(humid * 1.5f, -1.0f, 1.0f);

    return ComputeBiome(temp, humid, cont);
}

Biome TerrainGenerator::ComputeBiome(float temp, float humid, float cont) const {
    // 海洋判定最优先
    if (cont < CONT_OCEAN_MAX)  return Biome::Ocean;
    if (cont < CONT_OCEAN_MAX + 0.1f) return Biome::Beach;   // 海岸带

    // 内陆：按温度/湿度分
    // temp > 0.3 → 热；< -0.3 → 冷
    if (temp >  0.30f && humid < -0.10f) return Biome::Desert;
    if (temp < -0.30f)                    return Biome::Snow;
    if (humid >  0.15f)                   return Biome::Forest;

    // 高大陆性 + 中等侵蚀 → 山地
    if (cont > 0.55f && temp > -0.2f && temp < 0.3f) return Biome::Mountain;

    return Biome::Plains;
}

void TerrainGenerator::ComputeBiomeField(
        int baseX, int baseZ,
        std::array<std::array<Biome, 18>, 18>& out) const {
    for (int dx = -1; dx <= 16; ++dx)
        for (int dz = -1; dz <= 16; ++dz)
            out[dx + 1][dz + 1] = GetBiome(baseX + dx, baseZ + dz);
}

// ============================================================
// 阶段 1b：地形样条（参数 → 高度）
// ============================================================
int TerrainGenerator::GetHeight(int wx, int wz) const {
    // ---- 复算群系参数（和 GetBiome 用同一套放大）----
    float temp  = m_noise.Fractal2D(wx * TEMP_FREQ  + 1500.0f,
                                     wz * TEMP_FREQ  + 1500.0f, 3);
    float humid = m_noise.Fractal2D(wx * HUMID_FREQ + 3000.0f,
                                     wz * HUMID_FREQ + 3000.0f, 3);
    float cont  = m_noise.Fractal2D(wx * CONT_FREQ  + 5000.0f,
                                     wz * CONT_FREQ  + 5000.0f, 4);

    cont  = std::clamp(cont  * 4.0f, -1.0f, 1.0f);
    temp  = std::clamp(temp  * 1.8f, -1.0f, 1.0f);
    humid = std::clamp(humid * 1.5f, -1.0f, 1.0f);

    Biome biome = ComputeBiome(temp, humid, cont);

    // ---- 地形偏移样条：cont → 基准高度 ----
    float baseH;
    if (cont < CONT_OCEAN_MAX) {
        baseH = Remap(cont, -1.0f, CONT_OCEAN_MAX,
                      static_cast<float>(OCEAN_FLOOR),
                      static_cast<float>(COAST_BASE));
    } else if (cont < 0.6f) {
        baseH = Remap(cont, CONT_OCEAN_MAX, 0.6f,
                      static_cast<float>(COAST_BASE),
                      static_cast<float>(INLAND_BASE));
    } else {
        baseH = Remap(cont, 0.6f, 1.0f,
                      static_cast<float>(INLAND_BASE),
                      static_cast<float>(MTN_PEAK));
    }

    // ---- 起伏幅度 ----
    float mtnFactor = Smoothstep(0.3f, 0.8f, cont);
    float amp = PLAINS_AMP + mtnFactor * (MTN_AMP - PLAINS_AMP);

    // ---- 细节噪声 ----
    float n1 = m_noise.Fractal2D(wx * 0.012f + 100.0f, wz * 0.012f + 100.0f, 5);
    float n2 = m_noise.Fractal2D(wx * 0.04f,           wz * 0.04f,           3);

    // ---- biome 影响起伏 ----
    float biomeMul = 1.0f;
    if (biome == Biome::Desert || biome == Biome::Snow) biomeMul = 0.6f;
    if (biome == Biome::Ocean)                          biomeMul = 0.3f;

    float hf = baseH + (n1 * 1.4f + n2 * 0.3f) * amp * biomeMul;

    return std::clamp(static_cast<int>(hf), 1, CHUNK_SIZE_Y - 2);
}

// ============================================================
// 阶段 2：密度地形 → 填方块
// ============================================================
bool TerrainGenerator::IsSolidAt(int wx, int wy, int wz) const {
    int surfaceY = GetHeight(wx, wz);
    if (wy > surfaceY) return false;
    if (wy < 0) return false;
    // 洞穴
    if (IsCarvedByCave(wx, wy, wz)) return false;
    return true;
}

BlockType TerrainGenerator::FillBlock(int wx, int wy, int wz,
                                       Biome biome, int surfaceY) const {
    // ---- 空气 / 水 ----
    if (wy > surfaceY) {
        return (wy <= SEA_LEVEL) ? BlockType::Water : BlockType::Air;
    }

    // ---- 洞穴 ----
    if (wy < surfaceY - 2 && IsCarvedByCave(wx, wy, wz)) {
        return (wy < 10) ? BlockType::Air : BlockType::Air;
    }

    // ---- 地表附近：由表面规则决定 ----
    // （真正的替换在 ApplySurfaceRule，这里先填基础方块）
    if (wy >= surfaceY - 3) return BlockType::Dirt;   // 占位，会被表面规则覆盖
    return BlockType::Stone;
}

BlockType TerrainGenerator::ApplySurfaceRule(Biome biome, int wy, int surfaceY,
                                              bool nearWater) const {
    if (wy != surfaceY) return BlockType::Dirt;  // 中间层

    // 顶层方块
    switch (biome) {
        case Biome::Ocean:  return BlockType::Sand;
        case Biome::Beach:  return BlockType::Sand;
        case Biome::Desert: return BlockType::Sand;
        case Biome::Snow:   return BlockType::Snow;
        case Biome::Mountain:
            return (surfaceY > 110) ? BlockType::Stone : BlockType::GrassBlock;
        case Biome::Plains:
        case Biome::Forest:
        default:
            return nearWater ? BlockType::Sand : BlockType::GrassBlock;
    }
}

// ============================================================
// 阶段 4：雕刻器——3D 噪声洞穴
// ============================================================
bool TerrainGenerator::IsCarvedByCave(int wx, int wy, int wz) const {
    // 只在 y ∈ [5, 200] 范围内判断（避免挖穿基岩/空中）
    if (wy < 5 || wy > 200) return false;

    // 两个独立 3D 噪声
    float n1 = m_noise.Fractal3D(wx * CAVE_SCALE,
                                  wy * CAVE_SCALE * 1.5f,
                                  wz * CAVE_SCALE, 3);
    float n2 = m_noise.Fractal3D(wx * CAVE_SCALE + 1000.0f,
                                  wy * CAVE_SCALE * 1.5f + 1000.0f,
                                  wz * CAVE_SCALE + 1000.0f, 3);

    // ---- 芝士洞穴：噪声值大 → 挖空 ----
    if (n1 > CHEESE_THRESHOLD) return true;

    // ---- 意面洞穴：|噪声| 接近 0 → 挖空 ----
    if (std::abs(n2) < SPAGHETTI_WIDTH) return true;

    return false;
}

// ============================================================
// 阶段 5：矿脉
// ============================================================
BlockType TerrainGenerator::OreForPosition(int wx, int wy, int wz,
                                            BlockType base) const {
    if (base != BlockType::Stone) return base;

    float v = m_noise.Fractal3D(wx * ORE_SCALE,
                                 wy * ORE_SCALE,
                                 wz * ORE_SCALE, 2);

    // 煤：y ∈ [30, 80]
    if (wy > 30 && wy < 80 && v > 0.55f) return BlockType::CoalOre;
    // 铁：y ∈ [5, 50]
    if (wy > 5 && wy < 50 && v < -0.55f) return BlockType::IronOre;

    return base;
}

// ============================================================
// 主入口：GenerateChunk
// ============================================================
Chunk TerrainGenerator::GenerateChunk(int cx, int cz) const {
    Chunk chunk;
    chunk.chunk_x = cx;
    chunk.chunk_z = cz;

    const int baseX = cx * CHUNK_SIZE_X;
    const int baseZ = cz * CHUNK_SIZE_Z;

    // ---------- 阶段 1：生物群系 + 高度（带 1 格 padding）----------
    // 只在 18x18 上算一次 2D 场，避免每方块重算
    static thread_local std::array<std::array<Biome, 18>, 18> biomes;
    static thread_local std::array<std::array<int,   18>, 18> heights;

    for (int dx = -1; dx <= 16; ++dx) {
        for (int dz = -1; dz <= 16; ++dz) {
            int wx = baseX + dx;
            int wz = baseZ + dz;
            biomes [dx + 1][dz + 1] = GetBiome (wx, wz);
            heights[dx + 1][dz + 1] = GetHeight(wx, wz);
        }
    }

    // ---------- 阶段 2+3+4+5：逐方块填充 ----------
    for (int lx = 0; lx < CHUNK_SIZE_X; ++lx) {
        for (int lz = 0; lz < CHUNK_SIZE_Z; ++lz) {
            const int wx = baseX + lx;
            const int wz = baseZ + lz;
            const int surfaceY = heights[lx + 1][lz + 1];
            const Biome biome  = biomes [lx + 1][lz + 1];

            // 判断是否靠水（用 5x5 邻域）
            bool nearWater = (surfaceY <= SEA_LEVEL + 1);
            if (!nearWater) {
                for (int ddx = -2; ddx <= 2 && !nearWater; ++ddx)
                    for (int ddz = -2; ddz <= 2 && !nearWater; ++ddz) {
                        int nx = lx + 1 + ddx, nz = lz + 1 + ddz;
                        if (nx >= 0 && nx < 18 && nz >= 0 && nz < 18) {
                            if (heights[nx][nz] <= SEA_LEVEL) nearWater = true;
                        }
                    }
            }

            // 整列填充
            for (int ly = 0; ly < CHUNK_SIZE_Y; ++ly) {
                BlockType type;

                if (ly > surfaceY) {
                    type = (ly <= SEA_LEVEL) ? BlockType::Water : BlockType::Air;
                } else if (ly == surfaceY) {
                    // 阶段 3：表面规则
                    type = ApplySurfaceRule(biome, ly, surfaceY, nearWater);
                } else if (ly >= surfaceY - 3) {
                    // 中间层：按 biome 分层
                    switch (biome) {
                        case Biome::Desert:  type = BlockType::Sandstone; break;
                        case Biome::Ocean:   type = BlockType::Sand;      break;
                        default:             type = BlockType::Dirt;      break;
                    }
                } else {
                    type = BlockType::Stone;
                }

                // 阶段 4：雕刻器（挖洞）
                if (type != BlockType::Air
                    && type != BlockType::Water
                    && ly < surfaceY - 1
                    && IsCarvedByCave(wx, ly, wz)) {
                    type = BlockType::Air;
                }

                // 阶段 5：矿脉（只在石头上）
                if (type == BlockType::Stone) {
                    type = OreForPosition(wx, ly, wz, type);
                }

                chunk.blocks[ChunkIndex(lx, ly, lz)] = type;
            }
        }
    }

    return chunk;
}

} // namespace game::server