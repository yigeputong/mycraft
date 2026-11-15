#include "game/server/WorldGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <nlohmann/json.hpp>
#include <numeric>
#include <stdexcept>
#include <string_view>

#include "game/server/DensityFunctionJson.h"

namespace game::server {

// ============================================================
// 原版气候参数精确常量与查表矩阵 (基于 OverworldBiomeBuilder 26.2)
// ============================================================
namespace MCBiomeParams {
// 温度 5 带
constexpr float TEMPS[6] = {-1.0f, -0.45f, -0.15f, 0.2f, 0.55f, 1.0f};
// 湿度 5 带
constexpr float HUMIDS[6] = {-1.0f, -0.35f, -0.1f, 0.1f, 0.3f, 1.0f};
// 侵蚀度 7 带
constexpr float EROSIONS[8] = {-1.0f, -0.78f, -0.375f, -0.2225f, 0.05f, 0.45f, 0.55f, 1.0f};

// 大陆性阈值
constexpr float CONT_MUSHROOM = -1.05f;
constexpr float CONT_DEEP_OCEAN = -0.455f;
constexpr float CONT_OCEAN = -0.19f;
constexpr float CONT_COAST = -0.11f;
constexpr float CONT_NEAR_INLAND = 0.03f;
constexpr float CONT_MID_INLAND = 0.3f;

// 怪异度 (Weirdness) 切片阈值
constexpr float W_VALLEY = 0.05f;
constexpr float W_LOW_START = 0.26666668f;
constexpr float W_MID_START = 0.4f;
constexpr float W_HIGH_START = 0.56666666f;
constexpr float W_PEAK_START = 0.7666667f;
constexpr float W_PEAK_END = 0.93333334f;

// 基础群系查表 (行: 温度 0~4, 列: 湿度 0~4)
constexpr Biome MIDDLE_BIOMES[5][5] = {{Biome::Snow, Biome::Snow, Biome::Snow, Biome::Snow, Biome::Snow},
                                       {Biome::Plains, Biome::Plains, Biome::Forest, Biome::Forest, Biome::Forest},
                                       {Biome::Forest, Biome::Plains, Biome::Forest, Biome::Forest, Biome::Forest},
                                       {Biome::Plains, Biome::Plains, Biome::Forest, Biome::Jungle, Biome::Jungle},
                                       {Biome::Desert, Biome::Desert, Biome::Desert, Biome::Desert, Biome::Desert}};

// 高原群系查表
constexpr Biome PLATEAU_BIOMES[5][5] = {
    {Biome::Snow, Biome::Snow, Biome::Snow, Biome::Snow, Biome::Snow},
    {Biome::Plains, Biome::Plains, Biome::Forest, Biome::Forest, Biome::Forest},
    {Biome::Plains, Biome::Plains, Biome::Plains, Biome::Plains, Biome::Forest},
    {Biome::Plains, Biome::Plains, Biome::Forest, Biome::Forest, Biome::Jungle},
    {Biome::Badlands, Biome::Badlands, Biome::Badlands, Biome::Badlands, Biome::Badlands}};

// 破碎/高山群系查表
constexpr Biome SHATTERED_BIOMES[5][5] = {
    {Biome::Mountain, Biome::Mountain, Biome::Mountain, Biome::Mountain, Biome::Mountain},
    {Biome::Mountain, Biome::Mountain, Biome::Mountain, Biome::Mountain, Biome::Mountain},
    {Biome::Mountain, Biome::Mountain, Biome::Mountain, Biome::Mountain, Biome::Mountain},
    {Biome::Mountain, Biome::Mountain, Biome::Mountain, Biome::Mountain, Biome::Mountain},
    {Biome::Mountain, Biome::Mountain, Biome::Mountain, Biome::Mountain, Biome::Mountain}};

// 1. 原版 peaksAndValleys 折返公式（决定山脉与河谷）
inline float PeaksAndValleys(float weirdness) {
    return -(std::abs(std::abs(weirdness) - 0.6666667f) - 0.33333334f) * 3.0f;
}

// 2. 原版 offset 主干点 (根据 TerrainProvider.overworldOffset)
static constexpr SplinePoint kVanillaOffsetSpline[] = {
    {-1.10f, 0.044f}, {-1.02f, -0.2222f}, {-0.51f, -0.2222f},
    {-0.44f, -0.12f}, {-0.18f, -0.12f},   {-0.16f, -0.08f},  // 近似代替 beachSpline
    {-0.10f, -0.02f}, {0.25f, 0.1f},      {1.00f, 0.4f}      // 近似代替 lowSpline 和 highSpline
};

// 3. 原版 factor 主干点
static constexpr SplinePoint kVanillaFactorSpline[] = {{-0.19f, 3.95f}, {-0.15f, 6.25f}, {-0.10f, 5.47f},
                                                       {0.03f, 5.08f},  {0.06f, 4.69f},  {1.00f, 4.69f}};

}  // namespace MCBiomeParams

// ============================================================
// Perlin 噪声（LUT + 平台无关 PRNG 洗牌）
// ============================================================
PerlinNoise::PerlinNoise(uint32_t seed) {
    std::array<int, 256> p;
    std::iota(p.begin(), p.end(), 0);

    XorShift32 rng(seed);
    for (int i = 255; i > 0; --i) {
        int j = static_cast<int>(rng.NextBounded(static_cast<uint32_t>(i + 1)));
        std::swap(p[i], p[j]);
    }

    for (int i = 0; i < 256; ++i) {
        m_perm[i] = p[i];
        m_perm[i + 256] = p[i];
    }

    for (int i = 0; i < 1024; ++i) {
        float t = static_cast<float>(i) / 1023.0f;
        m_fadeLUT[i] = t * t * t * (t * (t * 6 - 15) + 10);
    }
}

float PerlinNoise::Fade(float t) const {
    int idx = static_cast<int>(t * 1023.0f);
    return m_fadeLUT[std::clamp(idx, 0, 1023)];
}

float PerlinNoise::Lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float PerlinNoise::Grad(int hash, float x, float y) {
    static constexpr float lut[8][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const int h = hash & 7;
    return lut[h][0] * x + lut[h][1] * y;
}

float PerlinNoise::Grad(int hash, float x, float y, float z) {
    static constexpr float lut[16][3] = {{1, 1, 0},  {-1, 1, 0},  {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                                         {1, 0, -1}, {-1, 0, -1}, {0, 1, 1},  {0, -1, 1},  {0, 1, -1}, {0, -1, -1},
                                         {1, 1, 0},  {-1, 1, 0},  {1, -1, 0}, {-1, -1, 0}};
    const int h = hash & 15;
    return lut[h][0] * x + lut[h][1] * y + lut[h][2] * z;
}

float PerlinNoise::Noise2D(float x, float y) const {
    const int xf = FastFloor(x);
    const int yf = FastFloor(y);
    const int X = xf & 255;
    const int Y = yf & 255;
    x -= static_cast<float>(xf);
    y -= static_cast<float>(yf);

    const float u = Fade(x), v = Fade(y);
    int aa = m_perm[m_perm[X] + Y], ab = m_perm[m_perm[X] + Y + 1];
    int ba = m_perm[m_perm[X + 1] + Y], bb = m_perm[m_perm[X + 1] + Y + 1];
    return Lerp(Lerp(Grad(aa, x, y), Grad(ba, x - 1, y), u), Lerp(Grad(ab, x, y - 1), Grad(bb, x - 1, y - 1), u), v);
}

float PerlinNoise::Noise3D(float x, float y, float z) const {
    const int xf = FastFloor(x);
    const int yf = FastFloor(y);
    const int zf = FastFloor(z);
    const int X = xf & 255;
    const int Y = yf & 255;
    const int Z = zf & 255;
    x -= static_cast<float>(xf);
    y -= static_cast<float>(yf);
    z -= static_cast<float>(zf);

    const float u = Fade(x), v = Fade(y), w = Fade(z);
    int A = m_perm[X] + Y, AA = m_perm[A] + Z, AB = m_perm[A + 1] + Z;
    int B = m_perm[X + 1] + Y, BA = m_perm[B] + Z, BB = m_perm[B + 1] + Z;
    return Lerp(Lerp(Lerp(Grad(m_perm[AA], x, y, z), Grad(m_perm[BA], x - 1, y, z), u),
                     Lerp(Grad(m_perm[AB], x, y - 1, z), Grad(m_perm[BB], x - 1, y - 1, z), u), v),
                Lerp(Lerp(Grad(m_perm[AA + 1], x, y, z - 1), Grad(m_perm[BA + 1], x - 1, y, z - 1), u),
                     Lerp(Grad(m_perm[AB + 1], x, y - 1, z - 1), Grad(m_perm[BB + 1], x - 1, y - 1, z - 1), u), v),
                w);
}

float PerlinNoise::Fractal2D(float x, float y, int octaves, float persistence, float lacunarity) const {
    float total = 0, freq = 1, amp = 1, maxV = 0;
    for (int i = 0; i < octaves; ++i) {
        total += Noise2D(x * freq, y * freq) * amp;
        maxV += amp;
        amp *= persistence;
        freq *= lacunarity;
    }
    return total / maxV;
}

float PerlinNoise::Fractal3D(float x, float y, float z, int octaves, float persistence, float lacunarity) const {
    float total = 0, freq = 1, amp = 1, maxV = 0;
    for (int i = 0; i < octaves; ++i) {
        total += Noise3D(x * freq, y * freq, z * freq) * amp;
        maxV += amp;
        amp *= persistence;
        freq *= lacunarity;
    }
    return total / maxV;
}

// ============================================================
// 常量与工具
// ============================================================
namespace {
float SampleSpline(const SplinePoint* pts, size_t n, float x) {
    if (x <= pts[0].input)
        return pts[0].output;
    if (x >= pts[n - 1].input)
        return pts[n - 1].output;
    for (size_t i = 1; i < n; ++i) {
        if (x <= pts[i].input) {
            float t = (x - pts[i - 1].input) / (pts[i].input - pts[i - 1].input);
            return pts[i - 1].output + t * (pts[i].output - pts[i - 1].output);
        }
    }
    return 0.0f;
}

float Smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static constexpr SplinePoint kOffsetSpline[] = {{-1.0f, -0.6f}, {-0.4f, -0.2f}, {-0.1f, 0.0f}, {0.0f, 0.05f},
                                                {0.3f, 0.4f},   {0.7f, 1.0f},   {1.0f, 1.6f}};
static constexpr SplinePoint kFactorSpline[] = {{-1.0f, 5.0f}, {-0.5f, 2.0f}, {0.0f, 1.0f}, {0.5f, 0.5f}, {1.0f, 0.2f}};

uint32_t HashCoords(int cx, int cz, int idx) {
    uint32_t h = static_cast<uint32_t>(cx) * 374761393u ^ static_cast<uint32_t>(cz) * 668265263u ^
                 static_cast<uint32_t>(idx) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float SampleGrid3D(const auto& grid, int lx, int ly, int lz) {
    float fx = (lx + 2) / static_cast<float>(ChunkGenContext::STEP_XZ);
    float fy = ly / static_cast<float>(ChunkGenContext::STEP_Y);
    float fz = (lz + 2) / static_cast<float>(ChunkGenContext::STEP_XZ);

    int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, ChunkGenContext::GX - 1);
    int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, ChunkGenContext::GY - 1);
    int z0 = static_cast<int>(fz), z1 = std::min(z0 + 1, ChunkGenContext::GZ - 1);

    float tx = fx - x0, ty = fy - y0, tz = fz - z0;
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    float c00 = lerp(grid[x0][y0][z0], grid[x1][y0][z0], tx);
    float c10 = lerp(grid[x0][y1][z0], grid[x1][y1][z0], tx);
    float c01 = lerp(grid[x0][y0][z1], grid[x1][y0][z1], tx);
    float c11 = lerp(grid[x0][y1][z1], grid[x1][y1][z1], tx);

    float c0 = lerp(c00, c10, ty);
    float c1 = lerp(c01, c11, ty);
    return lerp(c0, c1, tz);
}

Biome BiomeFromString(std::string_view s) {
    if (s == "Ocean")
        return Biome::Ocean;
    if (s == "DeepOcean")
        return Biome::DeepOcean;
    if (s == "Beach")
        return Biome::Beach;
    if (s == "Plains")
        return Biome::Plains;
    if (s == "SunflowerPlains")
        return Biome::SunflowerPlains;
    if (s == "Forest")
        return Biome::Forest;
    if (s == "FlowerForest")
        return Biome::FlowerForest;
    if (s == "Desert")
        return Biome::Desert;
    if (s == "Badlands")
        return Biome::Badlands;
    if (s == "Snow")
        return Biome::Snow;
    if (s == "IceSpikes")
        return Biome::IceSpikes;
    if (s == "Mountain")
        return Biome::Mountain;
    if (s == "StonyPeaks")
        return Biome::StonyPeaks;
    if (s == "Jungle")
        return Biome::Jungle;
    if (s == "BambooJungle")
        return Biome::BambooJungle;
    if (s == "Swamp")
        return Biome::Swamp;
    if (s == "MangroveSwamp")
        return Biome::MangroveSwamp;
    throw std::runtime_error("Unknown biome name: " + std::string(s));
}
}  // namespace

// ============================================================
// TerrainGenerator 构造
// ============================================================
TerrainGenerator::TerrainGenerator(uint32_t seed, const BlockRegistry& reg) : m_noise(seed), m_registry(reg) {
    ID_STONE = m_registry.Get("mycraft:stone").id;
    ID_DIRT = m_registry.Get("mycraft:dirt").id;
    ID_GRASS = m_registry.Get("mycraft:grass_block").id;
    ID_SAND = m_registry.Get("mycraft:sand").id;
    ID_WATER = m_registry.Get("mycraft:water").id;
    ID_SNOW = m_registry.Get("mycraft:snow").id;
    ID_WOOD = m_registry.Get("mycraft:oak_log").id;
    ID_LEAVES = m_registry.Get("mycraft:oak_leaves").id;
    ID_TALLGRASS = m_registry.Get("mycraft:short_grass").id;
    ID_POPPY = m_registry.Get("mycraft:poppy").id;
    ID_DANDELION = m_registry.Get("mycraft:dandelion").id;
    ID_BEDROCK = ID_STONE;
    ID_DEEPSLATE = ID_STONE;
    ID_GRAVEL = ID_STONE;
    ID_RED_SAND = ID_STONE;
    ID_TERRACOTTA = ID_STONE;
    ID_SANDSTONE = ID_STONE;

    BuildDefaultDensity();
}

// 默认硬编码密度（作为 JSON 未加载时的回退路径）
void TerrainGenerator::BuildDefaultDensity() {
    // 1. offset 节点 (大陆性基准)
    auto offsetNode = std::make_shared<LambdaDF>([this](float x, float y, float z) {
        auto p = this->ComputeParams(static_cast<int>(x), static_cast<int>(z));
        return SampleSpline(MCBiomeParams::kVanillaOffsetSpline, std::size(MCBiomeParams::kVanillaOffsetSpline),
                            p.cont);
    });

    // 2. factor 节点 (侵蚀度决定陡峭度)
    auto factorNode = std::make_shared<LambdaDF>([this](float x, float y, float z) {
        auto p = this->ComputeParams(static_cast<int>(x), static_cast<int>(z));
        return SampleSpline(MCBiomeParams::kVanillaFactorSpline, std::size(MCBiomeParams::kVanillaFactorSpline),
                            p.cont);
    });

    // 3. depth = y_gradient + offset
    auto yGrad = std::make_shared<YClampedGradient>(MCNoise::DEPTH_MIN_Y, MCNoise::DEPTH_MAX_Y,
                                                    MCNoise::DEPTH_MIN_VALUE, MCNoise::DEPTH_MAX_VALUE);
    auto depth = std::make_shared<Add>(yGrad, offsetNode);

    // 4. ★ 修复：将 base_3d_noise 改为原版振幅正常的频率
    // 原版的 noise 是非常高频的，0.005 左右才能在区块内产生起伏
    auto base3D = std::make_shared<LambdaDF>([this](float x, float y, float z) {
        float noise = m_noise.Fractal3D(x * 0.005f, y * 0.01f, z * 0.005f, 4);
        return noise * 2.0f;  // 放大振幅
    });

    // 5. ★ 新增：jaggedness (锯齿感节点) - 原版的高山之所以尖锐，全靠它
    auto jaggednessNode = std::make_shared<LambdaDF>([this](float x, float y, float z) {
        auto p = this->ComputeParams(static_cast<int>(x), static_cast<int>(z));
        // 只有在极低侵蚀度 (ero < -0.375) 时，山峰才会有锯齿感
        if (p.erosion < -0.375f) {
            float jNoise = m_noise.Fractal3D(x * 0.05f, y * 0.05f, z * 0.05f, 2);
            return FastAbs(jNoise) * 0.5f * (1.0f - std::abs(p.erosion));  // 侵蚀越低，锯齿越大
        }
        return 0.0f;
    });

    // 6. initial_density = 4.0 * quarter_negative( (depth + jaggedness) * factor )
    auto depthPlusJag = std::make_shared<Add>(depth, jaggednessNode);
    auto depthMulFactor = std::make_shared<Mul>(depthPlusJag, factorNode);
    auto quarterNeg = std::make_shared<QuarterNegative>(depthMulFactor);
    auto four = std::make_shared<Constant>(MCNoise::BASE_DENSITY_MULTIPLIER);
    auto initialDensity = std::make_shared<Mul>(four, quarterNeg);

    // 7. sloped_cheese = initial_density + base_3d_noise
    auto slopedCheese = std::make_shared<Add>(initialDensity, base3D);

    // 8. slide (平滑) & squeeze (挤压)
    auto topGrad = std::make_shared<YClampedGradient>(MCNoise::SLIDE_TOP_START_Y, MCNoise::SLIDE_TOP_END_Y, 1.0f, 0.0f);
    auto bottomGrad =
        std::make_shared<YClampedGradient>(MCNoise::SLIDE_BOTTOM_START_Y, MCNoise::SLIDE_BOTTOM_END_Y, 0.0f, 1.0f);

    auto innerTerm = std::make_shared<Add>(std::make_shared<Constant>(MCNoise::SLIDE_MID_BASE), slopedCheese);
    auto midTerm = std::make_shared<Add>(std::make_shared<Constant>(-MCNoise::SLIDE_TOP_TARGET),
                                         std::make_shared<Mul>(topGrad, innerTerm));
    auto bottomTerm = std::make_shared<Add>(std::make_shared<Constant>(-MCNoise::SLIDE_BOTTOM_TARGET), midTerm);
    auto slideResult = std::make_shared<Add>(std::make_shared<Constant>(MCNoise::SLIDE_BOTTOM_TARGET),
                                             std::make_shared<Mul>(bottomGrad, bottomTerm));

    m_finalDensity = std::make_shared<LambdaDF>([slideResult](float x, float y, float z) {
        float d = slideResult->Compute(x, y, z);
        if (d > 1.0f)
            return 1.0f;
        if (d < -1.0f)
            return -1.0f;
        return d - (d * d * d) / 3.0f;
    });

    m_caveDensity = std::make_shared<LambdaDF>([this](float x, float y, float z) {
        return this->SampleCaveDiff(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z));
    });
}

void TerrainGenerator::LoadBiomeRules(const std::vector<BiomeRule>& rules) {
    m_biomeRules = rules;
}

// ============================================================
// JSON 数据驱动接口
// ============================================================
void TerrainGenerator::LoadConfigFromJson(const nlohmann::json& j) {
    auto getf = [&j](const char* key, float def) { return j.contains(key) ? j.at(key).get<float>() : def; };
    auto geti = [&j](const char* key, int def) { return j.contains(key) ? j.at(key).get<int>() : def; };

    m_config.contFreq = getf("contFreq", m_config.contFreq);
    m_config.oceanOffset = getf("oceanOffset", m_config.oceanOffset);
    m_config.verticalScale = getf("verticalScale", m_config.verticalScale);
    m_config.erosionScale = getf("erosionScale", m_config.erosionScale);
    m_config.tempFreq = getf("tempFreq", m_config.tempFreq);
    m_config.humidFreq = getf("humidFreq", m_config.humidFreq);
    m_config.ridgeWeight = getf("ridgeWeight", m_config.ridgeWeight);
    m_config.plainsNoise = getf("plainsNoise", m_config.plainsNoise);
    m_config.n1FreqXZ = getf("n1FreqXZ", m_config.n1FreqXZ);
    m_config.n1FreqY = getf("n1FreqY", m_config.n1FreqY);
    m_config.n2FreqXZ = getf("n2FreqXZ", m_config.n2FreqXZ);
    m_config.n2FreqY = getf("n2FreqY", m_config.n2FreqY);
    m_config.n3FreqXZ = getf("n3FreqXZ", m_config.n3FreqXZ);
    m_config.n3FreqY = getf("n3FreqY", m_config.n3FreqY);
    m_config.n1Weight = getf("n1Weight", m_config.n1Weight);
    m_config.n2Weight = getf("n2Weight", m_config.n2Weight);
    m_config.n3Weight = getf("n3Weight", m_config.n3Weight);
    m_config.caveFreq = getf("caveFreq", m_config.caveFreq);
    m_config.caveThreshold = getf("caveThreshold", m_config.caveThreshold);
    m_config.oceanFloor = geti("oceanFloor", m_config.oceanFloor);
    m_config.coastBase = geti("coastBase", m_config.coastBase);
    m_config.inlandBase = geti("inlandBase", m_config.inlandBase);
    m_config.mtnPeak = geti("mtnPeak", m_config.mtnPeak);
    m_config.contOceanMax = getf("contOceanMax", m_config.contOceanMax);
}

void TerrainGenerator::LoadBiomeRulesFromJson(const nlohmann::json& arr) {
    m_biomeRules.clear();
    m_biomeRules.reserve(arr.size());
    for (const auto& item : arr) {
        BiomeRule rule{};
        rule.biome = BiomeFromString(item.at("biome").get<std::string>());
        rule.tempMin = item.at("tempMin").get<float>();
        rule.tempMax = item.at("tempMax").get<float>();
        rule.humidMin = item.at("humidMin").get<float>();
        rule.humidMax = item.at("humidMax").get<float>();
        rule.contMin = item.at("contMin").get<float>();
        rule.contMax = item.at("contMax").get<float>();
        rule.erosionMin = item.at("erosionMin").get<float>();
        rule.erosionMax = item.at("erosionMax").get<float>();
        rule.weirdnessMin = item.at("weirdnessMin").get<float>();
        rule.weirdnessMax = item.at("weirdnessMax").get<float>();
        rule.depthMin = item.at("depthMin").get<float>();
        rule.depthMax = item.at("depthMax").get<float>();
        m_biomeRules.push_back(rule);
    }
}

void TerrainGenerator::LoadFromJson(const nlohmann::json& j) {
    if (j.contains("config"))
        LoadConfigFromJson(j.at("config"));
    if (j.contains("biomes"))
        LoadBiomeRulesFromJson(j.at("biomes"));
    if (j.contains("density"))
        m_finalDensity = DensityJsonFactory::Create(j.at("density"), m_noise);
    if (j.contains("caveDensity"))
        m_caveDensity = DensityJsonFactory::Create(j.at("caveDensity"), m_noise);
}

// ============================================================
// 接口查询
// ============================================================
Biome TerrainGenerator::GetBiome(int wx, int wz) const {
    ChunkGenContext::DensityParams p = ComputeParams(wx, wz);
    // 用真实的密度树找地表
    int surfaceY = GetHeight(wx, wz);
    return DetermineBiome(p, surfaceY);
}

int TerrainGenerator::GetHeight(int wx, int wz) const {
    for (int ly = CHUNK_SIZE_Y - 1; ly >= 0; --ly) {
        if (m_finalDensity->Compute(static_cast<float>(wx), static_cast<float>(ly), static_cast<float>(wz)) > 0.0f) {
            return ly;
        }
    }
    return 0;
}

bool TerrainGenerator::IsSolidAt(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_SIZE_Y)
        return false;
    if (m_finalDensity->Compute(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz)) <= 0.0f) {
        return false;
    }
    return !IsCarvedByCave(wx, wy, wz, wy);  // 后面参数补全即可
}

