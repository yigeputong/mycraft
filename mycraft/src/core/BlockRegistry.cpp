#include "game/core/BlockRegistry.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

namespace game {
namespace {

struct RawFaceTex { std::string tile; std::string tintStr; };

RawFaceTex ParseFaceTexRaw(const json& v) {
    RawFaceTex ft;
    if (v.is_string()) {
        ft.tile    = v.get<std::string>();
        ft.tintStr = "none";
    } else if (v.is_object()) {
        ft.tile    = v.value("tile", std::string{});
        ft.tintStr = v.value("tint", std::string{"none"});
    }
    return ft;
}

} // anonymous

// ---------- 枚举字符串解析（成员函数，可访问 m_logger） ----------
TintMode BlockRegistry::ParseTint(const std::string& s) const {
    if (s.empty() || s == "none") return TintMode::None;
    if (s == "grass")   return TintMode::Grass;
    if (s == "foliage") return TintMode::Foliage;
    if (s == "water")   return TintMode::Water;
    logWarning(m_logger.get(), "BlockRegistry: unknown tint '" << s << "', fallback to none");
    return TintMode::None;
}

ToolType BlockRegistry::ParseTool(const std::string& s) const {
    if (s.empty() || s == "none") return ToolType::None;
    if (s == "pickaxe") return ToolType::Pickaxe;
    if (s == "axe")     return ToolType::Axe;
    if (s == "shovel")  return ToolType::Shovel;
    if (s == "hoe")     return ToolType::Hoe;
    if (s == "sword")   return ToolType::Sword;
    logWarning(m_logger.get(), "BlockRegistry: unknown tool '" << s << "', fallback to none");
    return ToolType::None;
}

// ---------- 构造：初始化 fallback ----------
BlockRegistry::BlockRegistry() {
    m_fallback.id = UNKNOWN_ID;
    m_fallback.location = ResourceLocation{"mycraft", "unknown"};
    m_fallback.displayName = "Unknown";
    m_fallback.render.isOpaque  = false;
    m_fallback.render.isCross   = false;
    m_fallback.render.isLiquid  = false;
    m_fallback.render.isLeaves  = false;
    m_fallback.render.luminance = 0;
    m_fallback.props.isSolid     = false;
    m_fallback.props.hardness    = 0.0f;
    m_fallback.props.bestTool    = ToolType::None;
    m_fallback.props.harvestTier = 0;
}

// ---------- 加载 ----------
void BlockRegistry::LoadAll(const std::string& directory) {
    m_blocks.clear();
    m_sortedIds.clear();
    m_registeredIds.clear();

    fs::path dir(directory);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        logError(m_logger.get(), "BlockRegistry: assets dir not found: " << directory);
        return;
    }

    // 预分配一个合理的初始大小，比如 128
    m_blocks.resize(128);
    for (auto& b : m_blocks) b.id = UNKNOWN_ID; // 标记未使用

    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".json")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());

    for (auto& p : files) {
        try {
            ParseOne(p.string());
        } catch (const std::exception& e) {
            logError(m_logger.get(), "BlockRegistry: skip " << p.string()
                                     << " due to error: " << e.what());
        }
    }

    // 检查并补充 AIR_ID
    if (!m_registeredIds.count(AIR_ID)) {
        BlockDefinition air;
        air.id = AIR_ID;
        air.location = ResourceLocation{"mycraft", "air"};
        air.displayName = "Air";
        air.render.isOpaque = false;
        air.props.isSolid = false;
        
        if (m_blocks.size() <= AIR_ID) m_blocks.resize(AIR_ID + 1);
        m_blocks[AIR_ID] = air;
        m_registeredIds.insert(AIR_ID);
        logWarning(m_logger.get(), "BlockRegistry: air.json missing or invalid, synthesized default");
    }

    // 收集所有加载成功的 ID 用于排序
    for (size_t i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks[i].id != UNKNOWN_ID) {
            m_sortedIds.push_back(static_cast<BlockId>(i));
        }
    }

    logInfo(m_logger.get(), "BlockRegistry: loaded " << m_sortedIds.size() << " blocks");
}

