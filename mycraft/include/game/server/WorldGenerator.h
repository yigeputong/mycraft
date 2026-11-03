#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include "game/core/BlockRegistry.h"
#include "game/core/World.h"
#include "game/server/DensityFunction.h"

namespace game::server {

enum class Biome : uint8_t {
    Ocean, DeepOcean,
    Beach,
    Plains, SunflowerPlains,
    Forest, FlowerForest,
    Desert, Badlands,
    Snow, IceSpikes,
    Mountain, StonyPeaks,
    Jungle, BambooJungle,
    Swamp, MangroveSwamp
};

enum class GenPhase : uint8_t {
    Empty = 0,
    BiomeAndDensity,
    TerrainBlocks,
    Features,
    Lighting,
    Ready
};

struct BiomeRule {
    Biome biome;
    float tempMin, tempMax;
    float humidMin, humidMax;
    float contMin, contMax;
    float erosionMin, erosionMax;
    float weirdnessMin, weirdnessMax;
    float depthMin, depthMax;
};

struct ChunkGenContext {
    int cx = 0, cz = 0;
    int baseX = 0, baseZ = 0;

    GenPhase currentPhase = GenPhase::Empty;
    std::unique_ptr<Chunk> chunk = nullptr;

    struct DensityParams {
        float temp = 0.0f;
        float humid = 0.0f;
        float cont = 0.0f;
        float erosion = 0.0f;
        float weirdness = 0.0f;
        float depth = 0.0f;
    };

    std::array<std::array<DensityParams, 16>, 16> params2D;
    std::array<std::array<int, 16>, 16> surfaceYs;
    std::array<std::array<Biome, 16>, 16> localBiomes;

    DFPtr finalDensity;
    DFPtr caveDensity;

    static constexpr int STEP_XZ = 4;
    static constexpr int STEP_Y  = 8;

    static constexpr int GX = 20 / STEP_XZ + 1;
    static constexpr int GY = 256 / STEP_Y + 2;
    static constexpr int GZ = 20 / STEP_XZ + 1;

    std::array<std::array<std::array<float, GZ>, GY>, GX> grid;
    std::array<std::array<std::array<float, GZ>, GY>, GX> caveGrid;
};

struct WorldGenConfig {
    // ===== 宏观尺寸 =====
    float contFreq           = 0.0004f;
    float oceanOffset        = -0.35f;
    float verticalScale      = 1.2f;
    float erosionScale       = 0.0015f;

    // ===== 生物群系分布 =====
    float tempFreq           = 0.0025f;
    float humidFreq          = 0.0030f;

    // ===== 真实度调节 =====
    float ridgeWeight        = 0.8f;
    float plainsNoise        = 0.015f;

    // ===== 其他保留配置 =====
    float n1FreqXZ           = 0.006f;
    float n1FreqY            = 0.012f;
    float n2FreqXZ           = 0.020f;
    float n2FreqY            = 0.040f;
    float n3FreqXZ           = 0.045f;
    float n3FreqY            = 0.08f;
    float n1Weight           = 0.15f;
    float n2Weight           = 0.08f;
    float n3Weight           = 0.04f;

    float caveFreq           = 0.010f;
    float caveThreshold      = 0.06f;

    int   oceanFloor         = 30;
    int   coastBase          = 60;
    int   inlandBase         = 70;
    int   mtnPeak            = 100;
    float contOceanMax       = -0.3f;
};

class TerrainGenerator {
public:
    explicit TerrainGenerator(uint32_t seed, const BlockRegistry& reg);

    // ==========================================
    // JSON 数据驱动入口（可选；不调用则用默认硬编码算法）
    // ==========================================
    // 一站式加载：{ "config": {...}, "biomes": [...], "density": {...}, "caveDensity": {...} }
    void LoadFromJson(const nlohmann::json& j);
    void LoadConfigFromJson(const nlohmann::json& j);
    void LoadBiomeRulesFromJson(const nlohmann::json& arr);

    // ==========================================
    // 查询接口
    // ==========================================
    Biome    GetBiome(int wx, int wz) const;
    int      GetHeight(int wx, int wz) const;
    bool     IsSolidAt(int wx, int wy, int wz) const;
    int      GetSurfaceY(int wx, int wz) const;

    void LoadBiomeRules(const std::vector<BiomeRule>& rules);

    Chunk GenerateChunk(int cx, int cz) const;

    void GeneratePhase_BiomeAndDensity(ChunkGenContext& ctx) const;
    void GeneratePhase_TerrainBlocks(ChunkGenContext& ctx) const;
    void GeneratePhase_Features(ChunkGenContext& ctx) const;

private:
    PerlinNoise m_noise;
    const BlockRegistry& m_registry;
    WorldGenConfig m_config;

    std::vector<BiomeRule> m_biomeRules;

    BlockId ID_STONE, ID_DIRT, ID_GRASS, ID_SAND, ID_WATER;
    BlockId ID_SNOW, ID_WOOD, ID_LEAVES, ID_TALLGRASS, ID_POPPY, ID_DANDELION;

    DFPtr m_finalDensity;
    DFPtr m_caveDensity;

    // 默认硬编码密度（构造时调用；JSON 加载会覆盖）
    void BuildDefaultDensity();

    Biome    DetermineBiome(const ChunkGenContext::DensityParams& p, int surfaceY) const;
    BlockId  ApplySurfaceRule(Biome biome, int wy, int surfaceY, bool nearWater) const;
    bool     IsCarvedByCave(int wx, int wy, int wz, int surfaceY) const;
    float    SampleCaveDiff(int wx, int wy, int wz) const;
    bool     IsCaveAt(float diff, int ly, int surfaceY) const;
    BlockId  OreForPosition(int wx, int wy, int wz, BlockId base) const;

    ChunkGenContext::DensityParams ComputeParams(int wx, int wz) const;
    float ComputeDensity(int wx, int wy, int wz, const ChunkGenContext::DensityParams& p) const;

    void GenerateTreesForChunk(Chunk& chunk, const std::array<std::array<int, 16>, 16>& surfaceYs, const std::array<std::array<Biome, 16>, 16>& biomes) const;
    bool CanPlaceTree(int wx, int wz, int surfaceY, Biome biome) const;
    void PlaceTree(Chunk& chunk, int wx, int wy, int wz, int height) const;
    uint32_t TreeHash(int cx, int cz, int idx) const;
};

} // namespace game::server