#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace game::client {

inline constexpr int ATLAS_TILE = 16;

// 动态构建图集：由 tileTints 驱动（贴图名 → tint 字符串："none"/"grass"/"foliage"/"water"）
// 内部按名排序保证确定性。所有被引用的 tile 名都会分配一个 index，
// 若对应 PNG 找不到 → 像素全 0（按约定，渲染层补无效纹理）。
void BuildAtlas(const std::unordered_map<std::string, std::string>& tileTints);

// 贴图名 → 图集格索引；找不到返回 -1（调用方按 0 处理）
int ResolveTile(const std::string& name);

// 图集尺寸信息（BuildAtlas 之后有效）
int   AtlasCols();
int   AtlasRows();
float AtlasTileUV();             // 1.0f / Cols()
const std::vector<uint8_t>& AtlasPixels();

} // namespace game::client