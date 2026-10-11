#include "game/server/WorldSave.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace game::server {

WorldSave::WorldSave(std::string dir) : m_dir(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(m_dir, ec);
}

std::string WorldSave::SanitizeName(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-') out += c;
    }
    return out.empty() ? "player" : out;
}

bool WorldSave::HasLevel()   const { return fs::exists(LevelPath()); }
bool WorldSave::HasChanges() const { return fs::exists(ChangesPath()); }

bool WorldSave::LoadLevel(uint32_t& outSeed, glm::vec3& outSpawn) {
    if (!HasLevel()) return false;
    try {
        std::ifstream f(LevelPath());
        json j; f >> j;
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
    std::ofstream f(LevelPath(), std::ios::trunc);
    f << j.dump(2);
}

std::vector<BlockChange> WorldSave::LoadChanges() {
    std::vector<BlockChange> out;
    if (!HasChanges()) return out;
    try {
        std::ifstream f(ChangesPath());
        json j; f >> j;
        for (auto& e : j) {
            BlockChange c;
            c.x     = e.value("x", 0);
            c.y     = e.value("y", 0);
            c.z     = e.value("z", 0);
            c.block = e.value("b", (uint16_t)0);
            out.push_back(c);
        }
    } catch (...) {}
    return out;
}

void WorldSave::SaveChanges(const std::vector<BlockChange>& changes) {
    json j = json::array();
    for (const auto& c : changes) {
        j.push_back({ {"x", c.x}, {"y", c.y}, {"z", c.z}, {"b", c.block} });
    }
    std::ofstream f(ChangesPath(), std::ios::trunc);
    f << j.dump();
}

bool WorldSave::LoadPlayer(const std::string& name, glm::vec3& pos,
                            float& yaw, float& pitch) {
    std::string path = PlayerPath(SanitizeName(name));
    if (!fs::exists(path)) return false;
    try {
        std::ifstream f(path);
        json j; f >> j;
        pos   = glm::vec3(j.value("x", 0.0f), j.value("y", 256.0f), j.value("z", 0.0f));
        yaw   = j.value("yaw", 0.0f);
        pitch = j.value("pitch", 0.0f);
        return true;
    } catch (...) { return false; }
}

void WorldSave::SavePlayer(const std::string& name, const glm::vec3& pos,
                            float yaw, float pitch) {
    std::error_code ec;
    fs::create_directories(m_dir + "/players", ec);
    json j;
    j["x"] = pos.x; j["y"] = pos.y; j["z"] = pos.z;
    j["yaw"] = yaw; j["pitch"] = pitch;
    std::ofstream f(PlayerPath(SanitizeName(name)), std::ios::trunc);
    f << j.dump(2);
}

}