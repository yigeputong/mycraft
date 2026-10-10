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
        case 0:     return  x + y;  case 1:     return  x - y;
        case 2:     return -x + y;  case 3:     return -x - y;
        case 4:     return  x;      case 5:     return -x;
        case 6:     return  y;      case 7:     return -y;
        default:    return 0.0f;
    }
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
        default: return 0.0f;
    }
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
    constexpr int MTN_PEAK    = 100;  // 山峰最高

    constexpr int PLAINS_AMP = 8;
    constexpr int MTN_AMP    = 25;

    // ---- 大陆性阈值 ----
    constexpr float CONT_OCEAN_MAX = -0.3f;   // < -0.3 → 海洋

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

    // ============================================================
    // 样条采样（控制点线性插值）
    // ============================================================

    struct SplinePoint{
        float input;
        float output;
    };

    static float SampleSpline(const SplinePoint* pts, size_t n, float x) {
        if (x <= pts[0].input)      return pts[0].output;
        if (x >= pts[n-1].input)    return pts[n-1].output;
        for (size_t i = 1; i < n; ++i) {
            if (x <= pts[i].input) {
                float t = (x - pts[i-1].input) / (pts[i].input - pts[i-1].input);
                return pts[i-1].output + t * (pts[i].output - pts[i-1].output);
            }
        }
        return 0;
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

BlockType TerrainGenerator::ApplySurfaceRule(Biome biome, int wy, int surfaceY,
                                              bool nearWater) const {
    if (wy != surfaceY) return BlockType::Dirt;  // 中间层

    // 顶层方块
    switch (biome) {
        case Biome::Ocean:
        case Biome::Beach:
        case Biome::Desert: 
            return BlockType::Sand;
        case Biome::Snow:
                return BlockType::Snow;
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
    // 只在 y ∈ [8, 120] 范围内判断（避免挖穿基岩/空中）
    if (wy < 8 || wy > 120) return false;

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

    // ★ 提前返回：矿脉只在 y ∈ (5, 80)，之外直接跳过 3D 噪声
    //    逻辑完全等价 —— 下面两个 if 的 y 条件都是 (5, 80) 子集
    if (wy <= 5 || wy >= 80) return base;

    float v = m_noise.Fractal3D(wx * ORE_SCALE,
                                 wy * ORE_SCALE,
                                 wz * ORE_SCALE, 2);

    if (wy > 30 && v > 0.55f) return BlockType::CoalOre;   // y 上界省略（提前返回已判）
    if (wy > 5  && v < -0.55f) return BlockType::IronOre;
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

    // ============================================================
    // STEP×STEP×STEP 网格采样（STEP = 6）
    // ============================================================
    constexpr int STEP = 6;
    constexpr int GX = 20 / STEP + 1;   // 4
    constexpr int GY = 256 / STEP + 2;  // 44
    constexpr int GZ = 20 / STEP + 1;   // 4

    static thread_local float grid[GX][GY][GZ];

    for (int gx = 0; gx < GX; ++gx) {
        int wx = baseX - 2 + gx * STEP;
        for (int gz = 0; gz < GZ; ++gz) {
            int wz = baseZ - 2 + gz * STEP;
            DensityParams p = ComputeParams(wx, wz);
            for (int gy = 0; gy < GY; ++gy) {
                int wy = gy * STEP;
                grid[gx][gy][gz] = ComputeDensity(wx, wy, wz, p);
            }
        }
    }

    // ---- 缓存每列 params（填充循环用）----
    static thread_local std::array<std::array<DensityParams, 16>, 16> params;
    for (int lx = 0; lx < 16; ++lx)
        for (int lz = 0; lz < 16; ++lz)
            params[lx][lz] = ComputeParams(baseX + lx, baseZ + lz);

    static thread_local std::array<std::array<int, 16>, 16> surfaceYs;

    // ---- 三线性插值 ----
    auto sampleDensity = [&](int lx, int ly, int lz) -> float {
        float fx = (lx + 2) / static_cast<float>(STEP);
        float fy = ly       / static_cast<float>(STEP);
        float fz = (lz + 2) / static_cast<float>(STEP);

        int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, GX - 1);
        int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, GY - 1);
        int z0 = static_cast<int>(fz), z1 = std::min(z0 + 1, GZ - 1);

        float tx = fx - x0, ty = fy - y0, tz = fz - z0;

        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

        float c00 = lerp(grid[x0][y0][z0], grid[x1][y0][z0], tx);
        float c10 = lerp(grid[x0][y1][z0], grid[x1][y1][z0], tx);
        float c01 = lerp(grid[x0][y0][z1], grid[x1][y0][z1], tx);
        float c11 = lerp(grid[x0][y1][z1], grid[x1][y1][z1], tx);

        float c0 = lerp(c00, c10, ty);
        float c1 = lerp(c01, c11, ty);

        return lerp(c0, c1, tz);
    };

    // ---- 整列 density 缓存 ----
    static thread_local float colDensity[CHUNK_SIZE_Y];

    for (int lx = 0; lx < CHUNK_SIZE_X; ++lx) {
        for (int lz = 0; lz < CHUNK_SIZE_Z; ++lz) {
            const int wx = baseX + lx;
            const int wz = baseZ + lz;

            for (int ly = 0; ly < CHUNK_SIZE_Y; ++ly)
                colDensity[ly] = sampleDensity(lx, ly, lz);

            int surfaceY = 0;
            for (int ly = CHUNK_SIZE_Y - 1; ly >= 0; --ly) {
                if (colDensity[ly] > 0.0f) {
                    surfaceY = ly;
                    break;
                }
            }
            surfaceYs[lx][lz] = surfaceY;

            const auto& p = params[lx][lz];
            Biome biome = BiomeAt(p, wx, wz, surfaceY);
            bool nearWater = (surfaceY <= SEA_LEVEL + 1);

            for (int ly = 0; ly < CHUNK_SIZE_Y; ++ly) {
                float d = colDensity[ly];
                BlockType type;

                if (d > 0.0f) {
                    if (ly == surfaceY) {
                        type = ApplySurfaceRule(biome, ly, surfaceY, nearWater);
                    } else if (ly >= surfaceY - 3) {
                        switch (biome) {
                            case Biome::Desert:  type = BlockType::Sandstone; break;
                            case Biome::Ocean:   type = BlockType::Sand;      break;
                            default:             type = BlockType::Dirt;      break;
                        }
                    } else {
                        type = BlockType::Stone;
                        type = OreForPosition(wx, ly, wz, type);
                    }
                } else {
                    type = (ly <= SEA_LEVEL) ? BlockType::Water : BlockType::Air;
                }

                chunk.blocks[ChunkIndex(lx, ly, lz)] = type;
            }
        }
    }

    GenerateTrees(chunk, surfaceYs);
    return chunk;
}