int TerrainGenerator::GetSurfaceY(int wx, int wz) const {
    return GetHeight(wx, wz);
}

// ============================================================
// 核心阶段 1：计算密度网格、2D 参数、群系分布
// ============================================================
void TerrainGenerator::GeneratePhase_BiomeAndDensity(ChunkGenContext& ctx) const {
    ctx.finalDensity = m_finalDensity;
    ctx.caveDensity = m_caveDensity;

    const int baseX = ctx.cx * CHUNK_SIZE_X;
    const int baseZ = ctx.cz * CHUNK_SIZE_Z;
    ctx.baseX = baseX;
    ctx.baseZ = baseZ;

    for (int gx = 0; gx < ChunkGenContext::GX; ++gx) {
        int wx = baseX - 2 + gx * ChunkGenContext::STEP_XZ;
        for (int gz = 0; gz < ChunkGenContext::GZ; ++gz) {
            int wz = baseZ - 2 + gz * ChunkGenContext::STEP_XZ;
            ChunkGenContext::DensityParams p = ComputeParams(wx, wz);
            for (int gy = 0; gy < ChunkGenContext::GY; ++gy) {
                int wy = gy * ChunkGenContext::STEP_Y;
                ctx.grid[gx][gy][gz] = m_finalDensity->Compute(wx, wy, wz);
                ctx.caveGrid[gx][gy][gz] = m_caveDensity->Compute(wx, wy, wz);
            }
        }
    }

    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            ctx.params2D[lx][lz] = ComputeParams(baseX + lx, baseZ + lz);
        }
    }

    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            int surfaceY = 0;
            for (int ly = CHUNK_SIZE_Y - 1; ly >= 0; --ly) {
                if (SampleGrid3D(ctx.grid, lx, ly, lz) > 0.0f) {
                    surfaceY = ly;
                    break;
                }
            }
            ctx.surfaceYs[lx][lz] = surfaceY;
            ctx.localBiomes[lx][lz] = DetermineBiome(ctx.params2D[lx][lz], surfaceY);
        }
    }

    ctx.currentPhase = GenPhase::TerrainBlocks;
}

