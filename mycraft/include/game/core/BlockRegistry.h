#pragma once
#include <unordered_set>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include "ResourceLocation.h"
#include "core/Log.h"

namespace game {

using BlockId = uint16_t;
constexpr BlockId AIR_ID     = 0;
constexpr BlockId UNKNOWN_ID = 0xFFFF;   // 保留，JSON 中禁用

enum class ToolType : uint8_t { None, Pickaxe, Axe, Shovel, Hoe, Sword };
enum class TintMode : uint8_t { None = 0, Grass, Foliage, Water };

struct BlockFaceTex {
    std::string tile;
    TintMode    tint = TintMode::None;
};

struct BlockRender {
    BlockFaceTex all;
    BlockFaceTex top;
    BlockFaceTex bottom;
    BlockFaceTex side;

    bool isOpaque  = true;
    bool isCross   = false;
    bool isLiquid  = false;
    bool isLeaves  = false;
    int  luminance = 0;

    const BlockFaceTex& ForTop()    const { return all.tile.empty() ? top    : all; }
    const BlockFaceTex& ForBottom() const { return all.tile.empty() ? bottom : all; }
    const BlockFaceTex& ForSide()   const { return all.tile.empty() ? side   : all; }
};

struct BlockProperties {
    bool     isSolid     = true;
    float    hardness    = 0.0f;
    ToolType bestTool    = ToolType::None;
    int      harvestTier = 0;
};

struct BlockDefinition {
    BlockId          id = AIR_ID;
    ResourceLocation location{"mycraft", "air"};
    std::string      displayName = "Air";
    BlockRender      render;
    BlockProperties  props;
};

class BlockRegistry {
public:
    BlockRegistry();

    // 失败策略：致命错误 → logError 并跳过该文件；不抛异常、不中断。
    void LoadAll(const std::string& directory);

    [[nodiscard]] const BlockDefinition& Get(BlockId id) const;
    [[nodiscard]] const BlockDefinition& Get(const ResourceLocation& loc) const;
    [[nodiscard]] const BlockDefinition& Get(const std::string& idStr) const;  // "mycraft:stone"

    // 便捷判断（成员化，取代全局 IsSolid/IsPhysicsSolid）
    [[nodiscard]] bool IsSolid(BlockId id) const;
    [[nodiscard]] bool IsPhysicsSolid(BlockId id) const;
    [[nodiscard]] bool IsOpaque(BlockId id) const;

    std::unordered_map<std::string, std::string> CollectTileTints() const;

    [[nodiscard]] size_t                     Count()  const { return m_blocks.size(); }
    [[nodiscard]] const std::vector<BlockId>& AllIds() const { return m_sortedIds; }

private:
    std::unique_ptr<Eng::Log>    m_logger = std::make_unique<Eng::Log>();
    std::vector<BlockDefinition> m_blocks;
    std::unordered_set<BlockId>  m_registeredIds; // 仅用于加载时去重
    std::vector<BlockId>         m_sortedIds;
    BlockDefinition              m_fallback;

    void ParseOne(const std::string& filePath);   // 失败时抛 runtime_error，上层捕获
    [[nodiscard]] TintMode ParseTint(const std::string& s) const;
    [[nodiscard]] ToolType ParseTool(const std::string& s) const;
};

} // namespace game