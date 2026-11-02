#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace game::client {

    constexpr int ATLAS_REAL_SIZE = 16;  // 原始图片的尺寸
    constexpr int ATLAS_PADDING   = 1;   // 边缘外扩的像素数
    [[maybe_unused]] constexpr int ATLAS_TILE = ATLAS_REAL_SIZE + ATLAS_PADDING * 2; // 实际占用的格子大小（18）

    // 提供统一的 UV 计算接口，外部代码不要自己手算 UV 了！
    void GetTileUV(int tileIndex, float& u0, float& v0, float& u1, float& v1);

    void BuildAtlas(const std::unordered_map<std::string, std::string>& tileTints);
    int ResolveTile(const std::string& name);
    int AtlasCols();
    int AtlasRows();
    [[deprecated("Use GetTileUV() instead. AtlasTileUV() does not account for padding and will cause texture bleeding.")]]
    float AtlasTileUV();
    const std::vector<uint8_t>& AtlasPixels();
}