// ============================================================
// 核心阶段 2：根据密度网格填充方块
// ============================================================
void TerrainGenerator::GeneratePhase_TerrainBlocks(ChunkGenContext& ctx) const {
    if (!ctx.chunk)
        ctx.chunk = std::make_unique<Chunk>();
    Chunk& chunk = *ctx.chunk;
    chunk.chunk_x = ctx.cx;
    chunk.chunk_z = ctx.cz;

    static thread_local float colDensity[CHUNK_SIZE_Y];
    static thread_local float colCave[CHUNK_SIZE_Y];

    for (int lx = 0; lx < CHUNK_SIZE_X; ++lx) {
        for (int lz = 0; lz < CHUNK_SIZE_Z; ++lz) {
            const int wx = ctx.baseX + lx;
            const int wz = ctx.baseZ + lz;

            for (int ly = 0; ly < CHUNK_SIZE_Y; ++ly) {
                colDensity[ly] = SampleGrid3D(ctx.grid, lx, ly, lz);
                colCave[ly] = SampleGrid3D(ctx.caveGrid, lx, ly, lz);
            }

            int surfaceY = ctx.surfaceYs[lx][lz];
            Biome biome = ctx.localBiomes[lx][lz];
            bool nearWater = (surfaceY <= SEA_LEVEL + 1);

            for (int ly = 0; ly < CHUNK_SIZE_Y; ++ly) {
                float d = colDensity[ly];
                BlockId type = AIR_ID;

                if (d > 0.0f) {
                    bool isCave = false;
                    if (ly > 5 && ly <= surfaceY - 4 && !(surfaceY <= SEA_LEVEL + 1 && ly > surfaceY - 3)) {
                        float depth = static_cast<float>(surfaceY - ly);
                        float depthFactor = std::clamp(0.15f + depth / 20.0f, 0.15f, 1.0f);
                        if (colCave[ly] < 0.06f * depthFactor)
                            isCave = true;
                    }

                    if (isCave) {
                        type = AIR_ID;
                    } else {
                        // ★ 1. 生成基岩层 (最底部，修复了 wy 为 ly)
                        if (ly == 0 || (ly <= 3 && (wx * 31 + ly * 17 + wz * 13) % 100 < ly * 25)) {
                            type = ID_BEDROCK;
                        }
                        // ★ 2. 深板岩层 (原版在 y=0~8 渐变)
                        else if (ly < 8 && (wx * 73 + ly * 37 + wz * 91) % 100 > ly * 12) {
                            type = ID_DEEPSLATE;
                        }
                        // ★ 3. 常规石头层 (深层)
                        else if (ly < surfaceY - 4) {
                            type = ID_STONE;
                            type = OreForPosition(wx, ly, wz, type);
                        }
                        // ★ 4. 次表层 (地表下方 1~4 格)
                        else if (ly < surfaceY) {
                            type = ApplySubSurfaceRule(biome, surfaceY, ly, nearWater);
                        }
                        // ★ 5. 最表层 (地表)
                        else if (ly == surfaceY) {
                            type = ApplySurfaceRule(biome, surfaceY, nearWater);

                            // 陡峭/高山露石判定
                            if (ly + 1 < CHUNK_SIZE_Y && ly > SEA_LEVEL && colDensity[ly + 1] > 0.0f &&
                                colDensity[ly + 1] < 1.5f) {
                                type = ID_STONE;
                            }
                        }
                    }
                } else {
                    if (ly <= SEA_LEVEL) {
                        type = ID_WATER;
                    } else if (ly == surfaceY + 1) {
                        BlockId below = ChunkGet(chunk, lx, surfaceY, lz);
                        if (below == ID_GRASS) {
                            uint32_t r = static_cast<uint32_t>(wx * 73856093 ^ wz * 19349663) % 100;
                            if (r < 10)
                                type = ID_TALLGRASS;
                            else if (r < 13)
                                type = ID_POPPY;
                            else if (r < 16)
                                type = ID_DANDELION;
                        }
                    }
                }
                chunk.blocks[ChunkIndex(lx, ly, lz)] = type;
            }
        }
    }
    ctx.currentPhase = GenPhase::Features;
}

