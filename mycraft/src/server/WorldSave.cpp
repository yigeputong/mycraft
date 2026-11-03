#include "game/server/WorldSave.h"
#include "Engine/tool/FileTool.h"
#include "Engine/tool/Log.h"

#include <cctype>
#include <nlohmann/json.hpp>

// SDL3 的原生文件系统与 IO 接口
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

using json = nlohmann::json;

namespace game::server {

WorldSave::WorldSave(std::string dir) : m_dir(std::move(dir)) {
    // 清理掉可能存在的 "./" 前缀，防止沙盒路径拼接出错
    if (m_dir.starts_with("./")) {
        m_dir = m_dir.substr(2);
    }
    
    // 创建主存档目录
    std::string fullDir = Eng::tool::GetWritablePath(m_dir);
    SDL_CreateDirectory(fullDir.c_str());

    // 顺便把 players 子目录也建了
    std::string playersDir = Eng::tool::GetWritablePath(m_dir + "/players");
    SDL_CreateDirectory(playersDir.c_str());
}

std::string WorldSave::SanitizeName(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-') out += c;
    }
    return out.empty() ? "player" : out;
}

// 通过尝试打开文件来判断是否存在，代替 fs::exists
static bool FileExists(const std::string& path) {
    SDL_IOStream* io = SDL_IOFromFile(Eng::tool::GetWritablePath(path).c_str(), "r");
    if (io) {
        SDL_CloseIO(io);
        return true;
    }
    return false;
}

bool WorldSave::HasLevel()   const { return FileExists(LevelPath()); }
bool WorldSave::HasChanges() const { return FileExists(ChangesPath()); }

bool WorldSave::LoadLevel(uint32_t& outSeed, glm::vec3& outSpawn) {
    std::string path = Eng::tool::GetWritablePath(LevelPath());
    auto data = Eng::tool::ReadFileToMemory(path);
    if (data.empty()) return false;

    try {
        auto j = json::parse(data.begin(), data.end());
        outSeed  = j.value("seed", 0u);
        outSpawn = glm::vec3(
            j.value("spawnX", 0.0f),
            j.value("spawnY", 256.0f),
            j.value("spawnZ", 0.0f));
        return true;
    } catch (...) { return false; }
}

void WorldSave::SaveLevel(uint32_t seed, const glm::vec3& spawn) {
    json j;
    j["version"] = 1;
    j["seed"]    = seed;
    j["spawnX"]  = spawn.x;
    j["spawnY"]  = spawn.y;
    j["spawnZ"]  = spawn.z;
    
    std::string path = Eng::tool::GetWritablePath(LevelPath());
    Eng::tool::WriteStringToFile(path, j.dump(2));
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
    // 确保目录存在（虽然构造时建了，但这里再保个险）
    std::string playersDir = Eng::tool::GetWritablePath(m_dir + "/players");
    SDL_CreateDirectory(playersDir.c_str());
    
    json j;
    j["x"] = pos.x; j["y"] = pos.y; j["z"] = pos.z;
    j["yaw"] = yaw; j["pitch"] = pitch;
    
    std::string path = Eng::tool::GetWritablePath(PlayerPath(SanitizeName(name)));
    Eng::tool::WriteStringToFile(path, j.dump(2));
}

} // namespace game::server