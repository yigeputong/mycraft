#pragma once
#include "Engine.h"
#include "game/core/Blocks.h"
#include <vector>
#include <array>

namespace game::client {

// 代表一个面的基础数据
struct BlockFace {
    Eng::client::Vertex verts[4];
    glm::ivec3 normal;
    int u_axis, v_axis;
    int du[4], dv[4];
};

class MeshBuilderUtils {
public:
    static std::array<BlockFace, 6> GetBlockFaces(BlockType type, glm::vec3 center);

    static void AppendBlock(BlockType type, glm::vec3 center, std::vector<Eng::client::Vertex>& outV, std::vector<uint32_t>& outI);

    static void AppendSprite(BlockType type, glm::vec3 center, const glm::vec4& light, std::vector<Eng::client::Vertex>& outV, std::vector<uint32_t>& outI);

    static Eng::client::MeshData BuildItemMeshData(BlockType type);
};

} // namespace game::client