void TerrainGenerator::GeneratePhase_Features(ChunkGenContext& ctx) const {
    if (!ctx.chunk)
        return;
    GenerateTreesForChunk(*ctx.chunk, ctx.surfaceYs, ctx.localBiomes);
    ctx.currentPhase = GenPhase::Lighting;
}

Chunk TerrainGenerator::GenerateChunk(int cx, int cz) const {
    ChunkGenContext ctx;
    ctx.cx = cx;
    ctx.cz = cz;
    GeneratePhase_BiomeAndDensity(ctx);
    GeneratePhase_TerrainBlocks(ctx);
    GeneratePhase_Features(ctx);
    return *ctx.chunk;
}

// ============================================================
// 内部实现：群系判定与地形参数
// ============================================================
ChunkGenContext::DensityParams TerrainGenerator::ComputeParams(int wx, int wz) const {
    constexpr float WARP_FREQ = 0.0055f;
    constexpr float WARP_AMP = 70.0f;

    float warpX = m_noise.Fractal2D(wx * WARP_FREQ + 11000.0f, wz * WARP_FREQ + 11000.0f, 2) * WARP_AMP;
    float warpZ = m_noise.Fractal2D(wx * WARP_FREQ + 12000.0f, wz * WARP_FREQ + 12000.0f, 2) * WARP_AMP;

    float sx = static_cast<float>(wx) + warpX;
    float sz = static_cast<float>(wz) + warpZ;

    ChunkGenContext::DensityParams p;
    p.temp = std::clamp(m_noise.Fractal2D(sx * m_config.tempFreq + 1500.0f, sz * m_config.tempFreq + 1500.0f, 3) * 3.0f,
                        -1.0f, 1.0f);
    p.humid = std::clamp(
        m_noise.Fractal2D(sx * m_config.humidFreq + 3000.0f, sz * m_config.humidFreq + 3000.0f, 3) * 2.5f, -1.0f, 1.0f);

    // ★ 关键修复：限制 cont 的范围，防止样条极值把地形抬飞
    float rawCont = m_noise.Fractal2D(sx * m_config.contFreq + 5000.0f, sz * m_config.contFreq + 5000.0f, 4) * 4.0f;
    rawCont += m_config.oceanOffset;
    p.cont = std::clamp(rawCont, -1.0f, 1.0f);

    p.erosion = std::clamp(
        m_noise.Fractal2D(sx * m_config.erosionScale + 7000.0f, sz * m_config.erosionScale + 7000.0f, 3) * 2.5f, -1.0f,
        1.0f);
    p.weirdness = m_noise.Fractal2D(sx * 0.0045f + 9000.0f, sz * 0.0045f + 9000.0f, 2);
    p.depth = 0.0f;
    return p;
}

