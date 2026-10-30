#include "game/server/WorldGenerator.h"
#include <numeric>
#include <random>
#include <climits>

namespace game::server {

// ============================================================
// Perlin 噪声（保持你原有实现）
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
        case 0: return x + y; case 1: return x - y;
        case 2: return -x + y; case 3: return -x - y;
        case 4: return x; case 5: return -x;
        case 6: return y; case 7: return -y;
        default: return 0.0f;
    }
}

float PerlinNoise::Grad(int hash, float x, float y, float z) {
    switch (hash & 15) {
        case 0: return x + y; case 1: return -x + y;
        case 2: return x - y; case 3: return -x - y;
        case 4: return x + z; case 5: return -x + z;
        case 6: return x - z; case 7: return -x - z;
        case 8: return y + z; case 9: return -y + z;
        case 10: return y - z; case 11: return -y - z;
        case 12: return y + x; case 13: return -y + z;
        case 14: return y - x; case 15: return -y - z;
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
    return Lerp(Lerp(Grad(aa, x, y), Grad(ba, x - 1, y), u),
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
        Lerp(Lerp(Grad(m_perm[AA],     x,     y,     z), Grad(m_perm[BA],     x - 1, y,     z), u),
             Lerp(Grad(m_perm[AB],     x,     y - 1, z), Grad(m_perm[BB],     x - 1, y - 1, z), u), v),
        Lerp(Lerp(Grad(m_perm[AA + 1], x,     y,     z - 1), Grad(m_perm[BA + 1], x - 1, y,     z - 1), u),
             Lerp(Grad(m_perm[AB + 1], x,     y - 1, z - 1), Grad(m_perm[BB + 1], x - 1, y - 1, z - 1), u), v), w);
}

float PerlinNoise::Fractal2D(float x, float y, int octaves, float persistence, float lacunarity) const {
    float total = 0, freq = 1, amp = 1, maxV = 0;
    for (int i = 0; i < octaves; ++i) {
        total += Noise2D(x * freq, y * freq) * amp;
        maxV  += amp;
        amp   *= persistence;
        freq  *= lacunarity;
    }
    return total / maxV;
}

float PerlinNoise::Fractal3D(float x, float y, float z, int octaves, float persistence, float lacunarity) const {
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
// 常量与工具
// ============================================================
namespace {
    constexpr float TEMP_FREQ  = 0.0025f;
    constexpr float HUMID_FREQ = 0.0030f;
    constexpr float CONT_FREQ  = 0.0035f;

    constexpr int OCEAN_FLOOR = 30;
    constexpr int COAST_BASE  = 60;
    constexpr int INLAND_BASE = 70;
    constexpr int MTN_PEAK    = 100;

    constexpr int PLAINS_AMP = 8;
    constexpr int MTN_AMP    = 25;

    constexpr float CONT_OCEAN_MAX = -0.3f;

    float Smoothstep(float a, float b, float t) {
        t = std::clamp((t - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    float Remap(float v, float a, float b, float c, float d) {
        return c + (d - c) * std::clamp((v - a) / (b - a), 0.0f, 1.0f);
    }

    struct SplinePoint { float input; float output; };
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

// ============================================================
// TerrainGenerator 构造
// ============================================================
TerrainGenerator::TerrainGenerator(uint32_t seed, const BlockRegistry& reg) 
    : m_noise(seed), m_registry(reg) 
{
    // ★ 缓存常用 ID
    ID_STONE      = m_registry.Get("mycraft:stone").id;
    ID_DIRT       = m_registry.Get("mycraft:dirt").id;
    ID_GRASS      = m_registry.Get("mycraft:grass_block").id;
    ID_SAND       = m_registry.Get("mycraft:sand").id;
    ID_WATER      = m_registry.Get("mycraft:water").id;
    ID_SNOW       = m_registry.Get("mycraft:snow").id;
    ID_WOOD       = m_registry.Get("mycraft:oak_log").id;
    ID_LEAVES     = m_registry.Get("mycraft:oak_leaves").id;
    ID_TALLGRASS  = m_registry.Get("mycraft:short_grass").id;
    ID_POPPY      = m_registry.Get("mycraft:poppy").id;
    ID_DANDELION  = m_registry.Get("mycraft:dandelion").id;
}

// ============================================================
// 生物群系与地形
// ============================================================
Biome TerrainGenerator::GetBiome(int wx, int wz) const {
    float temp  = m_noise.Fractal2D(wx * TEMP_FREQ  + 1500.0f, wz * TEMP_FREQ  + 1500.0f, 3);
    float humid = m_noise.Fractal2D(wx * HUMID_FREQ + 3000.0f, wz * HUMID_FREQ + 3000.0f, 3);
    float cont  = m_noise.Fractal2D(wx * CONT_FREQ  + 5000.0f, wz * CONT_FREQ  + 5000.0f, 4);

    cont  = std::clamp(cont  * 4.0f, -1.0f, 1.0f);
    temp  = std::clamp(temp  * 1.8f, -1.0f, 1.0f);
    humid = std::clamp(humid * 1.5f, -1.0f, 1.0f);

    return ComputeBiome(temp, humid, cont);
}

Biome TerrainGenerator::ComputeBiome(float temp, float humid, float cont) const {
    if (cont < CONT_OCEAN_MAX)  return Biome::Ocean;
    if (cont < CONT_OCEAN_MAX + 0.1f) return Biome::Beach;

    if (temp >  0.30f && humid < -0.10f) return Biome::Desert;
    if (temp < -0.30f)                    return Biome::Snow;
    if (humid >  0.15f)                   return Biome::Forest;
    if (cont > 0.55f && temp > -0.2f && temp < 0.3f) return Biome::Mountain;

    return Biome::Plains;
}

void TerrainGenerator::ComputeBiomeField(int baseX, int baseZ, std::array<std::array<Biome, 18>, 18>& out) const {
    for (int dx = -1; dx <= 16; ++dx)
        for (int dz = -1; dz <= 16; ++dz)
            out[dx + 1][dz + 1] = GetBiome(baseX + dx, baseZ + dz);
}

int TerrainGenerator::GetHeight(int wx, int wz) const {
    float temp  = m_noise.Fractal2D(wx * TEMP_FREQ  + 1500.0f, wz * TEMP_FREQ  + 1500.0f, 3);
    float humid = m_noise.Fractal2D(wx * HUMID_FREQ + 3000.0f, wz * HUMID_FREQ + 3000.0f, 3);
    float cont  = m_noise.Fractal2D(wx * CONT_FREQ  + 5000.0f, wz * CONT_FREQ  + 5000.0f, 4);

    cont  = std::clamp(cont  * 4.0f, -1.0f, 1.0f);
    temp  = std::clamp(temp  * 1.8f, -1.0f, 1.0f);
    humid = std::clamp(humid * 1.5f, -1.0f, 1.0f);

    Biome biome = ComputeBiome(temp, humid, cont);

    float baseH;
    if (cont < CONT_OCEAN_MAX) {
        baseH = Remap(cont, -1.0f, CONT_OCEAN_MAX, (float)OCEAN_FLOOR, (float)COAST_BASE);
    } else if (cont < 0.6f) {
        baseH = Remap(cont, CONT_OCEAN_MAX, 0.6f, (float)COAST_BASE, (float)INLAND_BASE);
    } else {
        baseH = Remap(cont, 0.6f, 1.0f, (float)INLAND_BASE, (float)MTN_PEAK);
    }

    float mtnFactor = Smoothstep(0.3f, 0.8f, cont);
    float amp = PLAINS_AMP + mtnFactor * (MTN_AMP - PLAINS_AMP);

    float n1 = m_noise.Fractal2D(wx * 0.012f + 100.0f, wz * 0.012f + 100.0f, 5);
    float n2 = m_noise.Fractal2D(wx * 0.04f,           wz * 0.04f,           3);

    float biomeMul = 1.0f;
    if (biome == Biome::Desert || biome == Biome::Snow) biomeMul = 0.6f;
    if (biome == Biome::Ocean)                          biomeMul = 0.3f;

    float hf = baseH + (n1 * 1.4f + n2 * 0.3f) * amp * biomeMul;

    return std::clamp(static_cast<int>(hf), 1, CHUNK_SIZE_Y - 2);
}

int TerrainGenerator::GetSurfaceY(int wx, int wz) const {
    constexpr int STEP = 6;
    constexpr int GX = 20 / STEP + 1;
    constexpr int GY = 256 / STEP + 2;
    constexpr int GZ = 20 / STEP + 1;

    int ccx = wx >> 4;
    int ccz = wz >> 4;
    int baseX = ccx * CHUNK_SIZE_X;
    int baseZ = ccz * CHUNK_SIZE_Z;

    static thread_local float  grid[GX][GY][GZ];
    static thread_local int    cached_ccx = INT_MIN;
    static thread_local int    cached_ccz = INT_MIN;

    if (ccx != cached_ccx || ccz != cached_ccz) {
        cached_ccx = ccx;
        cached_ccz = ccz;
        for (int gx = 0; gx < GX; ++gx) {
            int gwx = baseX - 2 + gx * STEP;
            for (int gz = 0; gz < GZ; ++gz) {
                int gwz = baseZ - 2 + gz * STEP;
                DensityParams p = ComputeParams(gwx, gwz);
                for (int gy = 0; gy < GY; ++gy) {
                    grid[gx][gy][gz] = ComputeDensity(gwx, gy * STEP, gwz, p);
                }
            }
        }
    }

    int lx = wx - baseX;
    int lz = wz - baseZ;

    float fx = (lx + 2) / static_cast<float>(STEP);
    float fz = (lz + 2) / static_cast<float>(STEP);
    int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, GX - 1);
    int z0 = static_cast<int>(fz), z1 = std::min(z0 + 1, GZ - 1);
    float tx = fx - x0, tz = fz - z0;

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    for (int ly = CHUNK_SIZE_Y - 1; ly >= 0; --ly) {
        float fy = ly / static_cast<float>(STEP);
        int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, GY - 1);
        float ty = fy - y0;

        float c00 = lerp(grid[x0][y0][z0], grid[x1][y0][z0], tx);
        float c10 = lerp(grid[x0][y1][z0], grid[x1][y1][z0], tx);
        float c01 = lerp(grid[x0][y0][z1], grid[x1][y0][z1], tx);
        float c11 = lerp(grid[x0][y1][z1], grid[x1][y1][z1], tx);

        float c0 = lerp(c00, c10, ty);
        float c1 = lerp(c01, c11, ty);
        float d  = lerp(c0, c1, tz);

        if (d > 0.0f) return ly;
    }
    return 0;
}

bool TerrainGenerator::IsSolidAt(int wx, int wy, int wz) const {
    int surfaceY = GetHeight(wx, wz);
    if (wy > surfaceY) return false;
    if (wy < 0) return false;
    if (IsCarvedByCave(wx, wy, wz, surfaceY)) return false;
    return true;
}

BlockId TerrainGenerator::ApplySurfaceRule(Biome biome, int wy, int surfaceY, bool nearWater) const {
    if (wy != surfaceY) return ID_DIRT;

    switch (biome) {
        case Biome::Ocean:
        case Biome::Beach:
        case Biome::Desert: 
            return ID_SAND;
        case Biome::Snow:
            return ID_SNOW;
        case Biome::Mountain:
            return (surfaceY > 110) ? ID_STONE : ID_GRASS;
        case Biome::Plains:
        case Biome::Forest:
        default:
            return nearWater ? ID_SAND : ID_GRASS;
    }
}

bool TerrainGenerator::IsCarvedByCave(int wx, int wy, int wz, int surfaceY) const {
    return IsCaveAt(SampleCaveDiff(wx, wy, wz), wy, surfaceY);
}

float TerrainGenerator::SampleCaveDiff(int wx, int wy, int wz) const {
    constexpr float FREQ = 0.010f;
    float n1 = m_noise.Fractal3D(wx * FREQ + 20000.0f, wy * FREQ * 2.0f + 20000.0f, wz * FREQ + 20000.0f, 2);
    float n2 = m_noise.Fractal3D(wx * FREQ + 30000.0f, wy * FREQ * 2.0f + 30000.0f, wz * FREQ + 30000.0f, 2);
    return std::abs(n1 - n2);
}

bool TerrainGenerator::IsCaveAt(float diff, int ly, int surfaceY) const {
    if (ly > surfaceY) return false;
    if (ly < 5) return false;
    if (ly > surfaceY - 4) return false;
    if (surfaceY <= SEA_LEVEL + 1 && ly > surfaceY - 3) return false;

    float depth = static_cast<float>(surfaceY - ly);
    float depthFactor = std::clamp(0.15f + depth / 20.0f, 0.15f, 1.0f);
    return diff < 0.06f * depthFactor;
}

BlockId TerrainGenerator::OreForPosition([[maybe_unused]] int wx, [[maybe_unused]] int wy, [[maybe_unused]] int wz, BlockId base) const {
    // 由于删除了 CoalOre 和 IronOre，这里直接返回石头。
    // 以后可以在此处从 m_registry 获取新的矿物 ID 进行替换。
    return base;
}

Chunk TerrainGenerator::GenerateChunk(int cx, int cz) const {
    Chunk chunk;
    chunk.chunk_x = cx;
    chunk.chunk_z = cz;

    const int baseX = cx * CHUNK_SIZE_X;
    const int baseZ = cz * CHUNK_SIZE_Z;

    constexpr int STEP = 6;
    constexpr int GX = 20 / STEP + 1;
    constexpr int GY = 256 / STEP + 2;
    constexpr int GZ = 20 / STEP + 1;

    static thread_local float grid[GX][GY][GZ];
    static thread_local float caveGrid[GX][GY][GZ];

    for (int gx = 0; gx < GX; ++gx) {
        int wx = baseX - 2 + gx * STEP;
        for (int gz = 0; gz < GZ; ++gz) {
            int wz = baseZ - 2 + gz * STEP;
            DensityParams p = ComputeParams(wx, wz);
            for (int gy = 0; gy < GY; ++gy) {
                int wy = gy * STEP;
                grid[gx][gy][gz]     = ComputeDensity(wx, wy, wz, p);
                caveGrid[gx][gy][gz] = SampleCaveDiff(wx, wy, wz);
            }
        }
    }

    static thread_local std::array<std::array<DensityParams, 16>, 16> params;
    for (int lx = 0; lx < 16; ++lx)
        for (int lz = 0; lz < 16; ++lz)
            params[lx][lz] = ComputeParams(baseX + lx, baseZ + lz);

    static thread_local std::array<std::array<int, 16>, 16> surfaceYs;

    auto trilinear = [&](const float g[GX][GY][GZ], int lx, int ly, int lz) -> float {
        float fx = (lx + 2) / static_cast<float>(STEP);
        float fy = ly       / static_cast<float>(STEP);
        float fz = (lz + 2) / static_cast<float>(STEP);

        int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, GX - 1);
        int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, GY - 1);
        int z0 = static_cast<int>(fz), z1 = std::min(z0 + 1, GZ - 1);

        float tx = fx - x0, ty = fy - y0, tz = fz - z0;
        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

        float c00 = lerp(g[x0][y0][z0], g[x1][y0][z0], tx);
        float c10 = lerp(g[x0][y1][z0], g[x1][y1][z0], tx);
        float c01 = lerp(g[x0][y0][z1], g[x1][y0][z1], tx);
        float c11 = lerp(g[x0][y1][z1], g[x1][y1][z1], tx);

        float c0 = lerp(c00, c10, ty);
        float c1 = lerp(c01, c11, ty);

        return lerp(c0, c1, tz);
    };

    auto sampleDensity = [&](int lx, int ly, int lz) { return trilinear(grid, lx, ly, lz); };

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
                BlockId type;

                if (d > 0.0f) {
                    if (IsCarvedByCave(wx, ly, wz, surfaceY)) {
                        type = AIR_ID;
                    } else if (ly == surfaceY) {
                        type = ApplySurfaceRule(biome, ly, surfaceY, nearWater);
                    } else if (ly >= surfaceY - 3) {
                        switch (biome) {
                            case Biome::Desert:  type = ID_STONE; break; // 原为 Sandstone
                            case Biome::Ocean:   type = ID_SAND; break;
                            default:             type = ID_DIRT; break;
                        }
                    } else {
                        type = ID_STONE;
                        type = OreForPosition(wx, ly, wz, type);
                    }
                } else {
                    type = (ly <= SEA_LEVEL) ? ID_WATER : AIR_ID;

                    if (ly == surfaceY + 1) {
                        BlockId below = ChunkGet(chunk, lx, surfaceY, lz);
                        if (below == ID_GRASS) {
                            uint32_t r = static_cast<uint32_t>(wx * 73856093 ^ wz * 19349663) % 100;
                            if (r < 10) {
                                type = ID_TALLGRASS;
                            } else if (r < 13) {
                                type = ID_POPPY;
                            } else if (r < 16) {
                                type = ID_DANDELION;
                            }
                        }
                    }
                }

                chunk.blocks[ChunkIndex(lx, ly, lz)] = type;
            }
        }
    }

    GenerateTrees(chunk, surfaceYs);
    return chunk;
}

