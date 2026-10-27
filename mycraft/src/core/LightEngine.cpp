#include "game/core/LightEngine.h"
#include "game/core/Blocks.h"
#include <algorithm>

namespace game {

Chunk* LightEngine::RouteChunk(Chunk* current, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp, int& nx, int& nz) {
    if (nx < 0) { nx += CHUNK_SIZE_X; return nXm; }
    if (nx >= CHUNK_SIZE_X) { nx -= CHUNK_SIZE_X; return nXp; }
    if (nz < 0) { nz += CHUNK_SIZE_Z; return nZm; }
    if (nz >= CHUNK_SIZE_Z) { nz -= CHUNK_SIZE_Z; return nZp; }
    return current;
}

void LightEngine::InitRegion(Chunk& c, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp) {
    auto clearLight = [](Chunk* chk) { 
        if(!chk) return; 
        std::fill(chk->sky.begin(), chk->sky.end(), 0);
        std::fill(chk->block.begin(), chk->block.end(), LightRGB{0,0,0});
    };
    clearLight(&c); clearLight(nXm); clearLight(nXp); clearLight(nZm); clearLight(nZp);

    std::queue<NodeSky> qSky;
    std::queue<NodeBlock> qBlock;

    auto seedChunk = [&](Chunk* chk) {
        if (!chk) return;
        for (int x = 0; x < CHUNK_SIZE_X; ++x) {
            for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
                int lastY = -1;
                for (int y = CHUNK_SIZE_Y - 1; y >= 0; --y) {
                    if (IsSolid(ChunkGet(*chk, x, y, z))) break;
                    chk->sky[ChunkIndex(x, y, z)] = 15;
                    lastY = y;
                }
                if (lastY >= 0) {
                    qSky.push({chk, (int16_t)x, (int16_t)lastY, (int16_t)z, 15});
                }
                for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                    LightEmission em = GetLightEmission(ChunkGet(*chk, x, y, z));
                    if (em.r || em.g || em.b) {
                        chk->block[ChunkIndex(x, y, z)] = {em.r, em.g, em.b};
                        qBlock.push({chk, (int16_t)x, (int16_t)y, (int16_t)z, em.r, em.g, em.b});
                    }
                }
            }
        }
    };

    seedChunk(&c); seedChunk(nXm); seedChunk(nXp); seedChunk(nZm); seedChunk(nZp);

    PropagateSky(qSky, nXm, nXp, nZm, nZp);
    PropagateBlock(qBlock, nXm, nXp, nZm, nZp);
}

void LightEngine::OnBlockChanged(Chunk& c, int lx, int ly, int lz, BlockType oldB, BlockType newB) {
    if (oldB == newB) return;
    // 服务端暂不传邻居，只做单块全量重算；客户端会在 Worker 里用 InitRegion 跨块计算
    InitRegion(c, nullptr, nullptr, nullptr, nullptr);
}

void LightEngine::PropagateSky(std::queue<NodeSky>& q, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp) {
    static constexpr int DX[6] = { 1, -1, 0,  0, 0,  0 };
    static constexpr int DY[6] = { 0,  0, 1, -1, 0,  0 };
    static constexpr int DZ[6] = { 0,  0, 0,  0, 1, -1 };

    while (!q.empty()) {
        NodeSky n = q.front(); q.pop();
        if (n.v <= 1) continue;

        for (int i = 0; i < 6; ++i) {
            int nx = n.x + DX[i], ny = n.y + DY[i], nz = n.z + DZ[i];
            Chunk* target = RouteChunk(n.chunk, nXm, nXp, nZm, nZp, nx, nz);
            if (!target || ny < 0 || ny >= CHUNK_SIZE_Y) continue;
            if (IsSolid(ChunkGet(*target, nx, ny, nz))) continue;

            int idx = ChunkIndex(nx, ny, nz);
            uint8_t targetVal = n.v - 1;
            if (target->sky[idx] < targetVal) {
                target->sky[idx] = targetVal;
                q.push({target, (int16_t)nx, (int16_t)ny, (int16_t)nz, targetVal});
            }
        }
    }
}

void LightEngine::PropagateBlock(std::queue<NodeBlock>& q, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp) {
    static constexpr int DX[6] = { 1, -1, 0,  0, 0,  0 };
    static constexpr int DY[6] = { 0,  0, 1, -1, 0,  0 };
    static constexpr int DZ[6] = { 0,  0, 0,  0, 1, -1 };

    while (!q.empty()) {
        NodeBlock n = q.front(); q.pop();

        for (int i = 0; i < 6; ++i) {
            int nx = n.x + DX[i], ny = n.y + DY[i], nz = n.z + DZ[i];
            Chunk* target = RouteChunk(n.chunk, nXm, nXp, nZm, nZp, nx, nz);
            if (!target || ny < 0 || ny >= CHUNK_SIZE_Y) continue;
            if (IsSolid(ChunkGet(*target, nx, ny, nz))) continue;

            int idx = ChunkIndex(nx, ny, nz);
            LightRGB cur = target->block[idx];
            LightRGB next = cur;
            bool updated = false;

            if (n.r > 0 && cur.r < (int)n.r - 1) { next.r = n.r - 1; updated = true; }
            if (n.g > 0 && cur.g < (int)n.g - 1) { next.g = n.g - 1; updated = true; }
            if (n.b > 0 && cur.b < (int)n.b - 1) { next.b = n.b - 1; updated = true; }

            if (updated) {
                target->block[idx] = next;
                q.push({target, (int16_t)nx, (int16_t)ny, (int16_t)nz, next.r, next.g, next.b});
            }
        }
    }
}

} // namespace game