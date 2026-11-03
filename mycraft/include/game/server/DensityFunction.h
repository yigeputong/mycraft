#pragma once
#include <memory>
#include <algorithm>
#include <array>
#include <functional>
#include <cstdint>
#include <vector>
#include <bit>       // std::bit_cast

namespace game::server {

// ==========================================
// 平台无关 PRNG（铁律 1：世界种子必须跨端一致）
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
        m_state = x;
        return x;
    }

    // [0, bound)，拒绝采样消除模偏
    uint32_t NextBounded(uint32_t bound) {
        uint32_t threshold = (0u - bound) % bound;
        uint32_t r;
        do { r = Next(); } while (r < threshold);
        return r % bound;
    }
};

// ==========================================
// Fast math（热路径，避免 cmath 边缘开销）
// ==========================================
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
// 密度函数接口与节点
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

class Clamp : public DensityFunction {
    DFPtr m_input;
    float m_min, m_max;
public:
    Clamp(DFPtr input, float mn, float mx) : m_input(std::move(input)), m_min(mn), m_max(mx) {}
    float Compute(float x, float y, float z) const override { return std::clamp(m_input->Compute(x, y, z), m_min, m_max); }
};

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

// ★ 修复：由 shared_ptr 持有 points，杜绝外部 vector 销毁导致的悬空指针
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
        if (n == 0) return 0.0f;
        if (val <= pts[0].input) return pts[0].output;
        if (val >= pts[n - 1].input) return pts[n - 1].output;
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

} // namespace game::server