static constexpr SplinePoint kOffsetSpline[] = {
    {-1.0f, -0.35f}, {-0.5f, -0.15f}, {-0.2f, -0.02f},
    { 0.0f,  0.02f}, { 0.3f,  0.10f}, { 0.7f,  0.35f}, { 1.0f,  0.55f},
};
static constexpr size_t kOffsetSplineN = std::size(kOffsetSpline);

static constexpr SplinePoint kFactorSpline[] = {
    {-1.0f, 4.0f}, {-0.5f, 2.5f}, { 0.0f, 1.5f}, { 0.5f, 0.7f}, { 1.0f, 0.3f},
};
static constexpr size_t kFactorSplineN = std::size(kFactorSpline);

TerrainGenerator::DensityParams TerrainGenerator::ComputeParams(int wx, int wz) const {
    constexpr float WARP_FREQ = 0.0055f;
    constexpr float WARP_AMP  = 70.0f;

    float warpX = m_noise.Fractal2D(wx * WARP_FREQ + 11000.0f, wz * WARP_FREQ + 11000.0f, 2) * WARP_AMP;
    float warpZ = m_noise.Fractal2D(wx * WARP_FREQ + 12000.0f, wz * WARP_FREQ + 12000.0f, 2) * WARP_AMP;

    float sx = static_cast<float>(wx) + warpX;
    float sz = static_cast<float>(wz) + warpZ;

    DensityParams p;
    p.temp = std::clamp(m_noise.Fractal2D(sx * TEMP_FREQ  + 1500.0f, sz * TEMP_FREQ  + 1500.0f, 3) * 3.0f, -1.0f, 1.0f);
    p.humid = std::clamp(m_noise.Fractal2D(sx * HUMID_FREQ + 3000.0f, sz * HUMID_FREQ + 3000.0f, 3) * 2.5f, -1.0f, 1.0f);
    p.cont = std::clamp(m_noise.Fractal2D(sx * CONT_FREQ + 5000.0f, sz * CONT_FREQ + 5000.0f, 4) * 4.0f, -1.0f, 1.0f);
    p.erosion = std::clamp(m_noise.Fractal2D(sx * 0.0025f + 7000.0f, sz * 0.0025f + 7000.0f, 3) * 2.5f, -1.0f, 1.0f);
    p.weirdness = m_noise.Fractal2D(sx * 0.0045f + 9000.0f, sz * 0.0045f + 9000.0f, 2);
    return p;
}

