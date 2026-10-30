#include "game/client/TextureAtlas.h"
#include "core/Log.h"
#include <SDL3_image/SDL_image.h>

#include <cstddef>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace game::client {

namespace {

// ================== 生物群系固定色 ==================
const uint8_t PLAINS_GRASS_R = 145, PLAINS_GRASS_G = 189, PLAINS_GRASS_B =  89; // #91BD59
const uint8_t PLAINS_LEAF_R  = 119, PLAINS_LEAF_G  = 171, PLAINS_LEAF_B  =  47; // #77AB2F
const uint8_t WATER_R        =  63, WATER_G        = 118, WATER_B        = 228; // #3F76E4
// ====================================================

int s_cols = 8;
int s_rows = 8;
std::unordered_map<std::string, int> s_nameToIndex;
std::vector<uint8_t> s_pixels;
std::unique_ptr<Eng::Log> s_logger;

void TintFromString(const std::string& tint, uint8_t& r, uint8_t& g, uint8_t& b) {
    if      (tint == "grass")   { r = PLAINS_GRASS_R; g = PLAINS_GRASS_G; b = PLAINS_GRASS_B; }
    else if (tint == "foliage") { r = PLAINS_LEAF_R;  g = PLAINS_LEAF_G;  b = PLAINS_LEAF_B;  }
    else if (tint == "water")   { r = WATER_R;        g = WATER_G;        b = WATER_B;        }
    else                        { r = 255;            g = 255;            b = 255;            }
}

bool BlitToAtlasTinted(std::vector<uint8_t>& px, int atlasW,
                       const std::string& filepath, int tileIndex, int cols,
                       uint8_t tintR, uint8_t tintG, uint8_t tintB)
{
    SDL_Surface* loaded = IMG_Load(filepath.c_str());
    if (!loaded) return false;

    SDL_Surface* surf = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!surf) return false;

    int col = tileIndex % cols;
    int row = tileIndex / cols;
    int ox = col * ATLAS_TILE;
    int oy = row * ATLAS_TILE;

    int copyW = std::min(surf->w, ATLAS_TILE);
    int copyH = std::min(surf->h, ATLAS_TILE);
    const uint8_t* src = static_cast<const uint8_t*>(surf->pixels);

    for (int y = 0; y < copyH; ++y) {
        for (int x = 0; x < copyW; ++x) {
            int si = (y * surf->w + x) * 4;
            int di = ((oy + y) * atlasW + (ox + x)) * 4;
            px[di + 0] = static_cast<uint8_t>((src[si + 0] * tintR) / 255);
            px[di + 1] = static_cast<uint8_t>((src[si + 1] * tintG) / 255);
            px[di + 2] = static_cast<uint8_t>((src[si + 2] * tintB) / 255);
            px[di + 3] = src[si + 3];
        }
    }
    SDL_DestroySurface(surf);
    return true;
}

void FillMissingTile(std::vector<uint8_t>& px, int atlasW, int tileIndex, int cols) {
    // 按约定：像素全 0，由渲染层补无效纹理（图1 黑粉棋盘格）
    // 如果你想让图集内直接就是黑粉棋盘格，把下面循环换成品红填充即可。
    int col = tileIndex % cols;
    int row = tileIndex / cols;
    int ox = col * ATLAS_TILE;
    int oy = row * ATLAS_TILE;
    for (int y = 0; y < ATLAS_TILE; ++y) {
        for (int x = 0; x < ATLAS_TILE; ++x) {
            int i = ((oy + y) * atlasW + (ox + x)) * 4;
            px[i + 0] = 0;
            px[i + 1] = 0;
            px[i + 2] = 0;
            px[i + 3] = 0;
        }
    }
}

} // anonymous

void BuildAtlas(const std::unordered_map<std::string, std::string>& tileTints) {
    if (!s_logger) s_logger = std::make_unique<Eng::Log>();

    // 1. 收集名字并排序（确定性）
    std::vector<std::string> names;
    names.reserve(tileTints.size());
    for (auto& [name, _] : tileTints) names.push_back(name);
    std::sort(names.begin(), names.end());

    int n = static_cast<int>(names.size());
    if (n == 0) {
        s_cols = 1; s_rows = 1;
        s_nameToIndex.clear();
        s_pixels.assign(static_cast<size_t>(ATLAS_TILE * ATLAS_TILE) * 4, 0);
        logWarning(s_logger.get(), "TextureAtlas: no tiles requested");
        return;
    }

    // 2. 计算图集尺寸：cols = 向上取偶数的 ceil(sqrt(n))
    int c = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    if (c < 1) c = 1;
    if (c & 1) ++c;              // 向上取偶数
    s_cols = c;
    s_rows = c;

    int atlasW = s_cols * ATLAS_TILE;
    int atlasH = s_rows * ATLAS_TILE;
    s_pixels.assign(static_cast<size_t>(atlasW) * atlasH * 4, 0);
    s_nameToIndex.clear();

    logInfo(s_logger.get(), "TextureAtlas: building " << n << " tiles in "
                        << s_cols << "x" << s_rows << " grid");

    // 3. 逐个加载
    const std::string base = "assets/textures/block/";
    for (int i = 0; i < n; ++i) {
        const std::string& name    = names[i];
        const std::string& tintStr = tileTints.at(name);

        uint8_t tr, tg, tb;
        TintFromString(tintStr, tr, tg, tb);

        std::string path = base + name + ".png";
        bool ok = BlitToAtlasTinted(s_pixels, atlasW, path, i, s_cols, tr, tg, tb);
        if (!ok) {
            logWarning(s_logger.get(), "TextureAtlas: failed to load tile '" << name
                        << "' (path=" << path << "), pixel fill zero");
            FillMissingTile(s_pixels, atlasW, i, s_cols);
        }
        s_nameToIndex[name] = i;
    }

    logInfo(s_logger.get(), "TextureAtlas: build complete");
}

int ResolveTile(const std::string& name) {
    auto it = s_nameToIndex.find(name);
    return it == s_nameToIndex.end() ? -1 : it->second;
}

int   AtlasCols() { return s_cols; }
int   AtlasRows() { return s_rows; }
float AtlasTileUV() { return 1.0f / static_cast<float>(s_cols); }
const std::vector<uint8_t>& AtlasPixels() { return s_pixels; }

} // namespace game::client