// ---- 地形偏移样条：cont → 垂直偏移 ----
// cont 低 → 负偏移（深海）; cont 高 → 正偏移（高峰）
static constexpr SplinePoint kOffsetSpline[] = {
    {-1.0f, -0.35f},   // 深海
    {-0.5f, -0.15f},   // 海洋
    {-0.2f, -0.02f},   // 海岸
    { 0.0f,  0.02f},   // 
    { 0.3f,  0.10f},   // 
    { 0.7f,  0.35f},   // 
    { 1.0f,  0.55f},   // 
};
static constexpr size_t kOffsetSplineN = std::size(kOffsetSpline);

// ---- 地形因子样条：erosion → 垂直缩放 ----
// erosion 低（陡峭）→ factor 大; erosion 高（平坦）→ factor 小
static constexpr SplinePoint kFactorSpline[] = {
    {-1.0f, 4.0f},     // 极陡：山壁高耸
    {-0.5f, 2.5f},
    { 0.0f, 1.5f},     // 中等
    { 0.5f, 0.7f},
    { 1.0f, 0.3f},     // 极平：几乎无起伏
};
static constexpr size_t kFactorSplineN = std::size(kFactorSpline);

// ============================================================
// 5 参数计算
// ============================================================
TerrainGenerator::DensityParams TerrainGenerator::ComputeParams(int wx, int wz) const {
    // ============================================================
    // Domain Warp：用两个独立噪声偏移采样坐标，打破 Perlin 的圆形
    // ============================================================
    constexpr float WARP_FREQ = 0.0055f;   // 扭曲频率（比 cont 高）
    constexpr float WARP_AMP  = 70.0f;     // 最大偏移 70 格

    float warpX = m_noise.Fractal2D(wx * WARP_FREQ + 11000.0f,
                                     wz * WARP_FREQ + 11000.0f, 2) * WARP_AMP;
    float warpZ = m_noise.Fractal2D(wx * WARP_FREQ + 12000.0f,
                                     wz * WARP_FREQ + 12000.0f, 2) * WARP_AMP;

    // ★ 用扭曲后的浮点坐标采样所有参数
    float sx = static_cast<float>(wx) + warpX;
    float sz = static_cast<float>(wz) + warpZ;

    DensityParams p;
    p.temp = std::clamp(m_noise.Fractal2D(sx * TEMP_FREQ  + 1500.0f, sz * TEMP_FREQ  + 1500.0f, 3)
                        * 3.0f, -1.0f, 1.0f);
    p.humid = std::clamp(m_noise.Fractal2D(sx * HUMID_FREQ + 3000.0f, sz * HUMID_FREQ + 3000.0f, 3)
                        * 2.5f, -1.0f, 1.0f);
    p.cont = std::clamp(m_noise.Fractal2D(sx * CONT_FREQ + 5000.0f, sz * CONT_FREQ + 5000.0f, 4) * 4.0f,
                         -1.0f, 1.0f);
    p.erosion = std::clamp(m_noise.Fractal2D(sx * 0.0025f + 7000.0f,
                                              sz * 0.0025f + 7000.0f, 3) * 2.5f,
                            -1.0f, 1.0f);
    p.weirdness = m_noise.Fractal2D(sx * 0.0045f + 9000.0f,
                                     sz * 0.0045f + 9000.0f, 2);
    return p;
}