float TerrainGenerator::ComputeDensity(int wx, int wy, int wz, const DensityParams& p) const {
    float offset = SampleSpline(kOffsetSpline, kOffsetSplineN, p.cont);
    float factor = SampleSpline(kFactorSpline, kFactorSplineN, p.erosion);

    float targetY = SEA_LEVEL + offset * 64.0f;
    float d = (targetY - static_cast<float>(wy)) / 96.0f * factor;

    float n1 = m_noise.Fractal3D(wx * 0.008f, wy * 0.016f, wz * 0.008f, 3);
    float n2 = m_noise.Fractal3D(wx * 0.030f + 500.0f, wy * 0.060f, wz * 0.030f + 500.0f, 2);
    float n3 = m_noise.Fractal3D(wx * 0.08f, wy * 0.15f, wz * 0.08f, 2);

    d += n1 * 0.115f + n2 * 0.06f + n3 * 0.06;

    if (wy > 220) d -= (wy - 220) * 0.03f;
    if (wy < 15)  d += (15 - wy) * 0.05f;

    return d;
}

Biome TerrainGenerator::BiomeAt(const DensityParams& p, int /*wx*/, int /*wz*/, int surfaceY) const {
    float tempAdj = p.temp - (surfaceY - SEA_LEVEL) * 0.008f;

    if (p.cont < CONT_OCEAN_MAX)   return Biome::Ocean;
    if (surfaceY <= SEA_LEVEL + 1) return Biome::Beach;

    if (surfaceY > 175) return Biome::Snow;
    if (surfaceY > 155) return Biome::Mountain;

    if (tempAdj > 0.20f && p.humid < -0.05f && surfaceY < 110)
        return Biome::Desert;

    if (tempAdj < -0.20f)  return Biome::Snow;
    if (p.humid  >  0.10f) return Biome::Forest;
    if (p.cont   >  0.80f) return Biome::Mountain;
    return Biome::Plains;
}

