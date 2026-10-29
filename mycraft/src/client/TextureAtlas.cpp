#include "game/client/TextureAtlas.h"
#include "game/core/Blocks.h"

#include <SDL3_image/SDL_image.h>

#include <vector>
#include <cstdint>
#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>

// ================== 颜色定义 ==================
// --- 平原 (Plains) ---
// 草地: #91BD59 -> R:145, G:189, B:89
const uint8_t PLAINS_GRASS_R = 145, PLAINS_GRASS_G = 189, PLAINS_GRASS_B = 89;
// 树叶: #77AB2F -> R:119, G:171, B:47
const uint8_t PLAINS_LEAF_R = 119, PLAINS_LEAF_G = 171, PLAINS_LEAF_B = 47;

// --- 水体 (Water) --- 这里以Java版颜色为例
// 平原/河流水体: #3F76E4 -> R:63, G:118, B:228
const uint8_t WATER_R = 63, WATER_G = 118, WATER_B = 228;
// ============================================

namespace game::client {

// 简单 hash 噪声，让每个像素略有差异，不至于纯色块
constexpr auto HashNoise = [](int x, int y, uint32_t seed) -> uint8_t {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u
               + static_cast<uint32_t>(y) * 668265263u
               + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<uint8_t>((h ^ (h >> 16)) & 0xFF);
};

struct TileSpec { uint8_t r, g, b; int noise; };
constexpr std::array<TileSpec, 20> kTiles = {{
    {.r=128, .g=128, .b=128, .noise=30},    // 0 stone
    {.r= 88, .g=160, .b= 70, .noise=20},    // 1 grass_top
    {.r= 88, .g=160, .b= 70, .noise=20},    // 2 grass_side
    {.r=110, .g= 75, .b= 50, .noise=25},    // 3 dirt
    {.r=220, .g=210, .b=160, .noise=20},    // 4 sand
    {.r= 40, .g= 90, .b=180, .noise=15},    // 5 water
    {.r=210, .g=218, .b=228, .noise=20},    // 6 snow_top
    {.r=210, .g=218, .b=228, .noise=20},    // 7 snow_side
    {.r=200, .g=180, .b=130, .noise=25},    // 8 sandstone
    {.r= 60, .g= 60, .b= 60, .noise=25},    // 9 coal_ore
    {.r=180, .g=140, .b=100, .noise=25},    // 10 iron_ore
    {.r=139, .g= 90, .b= 43, .noise=25},    // 11 wood
    {.r= 42, .g=100, .b= 45, .noise=35},    // 12 leaves
    {.r=224, .g=180, .b=150, .noise=15},    // 13 skin
    {.r= 60, .g=110, .b=200, .noise=20},    // 14 shirt
    {.r= 45, .g= 55, .b=110, .noise=20},    // 15 pants
    {.r= 88, .g=160, .b= 70, .noise=20},    // 16 TallGrass
    {.r=220, .g= 40, .b= 40, .noise=10},    // 17 FlowerRed
    {.r=220, .g=200, .b= 40, .noise=10},    // 18 FlowerYellow
    {.r=255, .g=200, .b= 50, .noise=10}     // 19 Torch
}};

bool BlitToAtlasTinted(std::vector<uint8_t>& px, const std::string& filepath, int tileIndex, 
                       uint8_t tintR = 255, uint8_t tintG = 255, uint8_t tintB = 255) 
{
    SDL_Surface* loaded = IMG_Load(filepath.c_str());
    if (!loaded) return false;
    
    SDL_Surface* surf = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!surf) return false;

    int col = tileIndex % ATLAS_COLS;
    int row = tileIndex / ATLAS_COLS;
    int ox = col * ATLAS_TILE;
    int oy = row * ATLAS_TILE;

    int copyW = std::min(surf->w, 16);
    int copyH = std::min(surf->h, 16);
    const uint8_t* srcPixels = static_cast<const uint8_t*>(surf->pixels);

    for (int y = 0; y < copyH; ++y) {
        for (int x = 0; x < copyW; ++x) {
            int srcIdx = (y * surf->w + x) * 4;
            int dstIdx = ((oy + y) * ATLAS_W + (ox + x)) * 4;
            
            // ★ 核心：将原图的 RGB 乘以染料的 RGB
            px[dstIdx + 0] = static_cast<uint8_t>((srcPixels[srcIdx + 0] * tintR) / 255);
            px[dstIdx + 1] = static_cast<uint8_t>((srcPixels[srcIdx + 1] * tintG) / 255);
            px[dstIdx + 2] = static_cast<uint8_t>((srcPixels[srcIdx + 2] * tintB) / 255);
            px[dstIdx + 3] = srcPixels[srcIdx + 3]; // Alpha 不变
        }
    }
    
    SDL_DestroySurface(surf);
    return true;
}

