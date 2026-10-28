#include "game/client/MeshBuilderUtils.h"
#include "game/client/TextureAtlas.h"

namespace game::client {

static constexpr float faceOffsets[6][4][3] = {
    {{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f}},
    {{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},{-0.5f,-0.5f,-0.5f}},
    {{-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f}},
    {{-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}},
    {{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}},
    {{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f}},
};
static constexpr int faceNormals[6][3] = {
    { 1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
};
static constexpr float faceUVs[4][2] = {
    {0,0},{0,1},{1,1},{1,0}
};

std::array<BlockFace, 6> MeshBuilderUtils::GetBlockFaces(BlockType type, glm::vec3 center) {
    std::array<BlockFace, 6> faces;
    for (int f = 0; f < 6; ++f) {
        int N_axis = (faceNormals[f][0] != 0) ? 0 : (faceNormals[f][1] != 0) ? 1 : 2;
        faces[f].u_axis = (N_axis + 1) % 3;
        faces[f].v_axis = (N_axis + 2) % 3;
        faces[f].normal = glm::ivec3(faceNormals[f][0], faceNormals[f][1], faceNormals[f][2]);

        int tile = TileForBlock(type, f);
        
        // ★ 修复1：必须用整数除法算 row 和 col
        int tileRow = tile / ATLAS_COLS;
        int tileCol = tile % ATLAS_COLS;
        float u0 = static_cast<float>(tileCol) * TILE_UV;
        float v0 = static_cast<float>(tileRow) * TILE_UV;

        for (int v = 0; v < 4; ++v) {
            // ★ 修复2：浮点数判断必须用 < 0.0f
            faces[f].du[v] = (faceOffsets[f][v][faces[f].u_axis] < 0.0f) ? -1 : 1;
            faces[f].dv[v] = (faceOffsets[f][v][faces[f].v_axis] < 0.0f) ? -1 : 1;

            Eng::client::Vertex vert;
            vert.position = center + glm::vec3(faceOffsets[f][v][0], faceOffsets[f][v][1], faceOffsets[f][v][2]);
            vert.normal = glm::vec3(faceNormals[f][0], faceNormals[f][1], faceNormals[f][2]);
            vert.uv = { u0 + faceUVs[v][0] * TILE_UV, v0 + (1.0f - faceUVs[v][1]) * TILE_UV };
            vert.ao = 1.0f;
            vert.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            faces[f].verts[v] = vert;
        }
    }
    return faces;
}

void MeshBuilderUtils::AppendBlock(BlockType type, glm::vec3 c, std::vector<Eng::client::Vertex>& outV, std::vector<uint32_t>& outI) {
    auto faces = GetBlockFaces(type, c);
    for (int f = 0; f < 6; ++f) {
        uint32_t base = (uint32_t)outV.size();
        outV.insert(outV.end(), std::begin(faces[f].verts), std::end(faces[f].verts));
        outI.insert(outI.end(), { base+0, base+1, base+2, base+2, base+3, base+0 });
    }
}


void MeshBuilderUtils::AppendSprite(BlockType type, glm::vec3 c, const glm::vec4& light,
                                    std::vector<Eng::client::Vertex>& outV, 
                                    std::vector<uint32_t>& outI) {
    int tile = TileForBlock(type, 0);
    float u0 = (tile % ATLAS_COLS) * TILE_UV;
    float v0 = (tile / static_cast<float>(ATLAS_COLS)) * TILE_UV;
    float u1 = u0 + TILE_UV;
    float v1 = v0 + TILE_UV;

    float s, y0, y1;
    if (type == BlockType::Torch) {
        s = 0.15f;          // 宽 0.3
        y0 = c.y - 0.375f;  
        y1 = c.y + 0.375f;  // 高 0.75
    } else {
        s = 0.35f;          // 宽 0.7
        y0 = c.y - 0.5f;    
        y1 = c.y + 0.5f;    // 高 1.0
    }

    auto pushQuad = [&](glm::vec3 p1, glm::vec2 uv1, glm::vec3 p2, glm::vec2 uv2,
                        glm::vec3 p3, glm::vec2 uv3, glm::vec3 p4, glm::vec2 uv4, glm::vec3 n) {
        uint32_t base = (uint32_t)outV.size();
        auto pushV = [&](glm::vec3 p, glm::vec2 uv) {
            Eng::client::Vertex v;
            v.position = p; v.normal = n; v.uv = uv; v.ao = 1.0f;
            v.light = light; // ★ 使用传入的光照
            outV.push_back(v);
        };
        pushV(p1, uv1); pushV(p2, uv2); pushV(p3, uv3); pushV(p4, uv4);
        outI.insert(outI.end(), { base+0, base+1, base+2, base+2, base+3, base+0 });
    };

    // 交叉面片
    pushQuad({c.x-s, y0, c.z}, {u0, v1}, {c.x+s, y0, c.z}, {u1, v1}, {c.x+s, y1, c.z}, {u1, v0}, {c.x-s, y1, c.z}, {u0, v0}, {0,0,1});
    pushQuad({c.x+s, y0, c.z}, {u1, v1}, {c.x-s, y0, c.z}, {u0, v1}, {c.x-s, y1, c.z}, {u0, v0}, {c.x+s, y1, c.z}, {u1, v0}, {0,0,-1});
    pushQuad({c.x, y0, c.z-s}, {u0, v1}, {c.x, y0, c.z+s}, {u1, v1}, {c.x, y1, c.z+s}, {u1, v0}, {c.x, y1, c.z-s}, {u0, v0}, {1,0,0});
    pushQuad({c.x, y0, c.z+s}, {u1, v1}, {c.x, y0, c.z-s}, {u0, v1}, {c.x, y1, c.z-s}, {u0, v0}, {c.x, y1, c.z+s}, {u1, v0}, {-1,0,0});
}

Eng::client::MeshData MeshBuilderUtils::BuildItemMeshData(BlockType type) {
    Eng::client::MeshData data;
    bool isSprite = (type == BlockType::TallGrass || type == BlockType::FlowerRed || 
                     type == BlockType::FlowerYellow || type == BlockType::Torch);
                     
    if (isSprite) {
        glm::vec4 light = (type == BlockType::Torch) ? glm::vec4(0.0f, 1.0f, 0.6f, 0.2f) : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        AppendSprite(type, glm::vec3(0.0f), light, data.vertices, data.indices);
    } else {
        AppendBlock(type, glm::vec3(0.0f), data.vertices, data.indices);
    }
    return data;
}

} // namespace game::client