namespace {
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

bool TerrainGenerator::CanPlaceTree(int /*wx*/, int /*wz*/, int surfaceY, Biome biome) const {
    if (surfaceY < 30) return false;
    if (surfaceY <= SEA_LEVEL) return false;
    if (biome == Biome::Ocean || biome == Biome::Beach) return false;
    if (biome == Biome::Snow) return false;
    if (biome == Biome::Mountain && surfaceY > 130) return false;
    return true;
}

void TerrainGenerator::PlaceTree(Chunk& chunk, int wx, int wy, int wz, int height) const {
    const int baseLX = wx - chunk.chunk_x * CHUNK_SIZE_X;
    const int baseLZ = wz - chunk.chunk_z * CHUNK_SIZE_Z;

    auto put = [&](int dx, int dy, int dz, BlockId type) {
        int lx = baseLX + dx;
        int lz = baseLZ + dz;
        int ly = wy + dy;
        if (lx < 0 || lx >= CHUNK_SIZE_X) return;
        if (lz < 0 || lz >= CHUNK_SIZE_Z) return;
        if (ly < 0 || ly >= CHUNK_SIZE_Y) return;
        auto& b = chunk.blocks[ChunkIndex(lx, ly, lz)];
        if (b == AIR_ID) b = type;
    };

    for (int dy = 0; dy < height; ++dy) {
        put(0, dy, 0, ID_WOOD);
    }

    const int topY = height;
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) {
            if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
            put(dx, topY - 2, dz, ID_LEAVES);
        }

    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) {
            if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
            if (dx == 0 && dz == 0) continue;
            put(dx, topY - 1, dz, ID_LEAVES);
        }

    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            put(dx, topY, dz, ID_LEAVES);
        }

    put( 0, topY + 1,  0, ID_LEAVES);
    put( 1, topY + 1,  0, ID_LEAVES);
    put(-1, topY + 1,  0, ID_LEAVES);
    put( 0, topY + 1,  1, ID_LEAVES);
    put( 0, topY + 1, -1, ID_LEAVES);
}

