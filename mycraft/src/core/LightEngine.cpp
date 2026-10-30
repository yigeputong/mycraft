#include "game/core/LightEngine.h"
#include <algorithm>

namespace game {

Chunk* LightEngine::RouteChunk(Chunk* current, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp, int& nx, int& nz) {
    if (nx < 0) { nx += CHUNK_SIZE_X; return nXm; }
    if (nx >= CHUNK_SIZE_X) { nx -= CHUNK_SIZE_X; return nXp; }
    if (nz < 0) { nz += CHUNK_SIZE_Z; return nZm; }
    if (nz >= CHUNK_SIZE_Z) { nz -= CHUNK_SIZE_Z; return nZp; }
    return current;
}

// 环形队列节点结构
struct FastNodeSky {
    Chunk* chunk;
    int16_t x, y, z;
    uint8_t v;
};

struct FastNodeBlock {
    Chunk* chunk;
    int16_t x, y, z;
    uint8_t r, g, b;
};

void LightEngine::InitRegion(const BlockRegistry& reg, Chunk& c, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp) {
    // ★ 1. 仅清空当前区块的光照，不触碰邻居，避免多线程数据竞争
    std::fill(c.sky.begin(), c.sky.end(), 0);
    std::fill(c.block.begin(), c.block.end(), LightRGB{0,0,0});

    // ★ 2. 使用 thread_local 定长数组作为环形队列，最大容量 131072（足以容纳整个区块的多次入队）
    static thread_local FastNodeSky qSky[131072];
    static thread_local FastNodeBlock qBlock[131072];
    int skyHead = 0, skyTail = 0;
    int blockHead = 0, blockTail = 0;

    const int Y_STRIDE = CHUNK_SIZE_X * CHUNK_SIZE_Z;
    const int Z_STRIDE = CHUNK_SIZE_X;

    // 3. 填充基础天空光并收集发光方块
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int lastY = -1;
            for (int y = CHUNK_SIZE_Y - 1; y >= 0; --y) {
                int idx = y * Y_STRIDE + z * Z_STRIDE + x;
                BlockId id = c.blocks[idx];
                const auto& def = reg.Get(id);

                if (def.render.isOpaque) break; // 遇到不透明方块，天空光受阻
                
                c.sky[idx] = 15;
                lastY = y;

                if (def.render.luminance > 0) {
                    uint8_t lum = (uint8_t)def.render.luminance;
                    uint8_t r = lum, g = (uint8_t)((float)lum * (10.0f / 14.0f)), b = (uint8_t)((float)lum * (6.0f / 14.0f));
                    c.block[idx] = {r, g, b};
                    qBlock[blockTail++] = {&c, (int16_t)x, (int16_t)y, (int16_t)z, r, g, b};
                }
            }
            if (lastY >= 0) {
                qSky[skyTail++] = {&c, (int16_t)x, (int16_t)lastY, (int16_t)z, 15};
            }
        }
    }

    const int DX[6] = { 1, -1, 0,  0, 0,  0 };
    const int DY[6] = { 0,  0, 1, -1, 0,  0 };
    const int DZ[6] = { 0,  0, 0,  0, 1, -1 };

    // 4. 传播天空光 BFS
    while (skyHead < skyTail) {
        FastNodeSky n = qSky[skyHead++];
        if (n.v <= 1) continue;

        for (int i = 0; i < 6; ++i) {
            int nx = n.x + DX[i], ny = n.y + DY[i], nz = n.z + DZ[i];
            Chunk* target = RouteChunk(n.chunk, nXm, nXp, nZm, nZp, nx, nz);
            if (!target || ny < 0 || ny >= CHUNK_SIZE_Y) continue;

            int idx = ny * Y_STRIDE + nz * Z_STRIDE + nx;
            if (reg.Get(target->blocks[idx]).render.isOpaque) continue;

            uint8_t targetVal = n.v - 1;
            if (target->sky[idx] < targetVal) {
                target->sky[idx] = targetVal;
                if (skyTail >= 131072) skyTail = 0; // 极少发生，防止越界
                qSky[skyTail++] = {target, (int16_t)nx, (int16_t)ny, (int16_t)nz, targetVal};
            }
        }
    }

    // 5. 传播方块光 BFS
    while (blockHead < blockTail) {
        FastNodeBlock n = qBlock[blockHead++];

        for (int i = 0; i < 6; ++i) {
            int nx = n.x + DX[i], ny = n.y + DY[i], nz = n.z + DZ[i];
            Chunk* target = RouteChunk(n.chunk, nXm, nXp, nZm, nZp, nx, nz);
            if (!target || ny < 0 || ny >= CHUNK_SIZE_Y) continue;

            int idx = ny * Y_STRIDE + nz * Z_STRIDE + nx;
            if (reg.Get(target->blocks[idx]).render.isOpaque) continue;

            LightRGB cur = target->block[idx];
            LightRGB next = cur;
            bool updated = false;

            if (n.r > 0 && cur.r < n.r - 1) { next.r = n.r - 1; updated = true; }
            if (n.g > 0 && cur.g < n.g - 1) { next.g = n.g - 1; updated = true; }
            if (n.b > 0 && cur.b < n.b - 1) { next.b = n.b - 1; updated = true; }

            if (updated) {
                target->block[idx] = next;
                if (blockTail >= 131072) blockTail = 0;
                qBlock[blockTail++] = {target, (int16_t)nx, (int16_t)ny, (int16_t)nz, next.r, next.g, next.b};
            }
        }
    }
}

void LightEngine::OnBlockChanged(const BlockRegistry& reg, Chunk& c, [[maybe_unused]] int lx, [[maybe_unused]] int ly, [[maybe_unused]] int lz, BlockId oldB, BlockId newB) {
    if (oldB == newB) return;
    InitRegion(reg, c, nullptr, nullptr, nullptr, nullptr);
}

} // namespace game