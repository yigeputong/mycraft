#include "game/server/WorldSave.h"
#include "Engine/tool/FileTool.h"
#include "Engine/tool/Log.h"

#include <cctype>
#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

using json = nlohmann::json;

namespace game::server {

namespace {
    constexpr const char* SAVES_DIR = "saves";

    // 通过尝试打开文件来判断是否存在，代替 fs::exists
    bool FileExists(const std::string& path) {
        SDL_IOStream* io = SDL_IOFromFile(Eng::tool::GetWritablePath(path).c_str(), "r");
        if (io) {
            SDL_CloseIO(io);
            return true;
        }
        return false;
    }

    // 生成跨平台一致的 xorshift32 种子
    uint32_t GenerateSeed() {
        static uint32_t state = static_cast<uint32_t>(
            std::chrono::system_clock::now().time_since_epoch().count()
        );
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    uint32_t HashStringToSeed(const std::string& str) {
        uint32_t hash = 2166136261u;
        for (char c : str) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }
}

WorldSave::WorldSave(std::string worldId) : m_worldId(std::move(worldId)) {
    // 清理可能带入的 "./" 或 "saves/" 前缀，保证纯 ID
    if (m_worldId.starts_with("./")) {
        m_worldId = m_worldId.substr(2);
    }
    if (m_worldId.starts_with("saves/")) {
        m_worldId = m_worldId.substr(6);
    }

    m_dir = std::string(SAVES_DIR) + "/" + m_worldId;

    // 创建目录
    std::string fullDir = Eng::tool::GetWritablePath(m_dir);
    SDL_CreateDirectory(fullDir.c_str());

    std::string playersPath = m_dir + "/" + PLAYERS_DIR;
    std::string fullPlayersDir = Eng::tool::GetWritablePath(playersPath);
    SDL_CreateDirectory(fullPlayersDir.c_str());
}

std::string WorldSave::SanitizeName(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-') out += c;
    }
    return out.empty() ? "player" : out;
}

bool WorldSave::HasLevel()   const { return FileExists(LevelPath()); }
bool WorldSave::HasChanges() const { return FileExists(ChangesPath()); }
bool WorldSave::IsLocked()   const { return FileExists(LockPath()); }

bool WorldSave::Lock() {
    if (IsLocked()) return false;
    std::string path = Eng::tool::GetWritablePath(LockPath());
    Eng::tool::WriteStringToFile(path, "locked");
    return true;
}

void WorldSave::Unlock() {
    std::string path = Eng::tool::GetWritablePath(LockPath());
    SDL_RemovePath(path.c_str());
}

bool WorldSave::LoadLevel(WorldMetadata& outMeta, glm::vec3& outSpawn) {
    std::string path = Eng::tool::GetWritablePath(LevelPath());
    auto data = Eng::tool::ReadFileToMemory(path);
    if (data.empty()) return false;

    try {
        auto j = json::parse(data.begin(), data.end());
        outMeta.worldId        = m_worldId;
        outMeta.displayName    = j.value("name", m_worldId); // 兼容旧存档
        outMeta.seed           = j.value("seed", 0u);
        outMeta.lastPlayedTime = j.value("lastPlayed", (int64_t)0);
        
        outSpawn = glm::vec3(
            j.value("spawnX", 0.0f),
            j.value("spawnY", 256.0f),
            j.value("spawnZ", 0.0f));
        return true;
    } catch (...) { return false; }
}

void WorldSave::SaveLevel(const WorldMetadata& meta, const glm::vec3& spawn) {
    json j;
    j["version"]    = 2;
    j["name"]       = meta.displayName;
    j["seed"]       = meta.seed;
    j["lastPlayed"] = meta.lastPlayedTime;
    j["spawnX"]     = spawn.x;
    j["spawnY"]     = spawn.y;
    j["spawnZ"]     = spawn.z;
    
    std::string tmpPath = Eng::tool::GetWritablePath(LevelTmpPath());
    std::string realPath = Eng::tool::GetWritablePath(LevelPath());

    // 原子写入：先写 .tmp，成功后直接覆盖
    if (Eng::tool::WriteStringToFile(tmpPath, j.dump(2))) {
        SDL_RenamePath(tmpPath.c_str(), realPath.c_str());
    }
}

std::vector<BlockChange> WorldSave::LoadChanges() {
    std::vector<BlockChange> out;
    std::string path = Eng::tool::GetWritablePath(ChangesPath());
    auto data = Eng::tool::ReadFileToMemory(path);
    if (data.empty()) return out;

    try {
        auto j = json::parse(data.begin(), data.end());
        for (auto& e : j) {
            BlockChange c;
            c.x     = e.value("x", 0);
            c.y     = e.value("y", 0);
            c.z     = e.value("z", 0);
            c.block = e.value("b", (uint16_t)0);
            out.push_back(c);
        }
    } catch (const std::exception& e) {
        stLogWarning("[WorldSave] Failed to parse changes.json: " << e.what());
    }
    return out;
}

void WorldSave::SaveChanges(const std::vector<BlockChange>& changes) {
    json j = json::array();
    for (const auto& c : changes) {
        j.push_back({ {"x", c.x}, {"y", c.y}, {"z", c.z}, {"b", c.block} });
    }
    std::string path = Eng::tool::GetWritablePath(ChangesPath());
    Eng::tool::WriteStringToFile(path, j.dump());
}

bool WorldSave::LoadPlayer(const std::string& name, glm::vec3& pos, float& yaw, float& pitch) {
    std::string path = Eng::tool::GetWritablePath(PlayerPath(SanitizeName(name)));
    auto data = Eng::tool::ReadFileToMemory(path);
    if (data.empty()) return false;

    try {
        auto j = json::parse(data.begin(), data.end());
        pos   = glm::vec3(j.value("x", 0.0f), j.value("y", 256.0f), j.value("z", 0.0f));
        yaw   = j.value("yaw", 0.0f);
        pitch = j.value("pitch", 0.0f);
        return true;
    } catch (...) { return false; }
}

void WorldSave::SavePlayer(const std::string& name, const glm::vec3& pos, float yaw, float pitch) {
    std::string playersDir = Eng::tool::GetWritablePath(m_dir + "/" + PLAYERS_DIR);
    SDL_CreateDirectory(playersDir.c_str());
    
    json j;
    j["x"] = pos.x; j["y"] = pos.y; j["z"] = pos.z;
    j["yaw"] = yaw; j["pitch"] = pitch;
    
    std::string path = Eng::tool::GetWritablePath(PlayerPath(SanitizeName(name)));
    Eng::tool::WriteStringToFile(path, j.dump(2));
}

// ============================================================
// 静态方法
// ============================================================

std::vector<WorldMetadata> WorldSave::ListAllWorlds() {
    std::vector<WorldMetadata> out;
    std::string savesDir = Eng::tool::GetWritablePath(SAVES_DIR);
    SDL_CreateDirectory(savesDir.c_str());

    SDL_EnumerateDirectory(savesDir.c_str(), 
        [](void* userdata, const char* /*dirname*/, const char* fname) -> SDL_EnumerationResult {
            auto* list = static_cast<std::vector<WorldMetadata>*>(userdata);
            std::string id(fname);
            
            WorldSave ws(id);
            WorldMetadata meta;
            glm::vec3 spawn;
            if (ws.LoadLevel(meta, spawn)) {
                list->push_back(meta);
            }
            return SDL_ENUM_CONTINUE;
        }, &out);

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.lastPlayedTime > b.lastPlayedTime;
    });
    return out;
}