float TerrainGenerator::ComputeDensity(int wx, int wy, int wz, const ChunkGenContext::DensityParams& p) const {
    float offset = SampleSpline(kOffsetSpline, std::size(kOffsetSpline), p.cont);
    float factor = SampleSpline(kFactorSpline, std::size(kFactorSpline), p.erosion);

    float targetY = SEA_LEVEL + offset * 64.0f;
    float d = (targetY - static_cast<float>(wy)) / 96.0f * factor;

    float mountainMask = Smoothstep(0.5f, -0.2f, p.erosion) * Smoothstep(-0.2f, 0.4f, p.cont);
    float ridge = m_noise.Fractal3D(wx * 0.008f, wy * 0.016f, wz * 0.008f, 4);
    ridge = 1.0f - FastAbs(ridge);
    ridge = ridge * ridge;
    float plains = m_noise.Fractal3D(wx * 0.02f, wy * 0.04f, wz * 0.02f, 2) * m_config.plainsNoise;

    d += ridge * m_config.ridgeWeight * mountainMask;
    d += plains * (1.0f - mountainMask);
    d *= m_config.verticalScale;
    d -= std::max(0.0f, static_cast<float>(wy - 240)) * 0.15f;
    d += std::max(0.0f, static_cast<float>(10 - wy)) * 0.15f;
    return d;
}

