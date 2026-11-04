#pragma once
#include "game/core/World.h"
#include "game/core/BlockRegistry.h"

namespace game {

class LightEngine {
public:
    // 初始化当前区块光照（多线程安全，绝不越界）
    static void InitRegion(const BlockRegistry& reg, Chunk& c, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp);
    
    // 局部增量更新（返回 true 表示光照发生改变，需要重建 Mesh）
    static bool OnBlockChanged(const BlockRegistry& reg, Chunk& c, int lx, int ly, int lz, BlockId oldB, BlockId newB);
};

} // namespace game