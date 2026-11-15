#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace game::server {

// ==============================================================================
// Minecraft 主世界地形魔数常量提取 (基于 26.2 官方数据包)
// ==============================================================================
namespace MCNoise {
constexpr float GLOBAL_OFFSET = -0.50375f;
constexpr float BASE_DENSITY_MULTIPLIER = 4.0f;

// depth y_clamped_gradient
constexpr int DEPTH_MIN_Y = -64;
constexpr int DEPTH_MAX_Y = 320;
constexpr float DEPTH_MIN_VALUE = 1.5f;
constexpr float DEPTH_MAX_VALUE = -1.5f;

// base_3d_noise 参数
constexpr float BASE_3D_XZ_SCALE = 0.25f;
constexpr float BASE_3D_Y_SCALE = 0.125f;
constexpr float BASE_3D_XZ_FACTOR = 80.0f;
constexpr float BASE_3D_Y_FACTOR = 160.0f;

// final_density 阈值
constexpr float SURFACE_DENSITY_THRESHOLD = 1.5625f;
constexpr float ENTRANCE_SCALE = 5.0f;

// final_density slide 参数
constexpr int SLIDE_TOP_START_Y = 240;
constexpr int SLIDE_TOP_END_Y = 256;
constexpr float SLIDE_TOP_TARGET = -0.078125f;

constexpr int SLIDE_BOTTOM_START_Y = -64;
constexpr int SLIDE_BOTTOM_END_Y = -40;
constexpr float SLIDE_BOTTOM_TARGET = 0.1171875f;  // 同时也是底部常数
constexpr float SLIDE_MID_BASE = 0.078125f;

// 内部参数
constexpr float CAVE_CHEESE_SCALE = 0.6666666f;
constexpr float CAVE_CHEESE_OFFSET = 0.27f;
constexpr float CAVE_SLOPED_MUL = -0.64f;
constexpr float CAVE_SLOPED_OFFSET = 1.5f;
}  // namespace MCNoise

// ==========================================
// 平台无关 PRNG 与 Fast Math
// ==========================================
class XorShift32 {
    uint32_t m_state;

public:
    explicit XorShift32(uint32_t seed) : m_state(seed ? seed : 0x9E3779B9u) {}
    uint32_t Next() {
        uint32_t x = m_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return m_state = x;
    }
    uint32_t NextBounded(uint32_t bound) {
        uint32_t threshold = (0u - bound) % bound;
        uint32_t r;
        do {
            r = Next();
        } while (r < threshold);
        return r % bound;
    }
};

inline int FastFloor(float x) {
    int i = static_cast<int>(x);
    return i - static_cast<int>(x < static_cast<float>(i));
}
inline float FastAbs(float x) {
    uint32_t bits = std::bit_cast<uint32_t>(x) & 0x7FFFFFFFu;
    return std::bit_cast<float>(bits);
}

struct SplinePoint {
    float input;
    float output;
};

class PerlinNoise {
public:
    explicit PerlinNoise(uint32_t seed);
    float Noise2D(float x, float y) const;
    float Noise3D(float x, float y, float z) const;
    float Fractal2D(float x, float y, int octaves, float persistence = 0.5f, float lacunarity = 2.0f) const;
    float Fractal3D(float x, float y, float z, int octaves, float persistence = 0.5f, float lacunarity = 2.0f) const;

private:
    std::array<int, 512> m_perm{};
    std::array<float, 1024> m_fadeLUT{};
    float Fade(float t) const;
    static float Lerp(float a, float b, float t);
    static float Grad(int hash, float x, float y);
    static float Grad(int hash, float x, float y, float z);
};

// ==========================================
// 密度函数节点
// ==========================================
class DensityFunction {
public:
    virtual ~DensityFunction() = default;
    virtual float Compute(float x, float y, float z) const = 0;
};
using DFPtr = std::shared_ptr<const DensityFunction>;

class Constant : public DensityFunction {
    float m_value;

public:
    explicit Constant(float val) : m_value(val) {}
    float Compute(float, float, float) const override { return m_value; }
};

class Add : public DensityFunction {
    DFPtr m_a, m_b;

public:
    Add(DFPtr a, DFPtr b) : m_a(std::move(a)), m_b(std::move(b)) {}
    float Compute(float x, float y, float z) const override { return m_a->Compute(x, y, z) + m_b->Compute(x, y, z); }
};

class Mul : public DensityFunction {
    DFPtr m_a, m_b;

public:
    Mul(DFPtr a, DFPtr b) : m_a(std::move(a)), m_b(std::move(b)) {}
    float Compute(float x, float y, float z) const override { return m_a->Compute(x, y, z) * m_b->Compute(x, y, z); }
};

class Min : public DensityFunction {
    DFPtr m_a, m_b;

public:
    Min(DFPtr a, DFPtr b) : m_a(std::move(a)), m_b(std::move(b)) {}
    float Compute(float x, float y, float z) const override {
        return std::min(m_a->Compute(x, y, z), m_b->Compute(x, y, z));
    }
};