// ============================================================
// 核心：复刻原版 OverworldBiomeBuilder 判定逻辑
// ============================================================
// 在 WorldGenerator.cpp 中

Biome TerrainGenerator::DetermineBiome(const ChunkGenContext::DensityParams& p, int surfaceY) const {
    using namespace MCBiomeParams;

    auto getIndex = [](float val, const float* thresholds, int count) {
        for (int i = 0; i < count; ++i) {
            if (val < thresholds[i + 1])
                return i;
        }
        return count - 1;
    };

    int t = getIndex(p.temp, TEMPS, 5);
    int h = getIndex(p.humid, HUMIDS, 5);
    int ero = getIndex(p.erosion, EROSIONS, 7);
    float w = p.weirdness;

    // 1. 离岸群系 (海洋、蘑菇岛)
    if (p.cont < CONT_MUSHROOM)
        return Biome::Swamp;  // 原版蘑菇岛暂用沼泽代替
    if (p.cont < CONT_DEEP_OCEAN) {
        if (t == 0)
            return Biome::Snow;  // 冰冻深海
        return Biome::DeepOcean;
    }
    if (p.cont < CONT_OCEAN) {
        if (t == 0)
            return Biome::Snow;  // 冰冻海洋
        return Biome::Ocean;
    }

    // 2. 内陆群系切片 (按照 Weirdness 划分层级)
    // 辅助查表函数
    auto pickMiddle = [&](int ti, int hi) -> Biome { return MIDDLE_BIOMES[ti][hi]; };
    auto pickPlateau = [&](int ti, int hi) -> Biome { return PLATEAU_BIOMES[ti][hi]; };
    auto pickShattered = [&](int ti, int hi) -> Biome { return SHATTERED_BIOMES[ti][hi]; };
    auto pickPeak = [&](int ti, int hi) -> Biome {
        if (ti <= 2)
            return (w < 0) ? Biome::Snow : Biome::StonyPeaks;  // Jagged/Frozen peaks
        if (ti == 3)
            return Biome::StonyPeaks;
        return Biome::Badlands;
    };
    auto pickSlope = [&](int ti, int hi) -> Biome {
        if (ti >= 3)
            return pickPlateau(ti, hi);
        return Biome::Mountain;  // 原版是 Snowy Slopes / Grove
    };
    auto pickBeach = [&](int ti) -> Biome {
        if (ti == 0)
            return Biome::Snow;
        if (ti == 4)
            return Biome::Desert;
        return Biome::Beach;
    };

    // --- 切片边界判定 ---
    float absW = FastAbs(w);

    // Valleys (河流) [-0.05, 0.05]
    if (absW < 0.05f) {
        if (p.cont < CONT_NEAR_INLAND)
            return (t == 0) ? Biome::Snow : Biome::Ocean;  // 原版 River
        if (p.cont < CONT_MID_INLAND) {
            if (t >= 1 && t <= 2)
                return Biome::Swamp;
            if (t >= 3)
                return Biome::MangroveSwamp;
        }
        return pickMiddle(t, h);
    }

    // Low Slice [0.05, 0.266] & [-0.266, -0.05]
    if (absW < 0.2666f) {
        if (p.cont >= CONT_NEAR_INLAND && p.cont < CONT_MID_INLAND) {
            if (ero <= 1)
                return (t == 4) ? Biome::Badlands : pickMiddle(t, h);
        }
        if (p.cont >= CONT_MID_INLAND) {
            if (ero <= 1)
                return pickSlope(t, h);
            if (ero <= 3)
                return (t == 4) ? Biome::Badlands : pickMiddle(t, h);
            if (ero == 4)
                return pickMiddle(t, h);
        }
        if (p.cont >= CONT_COAST && p.cont < CONT_NEAR_INLAND) {
            if (ero <= 1)
                return pickMiddle(t, h);
            if (ero == 3 || ero == 4)
                return pickBeach(t);
        }
        return pickMiddle(t, h);
    }

    // Mid Slice [0.266, 0.4] & [-0.4, -0.266] (同时也处理极端两极 [-1, -0.933], [0.933, 1])
    if (absW < 0.4f || absW > 0.933f) {
        if (p.cont < CONT_NEAR_INLAND) {
            if (ero <= 2)
                return Biome::Mountain;  // 原版 Stony Shore
        }
        if (p.cont >= CONT_NEAR_INLAND && p.cont < CONT_MID_INLAND) {
            if (ero <= 1)
                return pickSlope(t, h);
            if (ero <= 2)
                return pickMiddle(t, h);
            if (ero == 3)
                return pickMiddle(t, h);
            if (ero == 4)
                return pickBeach(t);
            if (ero == 5 || ero == 6)
                return (w < 0) ? pickBeach(t) : pickMiddle(t, h);
        }
        if (p.cont >= CONT_MID_INLAND) {
            if (ero <= 1)
                return (t == 0) ? pickSlope(t, h) : pickPlateau(t, h);
            if (ero == 2)
                return pickPlateau(t, h);
            if (ero == 3)
                return (t == 4) ? Biome::Badlands : pickMiddle(t, h);
            if (ero == 4 || ero == 5)
                return pickShattered(t, h);
            if (ero == 6 && t == 0)
                return pickMiddle(t, h);
        }
        return pickMiddle(t, h);
    }

    // High Slice [0.4, 0.566] & [-0.566, -0.4] (同时也处理 [-0.933, -0.766], [0.766, 0.933])
    if (absW < 0.566f || (absW >= 0.766f && absW <= 0.933f)) {
        if (p.cont < CONT_NEAR_INLAND) {
            if (ero <= 1)
                return pickMiddle(t, h);
        }
        if (p.cont >= CONT_NEAR_INLAND && p.cont < CONT_MID_INLAND) {
            if (ero == 0)
                return pickSlope(t, h);
            if (ero == 1)
                return pickSlope(t, h);
            if (ero == 2 || ero == 3)
                return pickMiddle(t, h);
            if (ero == 4)
                return pickMiddle(t, h);
            if (ero == 5)
                return (w < 0) ? Biome::Mountain : pickMiddle(t, h);  // Windswept Savanna
        }
        if (p.cont >= CONT_MID_INLAND) {
            if (ero == 0)
                return pickPeak(t, h);
            if (ero == 1)
                return pickSlope(t, h);
            if (ero == 2)
                return pickPlateau(t, h);
            if (ero == 3)
                return pickPlateau(t, h);
            if (ero == 4)
                return pickMiddle(t, h);
            if (ero == 5)
                return pickShattered(t, h);
        }
        return pickMiddle(t, h);
    }

    // Peaks [0.566, 0.766] & [-0.766, -0.566]
    if (absW <= 0.766f) {
        if (p.cont < CONT_NEAR_INLAND) {
            if (ero == 0)
                return pickPeak(t, h);
        }
        if (p.cont >= CONT_NEAR_INLAND && p.cont < CONT_MID_INLAND) {
            if (ero == 0 || ero == 1)
                return pickSlope(t, h);
            if (ero == 2 || ero == 3)
                return pickMiddle(t, h);
            if (ero == 4)
                return pickMiddle(t, h);
            if (ero == 5)
                return (w < 0) ? Biome::Mountain : pickShattered(t, h);
        }
        if (p.cont >= CONT_MID_INLAND) {
            if (ero == 0 || ero == 1)
                return pickPeak(t, h);
            if (ero == 2)
                return pickPlateau(t, h);
            if (ero == 3)
                return pickPlateau(t, h);
            if (ero == 4)
                return pickMiddle(t, h);
            if (ero == 5)
                return pickShattered(t, h);
        }
        return pickMiddle(t, h);
    }

    return pickMiddle(t, h);
}

