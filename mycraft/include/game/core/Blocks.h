#pragma once
#include <cstdint>

namespace game {

enum BlockType : uint8_t {
    Air = 0,
    Stone, GrassBlock, Dirt, Sand, Water,
    Void
};

struct Block {
    BlockType type;
};

inline bool IsSolid(BlockType t) {
    return t != BlockType::Air && t != BlockType::Void;
}

}