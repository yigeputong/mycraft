#pragma once
#include "Engine.h"
#include "game/core/BlockRegistry.h"
#include <vector>
#include <array>

namespace game::client {

struct BlockFace {
    Eng::client::Vertex verts[4];
    glm::ivec3 normal;
    int u_axis, v_axis;
    int du[4], dv[4];
};

class MeshBuilderUtils {
public:
    static std::array<BlockFace, 6> GetBlockFaces(
        const BlockRegistry& reg, BlockId id, glm::vec3 center);

    static void AppendBlock(
        const BlockRegistry& reg, BlockId id, glm::vec3 center,
        std::vector<Eng::client::Vertex>& outV, std::vector<uint32_t>& outI);

    static void AppendSprite(
        const BlockRegistry& reg, BlockId id, glm::vec3 center,
        const glm::vec4& light,
        std::vector<Eng::client::Vertex>& outV, std::vector<uint32_t>& outI);

    static void AppendTorch(
        const BlockRegistry& reg, BlockId id, int x, int y, int z,
        glm::vec4 light,
        std::vector<Eng::client::Vertex>& outV, std::vector<uint32_t>& outI);

    static Eng::client::MeshData BuildItemMeshData(
        const BlockRegistry& reg, BlockId id);
};

} // namespace game::client