// 决定最表层方块 (Top Layer)
BlockId TerrainGenerator::ApplySurfaceRule(Biome biome, int surfaceY, bool nearWater) const {
    // 1. 水下规则
    if (nearWater && surfaceY <= SEA_LEVEL) {
        if (biome == Biome::Ocean || biome == Biome::DeepOcean) {
            // 深海底部：沙砾或沙子
            return (surfaceY < SEA_LEVEL - 10) ? ID_GRAVEL : ID_SAND;
        }
        if (biome == Biome::Swamp || biome == Biome::MangroveSwamp) {
            return ID_DIRT;  // 沼泽底部
        }
        if (biome == Biome::Mountain)
            return ID_GRAVEL;
        return ID_SAND;  // 沙滩/浅水
    }

    // 2. 高山/极寒规则
    if (biome == Biome::Mountain || biome == Biome::StonyPeaks) {
        if (surfaceY > 140)
            return ID_SNOW;
        if (surfaceY > 110)
            return ID_STONE;
        return ID_GRASS;
    }
    if (biome == Biome::Snow || biome == Biome::IceSpikes) {
        return ID_SNOW;  // 积雪平原
    }

    // 3. 炎热干旱群系
    if (biome == Biome::Desert || biome == Biome::Beach) {
        return ID_SAND;
    }
    if (biome == Biome::Badlands) {
        return ID_RED_SAND;  // 恶地
    }

    // 4. 默认温带群系 (平原、森林、丛林)
    if (nearWater)
        return ID_SAND;
    return ID_GRASS;
}

