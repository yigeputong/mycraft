#pragma once
#include "Blocks.h"
#include "BlockRegistry.h"
#include <array>

namespace game {

constexpr int CHUNK_SIZE_X = 16;
constexpr int CHUNK_SIZE_Y = 256;   // 世界高度
constexpr int CHUNK_SIZE_Z = 16;
constexpr int CHUNK_VOLUME = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z;
constexpr int SEA_LEVEL = 64;

struct LightRGB { uint8_t r = 0, g = 0, b = 0; };

struct Chunk {
    int chunk_x = 0;
    int chunk_z = 0;

    // 方块数据
    std::array<BlockId, CHUNK_VOLUME> blocks; 

    // 光照数据（随 chunk 加载/卸载）
    std::array<uint8_t, CHUNK_VOLUME>  sky{};
    std::array<LightRGB, CHUNK_VOLUME> block{};
};

// 本地坐标 → 数组索引（y → z → x）
constexpr int ChunkIndex(int lx, int ly, int lz) {
    return ly * CHUNK_SIZE_X * CHUNK_SIZE_Z + lz * CHUNK_SIZE_X + lx;
}

// 读取（越界返回 UNKNOWN_ID）
inline BlockId ChunkGet(const Chunk& c, int lx, int ly, int lz) {
    if (lx < 0 || lx >= CHUNK_SIZE_X ||
        ly < 0 || ly >= CHUNK_SIZE_Y ||
        lz < 0 || lz >= CHUNK_SIZE_Z) return UNKNOWN_ID; // ★ 修改
    return c.blocks[ChunkIndex(lx, ly, lz)];
}

// 写入（越界忽略）
inline void ChunkSet(Chunk& c, int lx, int ly, int lz, BlockId t) { // ★ 修改
    if (lx < 0 || lx >= CHUNK_SIZE_X ||
        ly < 0 || ly >= CHUNK_SIZE_Y ||
        lz < 0 || lz >= CHUNK_SIZE_Z) return;
    c.blocks[ChunkIndex(lx, ly, lz)] = t;
}

constexpr static uint64_t ChunkKey(int cx, int cz) {
    return (uint64_t)(uint32_t)cx | ((uint64_t)(uint32_t)cz << 32);
}

// 世界坐标 ↔ 区块坐标
constexpr int WorldToChunkX(int wx) { return wx >> 4; }
constexpr int WorldToChunkZ(int wz) { return wz >> 4; }
constexpr int WorldToLocalX(int wx) { return wx & 15; }
constexpr int WorldToLocalZ(int wz) { return wz & 15; }

}