void TerrainGenerator::GenerateTrees(Chunk& chunk, const std::array<std::array<int, 16>, 16>& surfaceYs) const {
    const int cx = chunk.chunk_x;
    const int cz = chunk.chunk_z;
    const int baseX = cx * CHUNK_SIZE_X;
    const int baseZ = cz * CHUNK_SIZE_Z;

    uint32_t nSeed = TreeHash(cx, cz, -1);
    int count = nSeed % 4;

    for (int i = 0; i < count; ++i) {
        uint32_t h = TreeHash(cx, cz, i);
        int lx = 2 + (h & 0xF) % 12;
        int lz = 2 + ((h >> 8) & 0xF) % 12;
        int height = 4 + ((h >> 16) & 0x3);
        int wx = baseX + lx;
        int wz = baseZ + lz;

        int surfaceY = surfaceYs[lx][lz];
        if (surfaceY <= 0) continue;

        BlockId top = chunk.blocks[ChunkIndex(lx, surfaceY, lz)];
        if (top != ID_GRASS) continue;

        DensityParams p = ComputeParams(wx, wz);
        Biome biome = BiomeAt(p, wx, wz, surfaceY);
        if (!CanPlaceTree(wx, wz, surfaceY, biome)) continue;

        PlaceTree(chunk, wx, surfaceY + 1, wz, height);
    }
}

} // namespace game::server