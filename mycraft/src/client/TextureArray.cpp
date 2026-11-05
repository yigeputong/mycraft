#include "game/client/TextureArray.h"
#include "Engine/service/AssetService.h"
#include "Engine/tool/Log.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>

namespace game::client {
namespace {
    const uint8_t PLAINS_GRASS_R = 145, PLAINS_GRASS_G = 189, PLAINS_GRASS_B =  89;
    const uint8_t PLAINS_LEAF_R  = 119, PLAINS_LEAF_G  = 171, PLAINS_LEAF_B  =  47;
    const uint8_t WATER_R        =  63, WATER_G        = 118, WATER_B        = 228;

    struct ArrayState {
        int layerCount = 0;
        std::unordered_map<std::string, int> nameToIndex;
        std::vector<uint8_t> pixels;
    };

    ArrayState& GetState() {
        static ArrayState state;
        return state;
    }

    void TintFromString(const std::string& tint, uint8_t& r, uint8_t& g, uint8_t& b) {
        if      (tint == "grass")   { r = PLAINS_GRASS_R; g = PLAINS_GRASS_G; b = PLAINS_GRASS_B; }
        else if (tint == "foliage") { r = PLAINS_LEAF_R;  g = PLAINS_LEAF_G;  b = PLAINS_LEAF_B;  }
        else if (tint == "water")   { r = WATER_R;        g = WATER_G;        b = WATER_B;        }
        else                        { r = 255;            g = 255;            b = 255;            }
    }
}

void BuildBlockTextureArray(Eng::service::AssetService* assets, 
                            const std::unordered_map<std::string, std::string>& tileTints) {
    if (!assets) return;

    auto& state = GetState();
    auto logger = std::make_unique<Eng::tool::Log>();

    std::vector<std::string> names;
    names.reserve(tileTints.size());
    for (auto& [name, _] : tileTints) names.push_back(name);
    std::sort(names.begin(), names.end());

    int n = static_cast<int>(names.size());
    state.layerCount = n;
    state.nameToIndex.clear();
    
    // 16x16 尺寸 * 总层数 * RGBA(4)
    state.pixels.assign(static_cast<size_t>(TILE_SIZE) * TILE_SIZE * n * 4, 0);

    const std::string base = "assets/textures/block/";
    for (int i = 0; i < n; ++i) {
        const std::string& name = names[i];
        uint8_t tr, tg, tb;
        TintFromString(tileTints.at(name), tr, tg, tb);

        std::string path = base + name + ".png";
        
        auto fileData = assets->ReadFile(path);
        if (!fileData || fileData->empty()) {
            logWarning(logger.get(), "BlockTextureArray: failed to read " << path);
            state.nameToIndex[name] = i;
            continue;
        }

        SDL_IOStream* io = SDL_IOFromConstMem(fileData->data(), fileData->size());
        SDL_Surface* loaded = IMG_Load_IO(io, true);
        
        if (loaded) {
            SDL_Surface* surf = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(loaded);
            if (surf) {
                uint8_t* dst = state.pixels.data() + (static_cast<size_t>(i) * TILE_SIZE * TILE_SIZE * 4);
                const uint8_t* src = static_cast<const uint8_t*>(surf->pixels);
                
                for (int y = 0; y < TILE_SIZE; ++y) {
                    for (int x = 0; x < TILE_SIZE; ++x) {
                        int si = (y * surf->w + x) * 4;
                        int di = (y * TILE_SIZE + x) * 4;
                        dst[di + 0] = static_cast<uint8_t>((src[si + 0] * tr) / 255);
                        dst[di + 1] = static_cast<uint8_t>((src[si + 1] * tg) / 255);
                        dst[di + 2] = static_cast<uint8_t>((src[si + 2] * tb) / 255);
                        dst[di + 3] = src[si + 3];
                    }
                }
                SDL_DestroySurface(surf);
            }
        } else {
            logWarning(logger.get(), "BlockTextureArray: failed to decode " << path);
        }
        state.nameToIndex[name] = i;
    }
    logInfo(logger.get(), "BlockTextureArray: built " << n << " layers.");
}

int ResolveLayer(const std::string& name) {
    auto& state = GetState();
    auto it = state.nameToIndex.find(name);
    return it == state.nameToIndex.end() ? -1 : it->second;
}

int GetTextureLayerCount() { return GetState().layerCount; }
const std::vector<uint8_t>& GetTextureArrayPixels() { return GetState().pixels; }

} // namespace game::client