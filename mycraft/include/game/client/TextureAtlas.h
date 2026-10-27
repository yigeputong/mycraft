#pragma once
#include "../core/Blocks.h"
#include <vector>
#include <cstdint>

namespace game::client {

inline constexpr int ATLAS_TILE = 16;
inline constexpr int ATLAS_COLS = 8;
inline constexpr int ATLAS_ROWS = 8;
inline constexpr int ATLAS_W    = ATLAS_TILE * ATLAS_COLS;
inline constexpr int ATLAS_H    = ATLAS_TILE * ATLAS_ROWS;
inline constexpr float TILE_UV  = 1.0f / ATLAS_COLS;

std::vector<uint8_t> GenerateAtlasPixels();
int TileForBlock(game::BlockType b, int face);
const char* BlockName(game::BlockType t);

} // namespace game::client