#include "game/core/LightEngine.h"
#include "game/core/World.h"
#include <algorithm>
#include <vector>
#include <span>
#include <cstdint>

namespace game {

namespace {
    constexpr int Y_STRIDE = CHUNK_SIZE_X * CHUNK_SIZE_Z;
    constexpr int Z_STRIDE = CHUNK_SIZE_X;
    constexpr int CHUNK_VOL = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z;

    // 将 x,y,z 压缩为一个 16 位整数 (x:4bit, z:4bit, y:8bit)
    // 足够表示 16*16*256 = 65536 的所有坐标
    inline uint16_t PackCoord(int x, int y, int z) {
        return static_cast<uint16_t>(x | (z << 4) | (y << 8));
    }
    
    inline int GetX(uint16_t idx) { return idx & 0x0F; }
    inline int GetZ(uint16_t idx) { return (idx >> 4) & 0x0F; }
    inline int GetY(uint16_t idx) { return (idx >> 8) & 0xFF; }

    // 极简节点，总共 4 字节（含对齐）
    struct LightNode {
        uint16_t index;
        uint8_t r, g, b;
    };

    // 线程局部队列，复用内存，绝无反复分配
    thread_local std::vector<LightNode> s_queue;

    // 预先计算好的 6 个方向的偏移量
    constexpr int DX[6] = { 1, -1, 0,  0, 0,  0 };
    constexpr int DY[6] = { 0,  0, 1, -1, 0,  0 };
    constexpr int DZ[6] = { 0,  0, 0,  0, 1, -1 };

    // 辅助判断是否越界
    inline bool IsOutOfBounds(int x, int y, int z) {
        return x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z;
    }
}

void LightEngine::InitRegion(const BlockRegistry& reg, Chunk& c, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp) {
    // 清空当前区块光照
    std::fill(c.sky.begin(), c.sky.end(), 0);
    std::fill(c.block.begin(), c.block.end(), LightRGB{0,0,0});
    
    // 使用 span 进行零拷贝访问
    std::span<uint8_t> skySpan(c.sky);
    std::span<LightRGB> blockSpan(c.block);
    std::span<const BlockId> blocksSpan(c.blocks);
    
    // 复用队列，清空
    s_queue.clear();
    s_queue.reserve(16384); // 预分配，避免扩容

    // 1. 自上而下填满天空光（优化：如果是透明方块直接标15，遇到不透明直接跳过）
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int lastY = -1;
            for (int y = CHUNK_SIZE_Y - 1; y >= 0; --y) {
                int idx = y * Y_STRIDE + z * Z_STRIDE + x;
                const auto& def = reg.Get(blocksSpan[idx]);

                if (def.render.isOpaque) break; // 遇到不透明，阻断天空光
                
                skySpan[idx] = 15;
                lastY = y;

                if (def.render.luminance > 0) {
                    uint8_t lum = static_cast<uint8_t>(def.render.luminance);
                    uint8_t r = lum, g = static_cast<uint8_t>((float)lum * (10.0f / 14.0f)), b = static_cast<uint8_t>((float)lum * (6.0f / 14.0f));
                    blockSpan[idx] = {r, g, b};
                    s_queue.push_back({ PackCoord(x, y, z), r, g, b });
                }
            }
            // 将天空光顶端加入队列扩散
            if (lastY >= 0) {
                s_queue.push_back({ PackCoord(x, lastY, z), 15, 15, 15 });
            }
        }
    }

    // 2. 整合 BFS (天空光与方块光统一处理，大幅减少循环分支)
    size_t head = 0;
    while (head < s_queue.size()) {
        LightNode n = s_queue[head++];
        int nx = GetX(n.index);
        int ny = GetY(n.index);
        int nz = GetZ(n.index);

        const bool isSky = (n.r == 15 && n.g == 15 && n.b == 15);

        for (int i = 0; i < 6; ++i) {
            int tx = nx + DX[i], ty = ny + DY[i], tz = nz + DZ[i];
            
            if (IsOutOfBounds(tx, ty, tz)) continue;

            int tidx = ty * Y_STRIDE + tz * Z_STRIDE + tx;
            if (reg.Get(blocksSpan[tidx]).render.isOpaque) continue;

            uint16_t packed = PackCoord(tx, ty, tz);

            if (isSky) {
                // 天空光传播
                if (skySpan[tidx] < n.r - 1) {
                    skySpan[tidx] = n.r - 1;
                    s_queue.push_back({ packed, static_cast<uint8_t>(n.r - 1), 15, 15 });
                }
            } else {
                // 方块光传播 (RGB 独立计算)
                LightRGB cur = blockSpan[tidx];
                LightRGB next = cur;
                bool updated = false;

                if (n.r > 0 && cur.r < n.r - 1) { next.r = n.r - 1; updated = true; }
                if (n.g > 0 && cur.g < n.g - 1) { next.g = n.g - 1; updated = true; }
                if (n.b > 0 && cur.b < n.b - 1) { next.b = n.b - 1; updated = true; }

                if (updated) {
                    blockSpan[tidx] = next;
                    s_queue.push_back({ packed, next.r, next.g, next.b });
                }
            }
        }
    }
}

bool LightEngine::OnBlockChanged(const BlockRegistry& reg, Chunk& c, int lx, int ly, int lz, BlockId oldB, BlockId newB) {
    if (oldB == newB) return false;

    // 粗暴但安全的增量更新：因为我们不跨区块，直接重算当前区块即可。
    // 这里可以加个标记，让外面的主线程把该区块标脏，重建 Mesh。
    InitRegion(reg, c, nullptr, nullptr, nullptr, nullptr); 
    return true;
}

} // namespace game