void BlockRegistry::ParseOne(const std::string& filePath) {
    std::ifstream in(filePath);
    if (!in) throw std::runtime_error("cannot open file");

    json j;
    try { in >> j; }
    catch (const std::exception& e) {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }

    BlockDefinition def;
    def.id = j.at("id").get<BlockId>();
    if (def.id == UNKNOWN_ID)
        throw std::runtime_error("id 0xFFFF is reserved");

    std::string name = j.at("name").get<std::string>();
    auto colon = name.find(':');
    if (colon == std::string::npos)
        throw std::runtime_error("name must be ns:path");
    def.location = ResourceLocation{name.substr(0, colon), name.substr(colon + 1)};
    def.displayName = j.value("display_name", def.location.path);

    auto& r = j.at("render");
    if (r.contains("tiles")) {
        const auto& t = r.at("tiles");
        auto conv = [&](const json& jn, BlockFaceTex& out) {
            auto raw = ParseFaceTexRaw(jn);
            out.tile = std::move(raw.tile);
            out.tint = ParseTint(raw.tintStr);
        };
        if (t.contains("all"))    conv(t.at("all"),    def.render.all);
        if (t.contains("top"))    conv(t.at("top"),    def.render.top);
        if (t.contains("bottom")) conv(t.at("bottom"), def.render.bottom);
        if (t.contains("side"))   conv(t.at("side"),   def.render.side);
    }
    def.render.isOpaque  = r.value("opaque",   true);
    def.render.isCross   = r.value("cross",    false);
    def.render.isLiquid  = r.value("liquid",   false);
    def.render.isLeaves  = r.value("leaves",   false);
    def.render.luminance = r.value("luminance", 0);

    auto& p = j.at("properties");
    def.props.isSolid     = p.value("solid", true);
    def.props.hardness    = p.value("hardness", 0.0f);
    def.props.bestTool    = ParseTool(p.value("tool", std::string{"none"}));
    def.props.harvestTier = p.value("harvest_tier", 0);

    // 如果 ID 超出了当前 vector 大小，进行扩容
    if (def.id >= m_blocks.size()) {
        size_t oldSize = m_blocks.size();
        m_blocks.resize(def.id + 1);
        for (size_t i = oldSize; i < m_blocks.size(); ++i) {
            m_blocks[i].id = UNKNOWN_ID; // 标记新增的空洞
        }
    }

    m_blocks[def.id] = std::move(def);
    m_registeredIds.insert(m_blocks[def.id].id);
}

// ---------- 查询 ----------
const BlockDefinition& BlockRegistry::Get(BlockId id) const {
    if (id == UNKNOWN_ID || id >= m_blocks.size() || m_blocks[id].id == UNKNOWN_ID) {
        return m_fallback;
    }
    return m_blocks[id];
}

const BlockDefinition& BlockRegistry::Get(const ResourceLocation& loc) const {
    for (const auto& d : m_blocks) {
        if (d.id != UNKNOWN_ID && d.location == loc) return d;
    }
    return m_fallback;
}

const BlockDefinition& BlockRegistry::Get(const std::string& idStr) const {
    auto colon = idStr.find(':');
    if (colon == std::string::npos)
        return Get(ResourceLocation{"mycraft", idStr});
    return Get(ResourceLocation{idStr.substr(0, colon), idStr.substr(colon + 1)});
}

bool BlockRegistry::IsSolid(BlockId id) const {
    const auto& d = Get(id);
    return d.props.isSolid && !d.render.isLiquid;
}
bool BlockRegistry::IsPhysicsSolid(BlockId id) const { return IsSolid(id); }
bool BlockRegistry::IsOpaque(BlockId id) const { return Get(id).render.isOpaque; }

std::unordered_map<std::string, std::string> BlockRegistry::CollectTileTints() const {
    std::unordered_map<std::string, std::string> out;
    auto add = [&](const BlockFaceTex& ft) {
        if (ft.tile.empty()) return;
        const char* tintStr =
            ft.tint == TintMode::Grass   ? "grass"   :
            ft.tint == TintMode::Foliage ? "foliage" :
            ft.tint == TintMode::Water   ? "water"   : "none";
        auto it = out.find(ft.tile);
        if (it == out.end()) out[ft.tile] = tintStr;
        else if (it->second != tintStr)
            logWarning(m_logger, "BlockRegistry: tile '" << ft.tile
                       << "' has conflicting tints '" << it->second
                       << "' vs '" << tintStr << "', keeping first");
    };
    for (auto& d : m_blocks) {
        add(d.render.all); add(d.render.top); add(d.render.bottom); add(d.render.side);
    }
    return out;
}

} // namespace game