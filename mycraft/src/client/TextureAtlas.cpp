#include "game/client/TextureAtlas.h"
#include "Engine/tool/Log.h"
#include <SDL3_image/SDL_image.h>

#include <vector>
#include <cstdint>
#include <algorithm>
#include <string>
#include <unordered_map>

namespace game::client {

namespace {

// ================== 生物群系固定色 ==================
const uint8_t PLAINS_GRASS_R = 145, PLAINS_GRASS_G = 189, PLAINS_GRASS_B =  89; // #91BD59
const uint8_t PLAINS_LEAF_R  = 119, PLAINS_LEAF_G  = 171, PLAINS_LEAF_B  =  47; // #77AB2F
const uint8_t WATER_R        =  63, WATER_G        = 118, WATER_B        = 228; // #3F76E4
// ====================================================

// 包装成函数，解决静态初始化抛异常的警告
struct AtlasState {
    int cols = 8;
    int rows = 8;
    std::unordered_map<std::string, int> nameToIndex;
    std::vector<uint8_t> pixels;
};

AtlasState& GetAtlasState() {
    static AtlasState state;
    return state;
}

std::unique_ptr<Eng::tool::Log>& GetLogger() {
    static std::unique_ptr<Eng::tool::Log> logger;
    return logger;
}

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

    int copyW = std::min(surf->w, ATLAS_REAL_SIZE);
    int copyH = std::min(surf->h, ATLAS_REAL_SIZE);
    const uint8_t* src = static_cast<const uint8_t*>(surf->pixels);

    // 1. 写入原图（居中偏移 1 像素）
    for (int y = 0; y < copyH; ++y) {
        for (int x = 0; x < copyW; ++x) {
            int si = (y * surf->w + x) * 4;
            int di = ((oy + ATLAS_PADDING + y) * atlasW + (ox + ATLAS_PADDING + x)) * 4;
            px[di + 0] = static_cast<uint8_t>((src[si + 0] * tintR) / 255);
            px[di + 1] = static_cast<uint8_t>((src[si + 1] * tintG) / 255);
            px[di + 2] = static_cast<uint8_t>((src[si + 2] * tintB) / 255);
            px[di + 3] = src[si + 3];
        }
    }

    // 2. 边缘外扩（Padding）
    auto clonePixel = [&](int destX, int destY, int srcX, int srcY) {
        int di = ((oy + destY) * atlasW + (ox + destX)) * 4;
        int si = ((oy + srcY) * atlasW + (ox + srcX)) * 4;
        px[di + 0] = px[si + 0];
        px[di + 1] = px[si + 1];
        px[di + 2] = px[si + 2];
        px[di + 3] = px[si + 3];
    };

    for (int y = 1; y <= copyH; ++y) {
        clonePixel(0, y, 1, y);                 // 左
        clonePixel(copyW + 1, y, copyW, y);     // 右
    }
    for (int x = 1; x <= copyW; ++x) {
        clonePixel(x, 0, x, 1);                 // 上
        clonePixel(x, copyH + 1, x, copyH);     // 下
    }
    // 四角
    clonePixel(0, 0, 1, 1);
    clonePixel(copyW + 1, 0, copyW, 1);
    clonePixel(0, copyH + 1, 1, copyH);
    clonePixel(copyW + 1, copyH + 1, copyW, copyH);

    SDL_DestroySurface(surf);
    return true;
}

void FillMissingTile(std::vector<uint8_t>& px, int atlasW, int tileIndex, int cols) {
    int col = tileIndex % cols;
    int row = tileIndex / cols;
    int ox = col * ATLAS_TILE;
    int oy = row * ATLAS_TILE;
    for (int y = 0; y < ATLAS_TILE; ++y) {
        for (int x = 0; x < ATLAS_TILE; ++x) {
            int i = ((oy + y) * atlasW + (ox + x)) * 4;
            px[i + 0] = 0; px[i + 1] = 0; px[i + 2] = 0; px[i + 3] = 0;
        }
    }
}

} // anonymous

void BuildAtlas(const std::unordered_map<std::string, std::string>& tileTints) {
    auto& state = GetAtlasState();
    auto& logger = GetLogger();
    if (!logger) logger = std::make_unique<Eng::tool::Log>();

    // 1. 收集名字并排序
    std::vector<std::string> names;
    names.reserve(tileTints.size());
    for (auto& [name, _] : tileTints) names.push_back(name);
    std::sort(names.begin(), names.end());

    int n = static_cast<int>(names.size());
    if (n == 0) {
        state.cols = 1; state.rows = 1;
        state.nameToIndex.clear();
        state.pixels.assign(static_cast<size_t>(ATLAS_TILE * ATLAS_TILE) * 4, 0);
        logWarning(logger.get(), "TextureAtlas: no tiles requested");
        return;
    }

    // 2. 计算图集尺寸
    int c = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    if (c < 1) c = 1;
    if (c & 1) ++c; 
    state.cols = c;
    state.rows = c;

    int atlasW = state.cols * ATLAS_TILE;
    int atlasH = state.rows * ATLAS_TILE;
    state.pixels.assign(static_cast<size_t>(atlasW) * atlasH * 4, 0);
    state.nameToIndex.clear();

    logInfo(logger.get(), "TextureAtlas: building " << n << " tiles in "
                        << state.cols << "x" << state.rows << " grid");

    // 3. 逐个加载并处理 Padding
    const std::string base = "assets/textures/block/";
    for (int i = 0; i < n; ++i) {
        const std::string& name    = names[i];
        const std::string& tintStr = tileTints.at(name);

        uint8_t tr, tg, tb;
        TintFromString(tintStr, tr, tg, tb);

        std::string path = base + name + ".png";
        bool ok = BlitToAtlasTinted(state.pixels, atlasW, path, i, state.cols, tr, tg, tb);
        if (!ok) {
            logWarning(logger.get(), "TextureAtlas: failed to load tile '" << name
                        << "' (path=" << path << "), pixel fill zero");
            FillMissingTile(state.pixels, atlasW, i, state.cols);
        }
        state.nameToIndex[name] = i;
    }

    logInfo(logger.get(), "TextureAtlas: build complete");
}

int ResolveTile(const std::string& name) {
    auto& state = GetAtlasState();
    auto it = state.nameToIndex.find(name);
    return it == state.nameToIndex.end() ? -1 : it->second;
}

int AtlasCols() { return GetAtlasState().cols; }
int AtlasRows() { return GetAtlasState().rows; }
const std::vector<uint8_t>& AtlasPixels() { return GetAtlasState().pixels; }

// ★ 新增：统一的 UV 计算（向内收缩 1 个像素）
void GetTileUV(int tileIndex, float& u0, float& v0, float& u1, float& v1) {
    auto& state = GetAtlasState();
    float gridUV = 1.0f / static_cast<float>(state.cols);  // 每个大格的 UV 宽度
    float pixelUV = gridUV / static_cast<float>(ATLAS_TILE); // 单个像素的 UV 宽度

    int col = tileIndex % state.cols;
    int row = tileIndex / state.cols;

    // 偏移 1 个像素，跳过 Padding
    u0 = (col * gridUV) + pixelUV;
    v0 = (row * gridUV) + pixelUV;
    // 覆盖 16x16 原图
    u1 = u0 + (ATLAS_REAL_SIZE * pixelUV);
    v1 = v0 + (ATLAS_REAL_SIZE * pixelUV);
}

// 为了防止有人还在用旧 API，保留这个
float AtlasTileUV() { return 1.0f / static_cast<float>(GetAtlasState().cols); }

} // namespace game::client