// ============================================================
// 3D 密度核心
// ============================================================
float TerrainGenerator::ComputeDensity(int wx, int wy, int wz,
                                        const DensityParams& p) const {
    float offset = SampleSpline(kOffsetSpline, kOffsetSplineN, p.cont);
    float factor = SampleSpline(kFactorSpline, kFactorSplineN, p.erosion);

    // ★ 目标地表高度：cont 高 → 高，cont 低 → 低
    float targetY = SEA_LEVEL + offset * 64.0f;

    // ★ 用 (targetY - y)：y 越大 d 越小 → 上方空气
    //    factor 控制"过渡陡峭度"——factor 大 → 山壁陡
    float d = (targetY - static_cast<float>(wy)) / 96.0f * factor;

    // 3D 噪声（洞穴 + 起伏）
    float n1 = m_noise.Fractal3D(wx * 0.008f,
                                  wy * 0.016f,
                                  wz * 0.008f, 3);
    float n2 = m_noise.Fractal3D(wx * 0.030f + 500.0f,
                                  wy * 0.060f,
                                  wz * 0.030f + 500.0f, 2);
    float n3 = m_noise.Fractal3D(wx * 0.08f, wy * 0.15f, wz * 0.08f, 2);

    d += n1 * 0.115f + n2 * 0.06f + n3 * 0.06;

    // 上下 clamp（防止地穿基岩 / 山太高）
    if (wy > 220) d -= (wy - 220) * 0.03f;
    if (wy < 15)  d += (15 - wy) * 0.05f;

    return d;
}
// ============================================================
// 群系（用 surfaceY 修正温度）
// ============================================================
Biome TerrainGenerator::BiomeAt(const DensityParams& p, int /*wx*/, int /*wz*/, int surfaceY) const {
    // 里面不再 ComputeParams，直接用 p
    float tempAdj = p.temp - (surfaceY - SEA_LEVEL) * 0.008f;

    // 海洋 / 海岸
    if (p.cont < CONT_OCEAN_MAX)   return Biome::Ocean;
    if (surfaceY <= SEA_LEVEL + 1) return Biome::Beach;

    // 高海拔强制覆盖
    if (surfaceY > 175) return Biome::Snow;
    if (surfaceY > 155) return Biome::Mountain;

    // ★ 沙漠：低海拔 + 干热
    if (tempAdj > 0.20f && p.humid < -0.05f && surfaceY < 110)
        return Biome::Desert;

    // 其他
    if (tempAdj < -0.20f)  return Biome::Snow;
    if (p.humid  >  0.10f) return Biome::Forest;
    if (p.cont   >  0.80f) return Biome::Mountain;
    return Biome::Plains;
}

// ============================================================
// 树生成
// ============================================================
namespace {
    // 简单确定性 hash
    uint32_t HashCoords(int cx, int cz, int idx) {
        uint32_t h = static_cast<uint32_t>(cx) * 374761393u
                   ^ static_cast<uint32_t>(cz) * 668265263u
                   ^ static_cast<uint32_t>(idx) * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }
}

