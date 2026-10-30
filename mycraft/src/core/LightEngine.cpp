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

void LightEngine::InitRegion(const BlockRegistry& reg, Chunk& c, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp) {
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
                    // 使用实例的 IsSolid 判断
                    if (reg.IsSolid(ChunkGet(*chk, x, y, z))) break;
                    chk->sky[ChunkIndex(x, y, z)] = 15;
                    lastY = y;
                }
                if (lastY >= 0) {
                    qSky.push({chk, (int16_t)x, (int16_t)lastY, (int16_t)z, 15});
                }
                
                for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                    BlockId id = ChunkGet(*chk, x, y, z);
                    const auto& def = reg.Get(id);
                    
                    // 基于 luminance 动态生成发光颜色
                    if (def.render.luminance > 0) {
                        uint8_t lum = (uint8_t)def.render.luminance;
                        // 按火把原比例 (14, 10, 6) 进行缩放
                        uint8_t r = lum;
                        uint8_t g = (uint8_t)((float)lum * (10.0f / 14.0f));
                        uint8_t b = (uint8_t)((float)lum * (6.0f / 14.0f));
                        
                        chk->block[ChunkIndex(x, y, z)] = {r, g, b};
                        qBlock.push({chk, (int16_t)x, (int16_t)y, (int16_t)z, r, g, b});
                    }
                }
            }
        }
    };

    seedChunk(&c); seedChunk(nXm); seedChunk(nXp); seedChunk(nZm); seedChunk(nZp);

    PropagateSky(reg, qSky, nXm, nXp, nZm, nZp);
    PropagateBlock(reg, qBlock, nXm, nXp, nZm, nZp);
}

void LightEngine::OnBlockChanged(const BlockRegistry& reg, Chunk& c, [[maybe_unused]] int lx, [[maybe_unused]] int ly, [[maybe_unused]] int lz, BlockId oldB, BlockId newB) {
    if (oldB == newB) return;
    // 服务端暂不传邻居，只做单块全量重算；客户端会在 Worker 里用 InitRegion 跨块计算
    InitRegion(reg, c, nullptr, nullptr, nullptr, nullptr);
}

void LightEngine::PropagateSky(const BlockRegistry& reg, std::queue<NodeSky>& q, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp) {
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
            
            // ★ 实例查表
            if (reg.IsSolid(ChunkGet(*target, nx, ny, nz))) continue;

            int idx = ChunkIndex(nx, ny, nz);
            uint8_t targetVal = n.v - 1;
            if (target->sky[idx] < targetVal) {
                target->sky[idx] = targetVal;
                q.push({target, (int16_t)nx, (int16_t)ny, (int16_t)nz, targetVal});
            }
        }
    }
}

void LightEngine::PropagateBlock(const BlockRegistry& reg, std::queue<NodeBlock>& q, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp) {
    static constexpr int DX[6] = { 1, -1, 0,  0, 0,  0 };
    static constexpr int DY[6] = { 0,  0, 1, -1, 0,  0 };
    static constexpr int DZ[6] = { 0,  0, 0,  0, 1, -1 };

    while (!q.empty()) {
        NodeBlock n = q.front(); q.pop();

        for (int i = 0; i < 6; ++i) {
            int nx = n.x + DX[i], ny = n.y + DY[i], nz = n.z + DZ[i];
            Chunk* target = RouteChunk(n.chunk, nXm, nXp, nZm, nZp, nx, nz);
            if (!target || ny < 0 || ny >= CHUNK_SIZE_Y) continue;
            
            // ★ 实例查表
            if (reg.IsSolid(ChunkGet(*target, nx, ny, nz))) continue;

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