void FallbackTile(std::vector<uint8_t>& px, int t) {
    int col = t % ATLAS_COLS;
    int row = t / ATLAS_COLS;
    int ox = col * ATLAS_TILE;
    int oy = row * ATLAS_TILE;

    for (int y = 0; y < ATLAS_TILE; ++y) {
        for (int x = 0; x < ATLAS_TILE; ++x) {
            int n = static_cast<int>(HashNoise(x, y, t * 31 + 7)) % (kTiles[t].noise * 2 + 1) - kTiles[t].noise;
            int r = std::clamp(kTiles[t].r + n, 0, 255);
            int g = std::clamp(kTiles[t].g + n, 0, 255);
            int b = std::clamp(kTiles[t].b + n, 0, 255);
            int i = ((oy + y) * ATLAS_W + (ox + x)) * 4;
            px[i + 0] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255;
        }
    }
}

std::vector<uint8_t> GenerateAtlasPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(ATLAS_W) * ATLAS_H * 4, 0);
    std::string base = "./assets/textures/block/";

    for (size_t t = 0; t < kTiles.size(); ++t) {
        bool success = false;
        
        // 针对需要染色的方块，使用 BlitToAtlasTinted
        // 这里我们先统一使用平原的颜色，方便你快速看到效果。
        // 后续你可以根据生物群系动态切换这些常量。
        if (t == 1) { // 草方块顶面
            success = BlitToAtlasTinted(px, base + "grass_block_top.png", t, PLAINS_GRASS_R, PLAINS_GRASS_G, PLAINS_GRASS_B);
        } else if (t == 2) { // 草方块侧面
            // 侧面染色逻辑不变，先用平原草地色
            success = BlitToAtlasTinted(px, base + "grass_block_side.png", t, PLAINS_GRASS_R, PLAINS_GRASS_G, PLAINS_GRASS_B);
        } else if (t == 12) { // 树叶
            success = BlitToAtlasTinted(px, base + "oak_leaves.png", t, PLAINS_LEAF_R, PLAINS_LEAF_G, PLAINS_LEAF_B);
        } else if (t == 16) { // 高草丛
            success = BlitToAtlasTinted(px, base + "short_grass.png", t, PLAINS_GRASS_R, PLAINS_GRASS_G, PLAINS_GRASS_B);
        } 
        // ★ 新增：处理水体染色
        else if (t == 5) { 
            // 假设水的原图是灰度的，我们给它染上水的颜色
            success = BlitToAtlasTinted(px, base + "water_still.png", t, WATER_R, WATER_G, WATER_B);
        } else {
            std::unordered_map<int, std::string> pngMap = {
                { 0, "stone.png" }, { 3, "dirt.png" }, { 4, "sand.png" },
                { 5, "water_still.png" }, { 6, "snow.png" }, { 7, "snow.png" },
                { 11, "oak_log.png" }, { 17, "poppy.png" }, { 18, "dandelion.png" },
                { 19, "torch.png" }
            };
            auto it = pngMap.find(t);
            if (it != pngMap.end()) {
                success = BlitToAtlasTinted(px, base + it->second, t, 255, 255, 255); // 传 255 等于不染色
            }
        }

        if (!success) {
            FallbackTile(px, t);
        }
    }

    return px;
}

// 方块 + 面朝向 -> 图集 tile 索引
// face: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
int TileForBlock(BlockType b, int face) {
    switch (b) {
        case BlockType::Stone:        return 0;
        case BlockType::GrassBlock:
            if (face == 2) return 1; // +Y 顶面：草
            if (face == 3) return 3; // -Y 底面：泥
            return 2;                // 侧面：草土混合
        case BlockType::Dirt:         return 3;
        case BlockType::Sand:         return 4;
        case BlockType::Water:        return 5;
        case BlockType::Snow:
            if (face == 2) return 6; // 顶面纯雪
            if (face == 3) return 3; // 底面土
            return 7;                // 侧面雪+土
        case BlockType::Sandstone:    return 8;
        case BlockType::CoalOre:      return 9;
        case BlockType::IronOre:      return 10;
        case BlockType::Wood:         return 11;
        case BlockType::Leaves:       return 12;
        case BlockType::TallGrass:    return 16;
        case BlockType::FlowerRed:    return 17;
        case BlockType::FlowerYellow: return 18;
        case BlockType::Torch:        return 19;
        default:                      return 0;
    }
}

const char* BlockName(BlockType t) {
    switch (t) {
        case BlockType::Air:          return "Air";
        case BlockType::Stone:        return "Stone";
        case BlockType::GrassBlock:   return "Grass Block";
        case BlockType::Dirt:         return "Dirt";
        case BlockType::Sand:         return "Sand";
        case BlockType::Water:        return "Water";
        case BlockType::Snow:         return "Snow";
        case BlockType::Sandstone:    return "Sandstone";
        case BlockType::CoalOre:      return "Coal Ore";
        case BlockType::IronOre:      return "Iron Ore";
        case BlockType::Wood:         return "Wood";
        case BlockType::Leaves:       return "Leaves";
        case BlockType::TallGrass:    return "Tall Grass";
        case BlockType::FlowerRed:    return "Red Flower";
        case BlockType::FlowerYellow: return "Yellow Flower";
        case BlockType::Torch:        return "Torch";
        case BlockType::Void:         return "Void";
        default:                      return "Unknown";
    }
}

} // namespace client