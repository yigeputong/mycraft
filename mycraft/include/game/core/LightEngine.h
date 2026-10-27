#pragma once
#include "./World.h"
#include <queue>

namespace game {

struct LightEmission { uint8_t r = 0, g = 0, b = 0; };
inline LightEmission GetLightEmission(BlockType b) { 
    (void)b;
    return {0, 0, 0}; 
} // 后续加火把在这里填

class LightEngine {
public:
    static void InitRegion(Chunk& c, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp);
    static void OnBlockChanged(Chunk& c, int lx, int ly, int lz, BlockType oldB, BlockType newB);

private:
    struct NodeSky   { Chunk* chunk; int16_t x, y, z; uint8_t v; };
    struct NodeBlock { Chunk* chunk; int16_t x, y, z; uint8_t r, g, b; };

    static Chunk* RouteChunk(Chunk* current, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp, int& nx, int& nz);
    static void PropagateSky  (std::queue<NodeSky>& q, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp);
    static void PropagateBlock(std::queue<NodeBlock>& q, Chunk* nXm, Chunk* nXp, Chunk* nZm, Chunk* nZp);
};

} // namespace game