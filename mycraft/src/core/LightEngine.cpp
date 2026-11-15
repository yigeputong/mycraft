#include "game/core/LightEngine.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "game/core/World.h"

namespace game {

namespace {
constexpr int Y_STRIDE = CHUNK_SIZE_X * CHUNK_SIZE_Z;
constexpr int Z_STRIDE = CHUNK_SIZE_X;

inline uint16_t PackCoord(int x, int y, int z) {
    return static_cast<uint16_t>(x | (z << 4) | (y << 8));
}

inline int GetX(uint16_t idx) {
    return idx & 0x0F;
}
inline int GetZ(uint16_t idx) {
    return (idx >> 4) & 0x0F;
}
inline int GetY(uint16_t idx) {
    return (idx >> 8) & 0xFF;
}

struct LightNode {
    uint16_t index;
    uint8_t r, g, b;
};

thread_local std::vector<LightNode> s_skyQueue;
thread_local std::vector<LightNode> s_blockQueue;

constexpr int DX[6] = {1, -1, 0, 0, 0, 0};
constexpr int DY[6] = {0, 0, 1, -1, 0, 0};
constexpr int DZ[6] = {0, 0, 0, 0, 1, -1};

inline bool IsOutOfBounds(int x, int y, int z) {
    return x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z;
}
}  // namespace

void LightEngine::InitRegion(const BlockRegistry& reg, Chunk& c, Chunk* /*nXm*/, Chunk* /*nXp*/, Chunk* /*nZm*/,
                             Chunk* /*nZp*/) {
    std::fill(c.sky.begin(), c.sky.end(), 0);
    std::fill(c.block.begin(), c.block.end(), LightRGB{0, 0, 0});

    std::span<uint8_t> skySpan(c.sky);
    std::span<LightRGB> blockSpan(c.block);
    std::span<const BlockId> blocksSpan(c.blocks);

    s_skyQueue.clear();
    s_blockQueue.clear();

    // 1. 初始扫描：收集光源，并给所有暴露在天空下的方块天光 15
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            bool skyBlocked = false;

            for (int y = CHUNK_SIZE_Y - 1; y >= 0; --y) {
                int idx = y * Y_STRIDE + z * Z_STRIDE + x;
                const auto& def = reg.Get(blocksSpan[idx]);

                // 收集光源
                if (def.render.luminance > 0) {
                    uint8_t lum = static_cast<uint8_t>(def.render.luminance);
                    uint8_t r = lum;
                    uint8_t g = static_cast<uint8_t>((float)lum * (10.0f / 14.0f));
                    uint8_t b = static_cast<uint8_t>((float)lum * (6.0f / 14.0f));
                    blockSpan[idx] = {r, g, b};
                    s_blockQueue.push_back({PackCoord(x, y, z), r, g, b});
                }

                if (def.render.isOpaque) {
                    skyBlocked = true;
                }

                // 如果没被挡住，这里就是直射天光，满值 15，并且入队准备向四周扩散
                if (!skyBlocked) {
                    skySpan[idx] = 15;
                    s_skyQueue.push_back({PackCoord(x, y, z), 15, 0, 0});
                }
            }
        }
    }

    // 2. 方块光（彩色光）BFS 扩散
    size_t bHead = 0;
    while (bHead < s_blockQueue.size()) {
        LightNode n = s_blockQueue[bHead++];
        int nx = GetX(n.index);
        int ny = GetY(n.index);
        int nz = GetZ(n.index);

        for (int i = 0; i < 6; ++i) {
            int tx = nx + DX[i], ty = ny + DY[i], tz = nz + DZ[i];
            if (IsOutOfBounds(tx, ty, tz))
                continue;

            int tidx = ty * Y_STRIDE + tz * Z_STRIDE + tx;
            if (reg.Get(blocksSpan[tidx]).render.isOpaque)
                continue;

            LightRGB cur = blockSpan[tidx];
            LightRGB next = cur;
            bool updated = false;

            if (n.r > 0 && cur.r < n.r - 1) {
                next.r = n.r - 1;
                updated = true;
            }
            if (n.g > 0 && cur.g < n.g - 1) {
                next.g = n.g - 1;
                updated = true;
            }
            if (n.b > 0 && cur.b < n.b - 1) {
                next.b = n.b - 1;
                updated = true;
            }

            if (updated) {
                blockSpan[tidx] = next;
                s_blockQueue.push_back({PackCoord(tx, ty, tz), next.r, next.g, next.b});
            }
        }
    }

    // 3. 天光 BFS 扩散
    size_t sHead = 0;
    while (sHead < s_skyQueue.size()) {
        LightNode n = s_skyQueue[sHead++];
        int nx = GetX(n.index);
        int ny = GetY(n.index);
        int nz = GetZ(n.index);

        for (int i = 0; i < 6; ++i) {
            int tx = nx + DX[i], ty = ny + DY[i], tz = nz + DZ[i];
            if (IsOutOfBounds(tx, ty, tz))
                continue;

            int tidx = ty * Y_STRIDE + tz * Z_STRIDE + tx;
            if (reg.Get(blocksSpan[tidx]).render.isOpaque)
                continue;

            // 原版机制：垂直向下（DY == -1）如果当前光照为 15，则下方不衰减；否则向四周扩散衰减 1
            uint8_t nextSky = n.r - 1;
            if (DY[i] == -1 && n.r == 15) {
                nextSky = 15;
            }

            if (nextSky > 0 && skySpan[tidx] < nextSky) {
                skySpan[tidx] = nextSky;
                s_skyQueue.push_back({PackCoord(tx, ty, tz), nextSky, 0, 0});
            }
        }
    }
}

bool LightEngine::OnBlockChanged(const BlockRegistry& reg, Chunk& c, int /*lx*/, int /*ly*/, int /*lz*/, BlockId oldB,
                                 BlockId newB) {
    if (oldB == newB)
        return false;
    InitRegion(reg, c, nullptr, nullptr, nullptr, nullptr);
    return true;
}

}  // namespace game