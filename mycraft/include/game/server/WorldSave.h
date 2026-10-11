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

class WorldSave {
public:
    explicit WorldSave(std::string dir);

    bool HasLevel()   const;
    bool HasChanges() const;

    // level.json
    bool LoadLevel(uint32_t& outSeed, glm::vec3& outSpawn);
    void SaveLevel(uint32_t seed, const glm::vec3& spawn);

    // changes.json
    std::vector<BlockChange> LoadChanges();
    void SaveChanges(const std::vector<BlockChange>& changes);

    // players/<name>.json
    bool LoadPlayer(const std::string& name, glm::vec3& pos, float& yaw, float& pitch);
    void SavePlayer(const std::string& name, const glm::vec3& pos, float yaw, float pitch);

private:
    std::string m_dir;
    std::string LevelPath()   const { return m_dir + "/level.json"; }
    std::string ChangesPath() const { return m_dir + "/changes.json"; }
    std::string PlayerPath(const std::string& name) const {
        return m_dir + "/players/" + name + ".json";
    }
    static std::string SanitizeName(const std::string& in);
};

}