std::string WorldSave::CreateNewWorld(const std::string& displayName, const std::string& seedInput) {
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
        
    std::string worldId = "world_" + std::to_string(now);
    WorldSave ws(worldId);
    
    WorldMetadata meta;
    meta.worldId        = worldId;
    meta.displayName    = displayName.empty() ? "New World" : displayName;
    meta.lastPlayedTime = now;

    // 处理种子逻辑
    if (seedInput.empty()) {
        meta.seed = GenerateSeed(); // 留空则随机
    } else {
        // 如果全数字，就尝试转成数字；否则用哈希
        try {
            size_t pos = 0;
            long long val = std::stoll(seedInput, &pos);
            if (pos == seedInput.length()) {
                meta.seed = static_cast<uint32_t>(val);
            } else {
                meta.seed = HashStringToSeed(seedInput);
            }
        } catch (...) {
            meta.seed = HashStringToSeed(seedInput);
        }
    }
    
    glm::vec3 spawn(0.0f, 256.0f, 0.0f);
    ws.SaveLevel(meta, spawn);
    return worldId;
}

bool WorldSave::DeleteWorld(const std::string& worldId) {
    std::string dir = Eng::tool::GetWritablePath(std::string(SAVES_DIR) + "/" + worldId);
    std::vector<std::string> pathsToDelete;

    // 1. 收集 players 文件夹下的文件
    std::string playersDir = dir + "/" + PLAYERS_DIR;
    SDL_EnumerateDirectory(playersDir.c_str(), [](void* userdata, const char* dirname, const char* fname) -> SDL_EnumerationResult {
        auto* list = static_cast<std::vector<std::string>*>(userdata);
        list->push_back(std::string(dirname) + "/" + fname);
        return SDL_ENUM_CONTINUE;
    }, &pathsToDelete);
    pathsToDelete.push_back(playersDir); // players 文件夹本身也要删

    // 2. 收集根目录下的文件
    SDL_EnumerateDirectory(dir.c_str(), [](void* userdata, const char* dirname, const char* fname) -> SDL_EnumerationResult {
        auto* list = static_cast<std::vector<std::string>*>(userdata);
        std::string filename(fname);
        if (filename != PLAYERS_DIR) { // 避免重复添加 players 文件夹
            list->push_back(std::string(dirname) + "/" + fname);
        }
        return SDL_ENUM_CONTINUE;
    }, &pathsToDelete);

    // 3. 统一删除（先删文件，再删空目录）
    for (const auto& path : pathsToDelete) {
        SDL_RemovePath(path.c_str());
    }
    
    // 4. 最后删除世界文件夹本身
    return SDL_RemovePath(dir.c_str());
}

} // namespace game::server