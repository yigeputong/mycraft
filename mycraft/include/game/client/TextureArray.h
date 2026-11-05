#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace Eng::service {
    class AssetService;
}

namespace game::client {
    constexpr int TILE_SIZE = 16; // 统一贴图尺寸

    // 生成纹理数组数据
    void BuildBlockTextureArray(Eng::service::AssetService* assets, 
                                const std::unordered_map<std::string, std::string>& tileTints);
    
    // 获取层索引
    int ResolveLayer(const std::string& name);
    
    // 获取总层数
    int GetTextureLayerCount();
    
    // 获取所有层级的连续像素数据
    const std::vector<uint8_t>& GetTextureArrayPixels();
}