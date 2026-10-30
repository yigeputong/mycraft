#pragma once
#include "./World.h"
#include "BlockRegistry.h"

namespace game {

class LightEngine {
public:
    // 只初始化当前区块，不污染邻居，防止多线程冲突
    static void InitRegion(const BlockRegistry& reg, Chunk& c, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp);
    
    static void OnBlockChanged(const BlockRegistry& reg, Chunk& c, int lx, int ly, int lz, BlockId oldB, BlockId newB);

private:
    static Chunk* RouteChunk(Chunk* current, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp, int& nx, int& nz);
};

} // namespace game