// 决定次表层方块 (Sub-Surface Layer)
BlockId TerrainGenerator::ApplySubSurfaceRule(Biome biome, int surfaceY, int currentY, bool nearWater) const {
    int depth = surfaceY - currentY;  // 距离地表几格

    if (biome == Biome::Ocean || biome == Biome::DeepOcean || biome == Biome::Beach) {
        return ID_SAND;
    }
    if (biome == Biome::Desert) {
        return (depth <= 3) ? ID_SAND : ID_SANDSTONE;  // 沙漠下面有砂岩
    }
    if (biome == Biome::Badlands) {
        return ID_TERRACOTTA;  // 恶地下面是陶瓦
    }
    if (biome == Biome::Mountain || biome == Biome::StonyPeaks) {
        return (surfaceY > 110) ? ID_STONE : ID_DIRT;  // 高山露石，低海拔有泥土
    }

    // 默认情况 (草方块下面)
    return ID_DIRT;
}

bool TerrainGenerator::IsCarvedByCave(int wx, int wy, int wz, int surfaceY) const {
    return IsCaveAt(SampleCaveDiff(wx, wy, wz), wy, surfaceY);
}

float TerrainGenerator::SampleCaveDiff(int wx, int wy, int wz) const {
    float n1 = m_noise.Fractal3D(wx * m_config.caveFreq + 20000.0f, wy * m_config.caveFreq * 2.0f + 20000.0f,
                                 wz * m_config.caveFreq + 20000.0f, 2);
    float n2 = m_noise.Fractal3D(wx * m_config.caveFreq + 30000.0f, wy * m_config.caveFreq * 2.0f + 30000.0f,
                                 wz * m_config.caveFreq + 30000.0f, 2);
    return FastAbs(n1 - n2);
}

bool TerrainGenerator::IsCaveAt(float diff, int ly, int surfaceY) const {
    if (ly > surfaceY)
        return false;
    if (ly < 5)
        return false;
    if (ly > surfaceY - 4)
        return false;
    if (surfaceY <= SEA_LEVEL + 1 && ly > surfaceY - 3)
        return false;

    float depth = static_cast<float>(surfaceY - ly);
    float depthFactor = std::clamp(0.15f + depth / 20.0f, 0.15f, 1.0f);
    return diff < m_config.caveThreshold * depthFactor;
}

BlockId TerrainGenerator::OreForPosition([[maybe_unused]] int wx, [[maybe_unused]] int wy, [[maybe_unused]] int wz,
                                         BlockId base) const {
    return base;
}

uint32_t TerrainGenerator::TreeHash(int cx, int cz, int idx) const {
    return HashCoords(cx, cz, idx);
}

bool TerrainGenerator::CanPlaceTree(int /*wx*/, int /*wz*/, int surfaceY, Biome biome) const {
    if (surfaceY < 30)
        return false;
    if (surfaceY <= SEA_LEVEL)
        return false;
    if (biome == Biome::Ocean || biome == Biome::DeepOcean || biome == Biome::Beach)
        return false;
    if (biome == Biome::Snow || biome == Biome::IceSpikes || biome == Biome::Desert)
        return false;
    if (biome == Biome::Mountain && surfaceY > 130)
        return false;
    return true;
}

void TerrainGenerator::PlaceTree(Chunk& chunk, int wx, int wy, int wz, int height) const {
    const int baseLX = wx - chunk.chunk_x * CHUNK_SIZE_X;
    const int baseLZ = wz - chunk.chunk_z * CHUNK_SIZE_Z;

    auto put = [&](int dx, int dy, int dz, BlockId type) {
        int lx = baseLX + dx;
        int lz = baseLZ + dz;
        int ly = wy + dy;
        if (lx < 0 || lx >= CHUNK_SIZE_X)
            return;
        if (lz < 0 || lz >= CHUNK_SIZE_Z)
            return;
        if (ly < 0 || ly >= CHUNK_SIZE_Y)
            return;
        auto& b = chunk.blocks[ChunkIndex(lx, ly, lz)];
        if (b == AIR_ID)
            b = type;
    };

    for (int dy = 0; dy < height; ++dy)
        put(0, dy, 0, ID_WOOD);

    const int topY = height;
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) {
            if (std::abs(dx) == 2 && std::abs(dz) == 2)
                continue;
            put(dx, topY - 2, dz, ID_LEAVES);
        }
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) {
            if (std::abs(dx) == 2 && std::abs(dz) == 2)
                continue;
            if (dx == 0 && dz == 0)
                continue;
            put(dx, topY - 1, dz, ID_LEAVES);
        }
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0)
                continue;
            put(dx, topY, dz, ID_LEAVES);
        }
    put(0, topY + 1, 0, ID_LEAVES);
    put(1, topY + 1, 0, ID_LEAVES);
    put(-1, topY + 1, 0, ID_LEAVES);
    put(0, topY + 1, 1, ID_LEAVES);
    put(0, topY + 1, -1, ID_LEAVES);
}

void TerrainGenerator::GenerateTreesForChunk(Chunk& chunk, const std::array<std::array<int, 16>, 16>& surfaceYs,
                                             const std::array<std::array<Biome, 16>, 16>& biomes) const {
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
        if (surfaceY <= 0)
            continue;

        BlockId top = chunk.blocks[ChunkIndex(lx, surfaceY, lz)];
        if (top != ID_GRASS)
            continue;

        Biome biome = biomes[lx][lz];
        if (!CanPlaceTree(wx, wz, surfaceY, biome))
            continue;

        PlaceTree(chunk, wx, surfaceY + 1, wz, height);
    }
}

}  // namespace game::server