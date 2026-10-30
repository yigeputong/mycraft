#pragma once
#include <string>
#include <functional>

namespace game {

struct ResourceLocation {
    std::string ns;   // 命名空间
    std::string path; // 路径

    ResourceLocation() = default;
    
    // 自动补全逻辑
    explicit ResourceLocation(const std::string& full) {
        size_t colon = full.find(':');
        if (colon != std::string::npos) {
            ns = full.substr(0, colon);
            path = full.substr(colon + 1);
        } else {
            // 如果没有冒号，默认命名空间为 global
            ns = "global";
            path = full;
        }
    }

    ResourceLocation(std::string n, std::string p) 
        : ns(std::move(n)), path(std::move(p)) {}

    [[nodiscard]] std::string toString() const { return ns + ":" + path; }
    [[nodiscard]] bool isGlobal() const { return ns == "global"; }
    [[nodiscard]] bool isMycraft() const { return ns == "mycraft"; }

    bool operator==(const ResourceLocation& o) const { return ns == o.ns && path == o.path; }
};

} // namespace game

namespace std {
    template<> struct hash<game::ResourceLocation> {
        size_t operator()(const game::ResourceLocation& rl) const {
            return hash<string>()(rl.ns) ^ (hash<string>()(rl.path) << 1);
        }
    };
}