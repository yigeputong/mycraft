#pragma once
#include "../core/Blocks.h"
#include <array>

namespace game {


constexpr int CHUNK_SIZE_X = 16;
constexpr int CHUNK_SIZE_Y = 256;   // 世界高度
constexpr int CHUNK_SIZE_Z = 16;
constexpr int CHUNK_VOLUME = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z;
constexpr int SEA_LEVEL = 64;

struct Chunk {
    int chunk_x = 0;   // 区块 X 坐标
    int chunk_z = 0;   // 区块 Z 坐标

    // 方块数据（x → z → y 顺序，缓存友好）
    std::array<BlockType, CHUNK_VOLUME> blocks{};
};

// 本地坐标 → 数组索引（y → z → x）
constexpr int ChunkIndex(int lx, int ly, int lz) {
    return ly * CHUNK_SIZE_X * CHUNK_SIZE_Z + lz * CHUNK_SIZE_X + lx;
}

// 读取（越界返回 Void）
inline BlockType ChunkGet(const Chunk& c, int lx, int ly, int lz) {
    if (lx < 0 || lx >= CHUNK_SIZE_X ||
        ly < 0 || ly >= CHUNK_SIZE_Y ||
        lz < 0 || lz >= CHUNK_SIZE_Z) return BlockType::Void;
    return c.blocks[ChunkIndex(lx, ly, lz)];
}

// 写入（越界忽略）
inline void ChunkSet(Chunk& c, int lx, int ly, int lz, BlockType t) {
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