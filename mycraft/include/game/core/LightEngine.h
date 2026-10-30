#pragma once
#include "./World.h"
#include "BlockRegistry.h" // ★ 引入 BlockRegistry
#include <queue>

namespace game {

// 移除硬编码的 GetLightEmission(BlockType)

class LightEngine {
public:
    static void InitRegion(const BlockRegistry& reg, Chunk& c, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp);
    
    // ★ BlockType 换为 BlockId，并传入注册表
    static void OnBlockChanged(const BlockRegistry& reg, Chunk& c, int lx, int ly, int lz, BlockId oldB, BlockId newB);

private:
    struct NodeSky   { Chunk* chunk; int16_t x, y, z; uint8_t v; };
    struct NodeBlock { Chunk* chunk; int16_t x, y, z; uint8_t r, g, b; };

    static Chunk* RouteChunk(Chunk* current, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp, int& nx, int& nz);
    
    // ★ 传播函数也需要传入注册表来判断遮挡
    static void PropagateSky  (const BlockRegistry& reg, std::queue<NodeSky>& q, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp);
    static void PropagateBlock(const BlockRegistry& reg, std::queue<NodeBlock>& q, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp);
};

} // namespace game