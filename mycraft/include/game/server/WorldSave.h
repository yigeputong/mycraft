#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace game::server {

struct BlockChange {
    int32_t  x, y, z;
    uint16_t block;
};

struct WorldMetadata {
    std::string worldId;       // 文件夹名，如 "world_1712345"
    std::string displayName;   // 玩家可见的名字
    uint32_t seed = 0;
    int64_t lastPlayedTime = 0; // Unix 时间戳（毫秒）
};

class WorldSave {
public:
    // 传入纯 ID（如 "world_123"），内部自动补全为 "saves/world_123"
    explicit WorldSave(std::string worldId);

    bool HasLevel()   const;
    bool HasChanges() const;

    // --- 硬锁机制（防止多开破坏存档） ---
    bool Lock();
    void Unlock();
    bool IsLocked() const;

    // --- 核心数据读写 ---
    // level.json (V2 格式，包含元数据)
    bool LoadLevel(WorldMetadata& outMeta, glm::vec3& outSpawn);
    void SaveLevel(const WorldMetadata& meta, const glm::vec3& spawn);

    // changes.json
    std::vector<BlockChange> LoadChanges();
    void SaveChanges(const std::vector<BlockChange>& changes);

    // players/<name>.json
    bool LoadPlayer(const std::string& name, glm::vec3& pos, float& yaw, float& pitch);
    void SavePlayer(const std::string& name, const glm::vec3& pos, float yaw, float pitch);

    // --- 静态工具方法（供 UI 层调用） ---
    
    // 扫描 saves/ 目录下所有合法的存档，按最后游玩时间降序排列
    static std::vector<WorldMetadata> ListAllWorlds();
    
    // 创建一个新世界，并生成对应的 level.json，返回 worldId
    static std::string CreateNewWorld(const std::string& displayName, const std::string& seedInput = "");
    
    // 删除整个世界文件夹（包含内部所有文件和玩家数据）
    static bool DeleteWorld(const std::string& worldId);

    const std::string& GetWorldId() const { return m_worldId; }

private:
    std::string m_worldId; // 纯 ID
    std::string m_dir;     // 完整相对路径 "saves/world_xxx"

    static constexpr const char* LEVEL_FILE    = "level.json";
    static constexpr const char* LEVEL_TMP     = "level.json.tmp";
    static constexpr const char* CHANGES_FILE  = "changes.json";
    static constexpr const char* LOCK_FILE     = "session.lock";
    static constexpr const char* PLAYERS_DIR   = "players";

    std::string LevelPath()   const { return m_dir + "/" + LEVEL_FILE; }
    std::string LevelTmpPath()const { return m_dir + "/" + LEVEL_TMP; }
    std::string ChangesPath() const { return m_dir + "/" + CHANGES_FILE; }
    std::string LockPath()    const { return m_dir + "/" + LOCK_FILE; }
    std::string PlayerPath(const std::string& name) const {
        return m_dir + "/" + PLAYERS_DIR + "/" + name + ".json";
    }
    static std::string SanitizeName(const std::string& in);
};

} // namespace game::server