class Max : public DensityFunction {
    DFPtr m_a, m_b;

public:
    Max(DFPtr a, DFPtr b) : m_a(std::move(a)), m_b(std::move(b)) {}
    float Compute(float x, float y, float z) const override {
        return std::max(m_a->Compute(x, y, z), m_b->Compute(x, y, z));
    }
};

class Clamp : public DensityFunction {
    DFPtr m_input;
    float m_min, m_max;

public:
    Clamp(DFPtr input, float mn, float mx) : m_input(std::move(input)), m_min(mn), m_max(mx) {}
    float Compute(float x, float y, float z) const override {
        return std::clamp(m_input->Compute(x, y, z), m_min, m_max);
    }
};

// 1.5 -> 1.5, -1.5 -> -0.375 (除以4)
class QuarterNegative : public DensityFunction {
    DFPtr m_input;

public:
    explicit QuarterNegative(DFPtr in) : m_input(std::move(in)) {}
    float Compute(float x, float y, float z) const override {
        float val = m_input->Compute(x, y, z);
        return val > 0.0f ? val : val * 0.25f;
    }
};

// 1.0 -> 1.0, -1.0 -> -0.5 (除以2)
class HalfNegative : public DensityFunction {
    DFPtr m_input;

public:
    explicit HalfNegative(DFPtr in) : m_input(std::move(in)) {}
    float Compute(float x, float y, float z) const override {
        float val = m_input->Compute(x, y, z);
        return val > 0.0f ? val : val * 0.5f;
    }
};

// 原版 y_clamped_gradient
class YClampedGradient : public DensityFunction {
    int m_fromY, m_toY;
    float m_fromVal, m_toVal;

public:
    YClampedGradient(int fromY, int toY, float fromVal, float toVal)
        : m_fromY(fromY), m_toY(toY), m_fromVal(fromVal), m_toVal(toVal) {}
    float Compute(float, float y, float) const override {
        if (y <= m_fromY)
            return m_fromVal;
        if (y >= m_toY)
            return m_toVal;
        float t = (y - m_fromY) / static_cast<float>(m_toY - m_fromY);
        return m_fromVal + t * (m_toVal - m_fromVal);
    }
};

// 原版 range_choice
class NoiseDF : public DensityFunction {
    const PerlinNoise& m_noise;
    float m_freqX, m_freqY, m_freqZ;
    int m_octaves;
    DFPtr m_shiftX = nullptr;
    DFPtr m_shiftZ = nullptr;

public:
    NoiseDF(const PerlinNoise& noise, float fx, float fy, float fz, int oct)
        : m_noise(noise), m_freqX(fx), m_freqY(fy), m_freqZ(fz), m_octaves(oct) {}

    void SetShift(DFPtr sx, DFPtr sz) {
        m_shiftX = std::move(sx);
        m_shiftZ = std::move(sz);
    }

    float Compute(float x, float y, float z) const override {
        float sx = m_shiftX ? m_shiftX->Compute(x, y, z) : x;
        float sz = m_shiftZ ? m_shiftZ->Compute(x, y, z) : z;
        return m_noise.Fractal3D(sx * m_freqX, y * m_freqY, sz * m_freqZ, m_octaves);
    }
};

class RangeChoice : public DensityFunction {
    DFPtr m_input;
    DFPtr m_whenInRange;
    DFPtr m_whenOutOfRange;
    float m_min, m_max;

public:
    RangeChoice(DFPtr input, float min, float max, DFPtr whenInRange, DFPtr whenOutOfRange)
        : m_input(std::move(input)),
          m_whenInRange(std::move(whenInRange)),
          m_whenOutOfRange(std::move(whenOutOfRange)),
          m_min(min),
          m_max(max) {}
    float Compute(float x, float y, float z) const override {
        float val = m_input->Compute(x, y, z);
        return (val >= m_min && val < m_max) ? m_whenInRange->Compute(x, y, z) : m_whenOutOfRange->Compute(x, y, z);
    }
};

class SplineDF : public DensityFunction {
    DFPtr m_input;
    std::shared_ptr<const std::vector<SplinePoint>> m_points;

public:
    SplineDF(DFPtr input, std::shared_ptr<const std::vector<SplinePoint>> pts)
        : m_input(std::move(input)), m_points(std::move(pts)) {}
    float Compute(float x, float y, float z) const override {
        const float val = m_input->Compute(x, y, z);
        const auto& pts = *m_points;
        const size_t n = pts.size();
        if (n == 0)
            return 0.0f;
        if (val <= pts[0].input)
            return pts[0].output;
        if (val >= pts[n - 1].input)
            return pts[n - 1].output;
        for (size_t i = 1; i < n; ++i) {
            if (val <= pts[i].input) {
                const float t = (val - pts[i - 1].input) / (pts[i].input - pts[i - 1].input);
                return pts[i - 1].output + t * (pts[i].output - pts[i - 1].output);
            }
        }
        return 0.0f;
    }
};

class LambdaDF : public DensityFunction {
    std::function<float(float, float, float)> m_func;

public:
    explicit LambdaDF(std::function<float(float, float, float)> func) : m_func(std::move(func)) {}
    float Compute(float x, float y, float z) const override { return m_func(x, y, z); }
};

}  // namespace game::server