uint32_t TerrainGenerator::TreeHash(int cx, int cz, int idx) const {
    return HashCoords(cx, cz, idx);
}

bool TerrainGenerator::CanPlaceTree(int /*wx*/, int /*wz*/,
                                     int surfaceY, Biome biome) const {
    if (surfaceY < 30) return false;   // 水下不放
    if (surfaceY <= SEA_LEVEL) return false;
    if (biome == Biome::Ocean || biome == Biome::Beach) return false;
    if (biome == Biome::Snow) return false;
    if (biome == Biome::Mountain && surfaceY > 130) return false;  // 高岩不放
    return true;
}

void TerrainGenerator::PlaceTree(Chunk& chunk, int wx, int wy, int wz, int height) const {
    // ★ 预计算"相对该 chunk 的本地坐标"
    const int baseLX = wx - chunk.chunk_x * CHUNK_SIZE_X;
    const int baseLZ = wz - chunk.chunk_z * CHUNK_SIZE_Z;

    // ★ 局部 lambda：不再每次乘 chunk_x / chunk_z
    auto put = [&](int dx, int dy, int dz, BlockType type) {
        int lx = baseLX + dx;
        int lz = baseLZ + dz;
        int ly = wy + dy;
        if (lx < 0 || lx >= CHUNK_SIZE_X) return;
        if (lz < 0 || lz >= CHUNK_SIZE_Z) return;
        if (ly < 0 || ly >= CHUNK_SIZE_Y) return;
        auto& b = chunk.blocks[ChunkIndex(lx, ly, lz)];
        if (b == BlockType::Air) b = type;   // 不覆盖已有方块
    };

    // ---- 树干 ----
    for (int dy = 0; dy < height; ++dy) {
        put(0, dy, 0, BlockType::Wood);
    }

    // ---- 树叶：4 层 ----
    // 树干占 [0, height-1]，树叶从 height-2 开始
    const int topY = height;   // 相对 wy 的偏移

    // 第 1 层：5×5，去掉四角（dy = height-2）
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) {
            if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
            put(dx, topY - 2, dz, BlockType::Leaves);
        }

    // 第 2 层：5×5 外圈，跳过树干（dy = height-1）
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) {
            if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
            if (dx == 0 && dz == 0) continue;   // 树干位置
            put(dx, topY - 1, dz, BlockType::Leaves);
        }

    // 第 3 层：3×3，跳过树干（dy = height）
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            put(dx, topY, dz, BlockType::Leaves);
        }

    // 第 4 层：十字（dy = height+1）
    put( 0, topY + 1,  0, BlockType::Leaves);
    put( 1, topY + 1,  0, BlockType::Leaves);
    put(-1, topY + 1,  0, BlockType::Leaves);
    put( 0, topY + 1,  1, BlockType::Leaves);
    put( 0, topY + 1, -1, BlockType::Leaves);
}

void TerrainGenerator::GenerateTrees(
        Chunk& chunk,
        const std::array<std::array<int, 16>, 16>& surfaceYs) const {
    const int cx = chunk.chunk_x;
    const int cz = chunk.chunk_z;
    const int baseX = cx * CHUNK_SIZE_X;
    const int baseZ = cz * CHUNK_SIZE_Z;

    // 每 chunk 尝试 0~3 棵树
    uint32_t nSeed = TreeHash(cx, cz, -1);
    int count = nSeed % 4;   // 0, 1, 2, 3

    for (int i = 0; i < count; ++i) {
        uint32_t h = TreeHash(cx, cz, i);
        int lx = 2 + (h & 0xF) % 12;
        int lz = 2 + ((h >> 8) & 0xF) % 12;
        int height = 4 + ((h >> 16) & 0x3);
        int wx = baseX + lx;
        int wz = baseZ + lz;

        // ★ 直接查，不用重新扫
        int surfaceY = surfaceYs[lx][lz];
        if (surfaceY <= 0) continue;

        BlockType top = chunk.blocks[ChunkIndex(lx, surfaceY, lz)];
        if (top != BlockType::GrassBlock) continue;

        DensityParams p = ComputeParams(wx, wz);
        Biome biome = BiomeAt(p, wx, wz, surfaceY);
        if (!CanPlaceTree(wx, wz, surfaceY, biome)) continue;

        PlaceTree(chunk, wx, surfaceY + 1, wz, height);
    }
}

} // namespace game::server