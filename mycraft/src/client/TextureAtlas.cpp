#include "game/client/TextureAtlas.h"
#include "game/core/Blocks.h"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <array>

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
constexpr std::array<TileSpec, 19> kTiles = {{
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
    {.r=220, .g=200, .b= 40, .noise=10}     // 18 FlowerYellow
}};

std::vector<uint8_t> GenerateAtlasPixels() {
    std::vector<uint8_t> px(static_cast<size_t>(ATLAS_W) * ATLAS_H * 4, 0);

    // 用字符串画出 16x16 的图案 (0=透明, 1=茎, 2=花瓣)
    static const char* flowerPattern[16] = {
        "................",
        "................",
        "................",
        ".....222222.....",
        "....22222222....",
        "....22222222....",
        "....22222222....",
        ".....222222.....",
        "......11........",
        "......11........",
        "......11........",
        "......11........",
        "......11........",
        "......11........",
        "......11........",
        "......11........",
    };

    for (size_t t = 0; t < kTiles.size(); ++t) {
        int col = t % ATLAS_COLS;
        int row = t / ATLAS_COLS;
        int ox = col * ATLAS_TILE;
        int oy = row * ATLAS_TILE;

        for (int y = 0; y < ATLAS_TILE; ++y) {
            for (int x = 0; x < ATLAS_TILE; ++x) {
                int n = static_cast<int>(HashNoise(x, y, t * 31 + 7)) % (kTiles[t].noise * 2 + 1)
                        - kTiles[t].noise;
                int r = kTiles[t].r + n;
                int g = kTiles[t].g + n;
                int b = kTiles[t].b + n;

                if (t == 2 || t == 7) {   // 草侧面 / 雪侧面
                    bool top = (y < 4) || (y == 4 && ((x + HashNoise(x, 0, 99)) % 3 != 0));
                    if (!top) { r = 110 + n; g = 75 + n; b = 50 + n; }   // 下半土色
                }
                if (t == 11) {
                    if ((x + HashNoise(0, y, 42)) % 4 == 0) {
                        r = 110 + n; g = 70 + n; b = 30 + n;   // 深色条纹
                    }
                }

                int i = ((oy + y) * ATLAS_W + (ox + x)) * 4;

                // ================= 处理透明植物 =================
                if (t >= 16) {
                    px[i + 0] = 0; px[i + 1] = 0; px[i + 2] = 0; px[i + 3] = 0; // 默认透明

                    if (t == 16) { 
                        // 从底部开始长，保证根部连着地，越往上越稀疏且带点弯曲
                        if (y >= 4) {
                            // 用 x 和 (15-y) 作为噪声，生成类似植物生长的弯曲感
                            bool isBlade = (HashNoise(x * 3, 15 - y, 42) % 4 == 0);
                            // 强制在底部生成几根主根
                            if (y > 12 && (x == 4 || x == 7 || x == 11)) isBlade = true;

                            if (isBlade) {
                                px[i + 0] = 88; px[i + 1] = 160; px[i + 2] = 70; px[i + 3] = 255;
                            }
                        }
                    } else if (t == 17 || t == 18) { 
                        // 使用上面定义的字符画图案
                        char p = flowerPattern[y][x];
                        if (p == '1') { // 茎
                            px[i + 0] = 42; px[i + 1] = 100; px[i + 2] = 45; px[i + 3] = 255;
                        } else if (p == '2') { // 花瓣
                            px[i + 0] = 220; 
                            px[i + 1] = (t == 17) ? 40 : 200; 
                            px[i + 2] = 40; 
                            px[i + 3] = 255;
                        }
                    }
                    continue; // 跳过下面的默认不透明赋值
                }
                // =================================================

                px[i + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                px[i + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                px[i + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                px[i + 3] = 255;
            }
        }
    }
    return px;
}

// 方块 + 面朝向 -> 图集 tile 索引
// face: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
int TileForBlock(game::BlockType b, int face) {
    switch (b) {
        case game::BlockType::Stone:        return 0;
        case game::BlockType::GrassBlock:
            if (face == 2) return 1;
            if (face == 3) return 3;
            return 2;
        case game::BlockType::Dirt:         return 3;
        case game::BlockType::Sand:         return 4;
        case game::BlockType::Water:        return 5;
        case game::BlockType::Snow:
            // 顶=6（纯白）、底=3（土）、侧面=7
            if (face == 2) return 6;
            if (face == 3) return 3;
            return 7;

        case game::BlockType::Sandstone:    return 8;
        case game::BlockType::CoalOre:      return 9;
        case game::BlockType::IronOre:      return 10;
        case game::BlockType::Wood:         return 11;
        case game::BlockType::Leaves:       return 12;
        case game::BlockType::TallGrass:    return 16;
        case game::BlockType::FlowerRed:    return 17;
        case game::BlockType::FlowerYellow: return 18;
        default:                            return 0;
    }
}

const char* BlockName(game::BlockType t) {
    switch (t) {
        case game::BlockType::Stone:      return "Stone";
        case game::BlockType::Dirt:       return "Dirt";
        case game::BlockType::GrassBlock: return "Grass";
        case game::BlockType::Sand:       return "Sand";
        case game::BlockType::Water:      return "Water";
        case game::BlockType::Snow:       return "Snow";
        case game::BlockType::Wood:       return "Wood";
        case game::BlockType::Leaves:     return "Leaves";
        default:                          return "Unknown";
    }
}

} // namespace game::client