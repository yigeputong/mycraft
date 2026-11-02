#include "game/client/MeshBuilderUtils.h"
#include "game/client/TextureAtlas.h"

namespace game::client {

namespace {

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

    // f: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
    static int ResolveFaceTile(const BlockRender& r, int f) {
        const BlockFaceTex& ft = (f == 2) ? r.ForTop()
                            : (f == 3) ? r.ForBottom()
                                        : r.ForSide();
        if (ft.tile.empty()) return -1;
        return ResolveTile(ft.tile);
    }

}

std::array<BlockFace, 6> MeshBuilderUtils::GetBlockFaces(
    const BlockRegistry& reg, BlockId id, glm::vec3 center)
{
    std::array<BlockFace, 6> faces;
    const auto& def = reg.Get(id);
    
    for (int f = 0; f < 6; ++f) {
        int N_axis = (faceNormals[f][0] != 0) ? 0 : (faceNormals[f][1] != 0) ? 1 : 2;
        faces[f].u_axis = (N_axis + 1) % 3;
        faces[f].v_axis = (N_axis + 2) % 3;
        faces[f].normal = glm::ivec3(faceNormals[f][0], faceNormals[f][1], faceNormals[f][2]);

        int tile = ResolveFaceTile(def.render, f);
        if (tile < 0) tile = 0;

        // ★ 使用新的 GetTileUV
        float u0, v0, u1, v1;
        GetTileUV(tile, u0, v0, u1, v1);
        float uScale = u1 - u0;
        float vScale = v1 - v0;

        for (int v = 0; v < 4; ++v) {
            faces[f].du[v] = (faceOffsets[f][v][faces[f].u_axis] < 0.0f) ? -1 : 1;
            faces[f].dv[v] = (faceOffsets[f][v][faces[f].v_axis] < 0.0f) ? -1 : 1;

            Eng::service::Vertex vert;
            vert.position = center + glm::vec3(faceOffsets[f][v][0], faceOffsets[f][v][1], faceOffsets[f][v][2]);
            vert.normal = glm::vec3(faceNormals[f][0], faceNormals[f][1], faceNormals[f][2]);
            
            // ★ 使用真实内缩后的尺寸
            vert.uv = { u0 + faceUVs[v][0] * uScale, v0 + (1.0f - faceUVs[v][1]) * vScale };
            vert.ao = 1.0f;
            vert.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            faces[f].verts[v] = vert;
        }
    }
    return faces;
}

void MeshBuilderUtils::AppendBlock(
    const BlockRegistry& reg, BlockId id, glm::vec3 c,
    std::vector<Eng::service::Vertex>& outV, std::vector<uint32_t>& outI)
{
    auto faces = GetBlockFaces(reg, id, c);
    for (int f = 0; f < 6; ++f) {
        uint32_t base = static_cast<uint32_t>(outV.size());
        outV.insert(outV.end(), std::begin(faces[f].verts), std::end(faces[f].verts));
        outI.insert(outI.end(), { base+0, base+1, base+2, base+2, base+3, base+0 });
    }
}

void MeshBuilderUtils::AppendSprite(
    const BlockRegistry& reg, BlockId id, glm::vec3 c, const glm::vec4& light,
    std::vector<Eng::service::Vertex>& outV, std::vector<uint32_t>& outI)
{
    const auto& def = reg.Get(id);
    const bool isTorch = def.render.isCross && def.render.luminance > 0;

    // 十字模型：取 side tile（all 覆盖时也是 side）
    const BlockFaceTex& ft = def.render.ForSide();
    int tile = ft.tile.empty() ? 0 : ResolveTile(ft.tile);
    if (tile < 0) tile = 0;

    float u0, v0, u1, v1;
    GetTileUV(tile, u0, v0, u1, v1);

    float s, y0, y1;
    if (isTorch) {
        s = 0.15f;
        y0 = c.y - 0.375f;
        y1 = c.y + 0.375f;
    } else {
        s = 0.35f;
        y0 = c.y - 0.5f;
        y1 = c.y + 0.5f;
    }

    auto pushQuad = [&](glm::vec3 p1, glm::vec2 uv1, glm::vec3 p2, glm::vec2 uv2,
                        glm::vec3 p3, glm::vec2 uv3, glm::vec3 p4, glm::vec2 uv4, glm::vec3 n) {
        uint32_t base = static_cast<uint32_t>(outV.size());
        auto pushV = [&](glm::vec3 p, glm::vec2 uv) {
            Eng::service::Vertex v;
            v.position = p; v.normal = n; v.uv = uv; v.ao = 1.0f;
            v.light = light;
            outV.push_back(v);
        };
        pushV(p1, uv1); pushV(p2, uv2); pushV(p3, uv3); pushV(p4, uv4);
        outI.insert(outI.end(), { base+0, base+1, base+2, base+2, base+3, base+0 });
    };

    pushQuad({c.x-s, y0, c.z}, {u0, v1}, {c.x+s, y0, c.z}, {u1, v1},
             {c.x+s, y1, c.z}, {u1, v0}, {c.x-s, y1, c.z}, {u0, v0}, {0,0,1});
    pushQuad({c.x+s, y0, c.z}, {u1, v1}, {c.x-s, y0, c.z}, {u0, v1},
             {c.x-s, y1, c.z}, {u0, v0}, {c.x+s, y1, c.z}, {u1, v0}, {0,0,-1});
    pushQuad({c.x, y0, c.z-s}, {u0, v1}, {c.x, y0, c.z+s}, {u1, v1},
             {c.x, y1, c.z+s}, {u1, v0}, {c.x, y1, c.z-s}, {u0, v0}, {1,0,0});
    pushQuad({c.x, y0, c.z+s}, {u1, v1}, {c.x, y0, c.z-s}, {u0, v1},
             {c.x, y1, c.z-s}, {u0, v0}, {c.x, y1, c.z+s}, {u1, v0}, {-1,0,0});
}

void MeshBuilderUtils::AppendTorch(
    const BlockRegistry& reg, BlockId id, int x, int y, int z, glm::vec4 light,
    std::vector<Eng::service::Vertex>& outV, std::vector<uint32_t>& outI)
{
    const auto& def = reg.Get(id);
    const BlockFaceTex& ft = def.render.ForSide();
    int tile = ft.tile.empty() ? 0 : ResolveTile(ft.tile);
    if (tile < 0) tile = 0;

    // ★ 替换为 GetTileUV
    float u0, v0, u1, v1;
    GetTileUV(tile, u0, v0, u1, v1);
    float TILE_UV = u1 - u0; // 真实贴图宽度

    // 原图 16x16：木棍在 x∈[7,9]，火焰在 v∈[0,6]
    constexpr float PIXEL_7  = 7.0f / 16.0f;
    constexpr float PIXEL_9  = 9.0f / 16.0f;
    constexpr float PIXEL_6  = 6.0f / 16.0f;

    float su_min = u0 + PIXEL_7 * TILE_UV;
    float su_max = u0 + PIXEL_9 * TILE_UV;

    float sv_min = v0 + PIXEL_6 * TILE_UV;
    float sv_max = v1; // 相当于 v0 + TILE_UV
    
    float tv_min = v0;
    float tv_max = v0 + PIXEL_6 * TILE_UV;

    glm::vec3 c = glm::vec3(x + 0.5f, y + 0.3125f, z + 0.5f);
    float hw = 0.0625f, hh = 0.3125f, hd = 0.0625f;

    auto addFace = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                       glm::vec3 norm, float umin, float umax, float vmin, float vmax) {
        uint32_t base = static_cast<uint32_t>(outV.size());
        outV.push_back({p0, norm, {umin, vmax}, 1.0f, light});
        outV.push_back({p1, norm, {umax, vmax}, 1.0f, light});
        outV.push_back({p2, norm, {umax, vmin}, 1.0f, light});
        outV.push_back({p3, norm, {umin, vmin}, 1.0f, light});
        outI.insert(outI.end(), {base, base+1, base+2, base+2, base+3, base});
    };

    addFace({c.x+hw, c.y-hh, c.z-hd}, {c.x+hw, c.y-hh, c.z+hd}, {c.x+hw, c.y+hh, c.z+hd}, {c.x+hw, c.y+hh, c.z-hd}, {1,0,0}, su_min, su_max, sv_min, sv_max);
    addFace({c.x-hw, c.y-hh, c.z+hd}, {c.x-hw, c.y-hh, c.z-hd}, {c.x-hw, c.y+hh, c.z-hd}, {c.x-hw, c.y+hh, c.z+hd}, {-1,0,0}, su_min, su_max, sv_min, sv_max);
    addFace({c.x-hw, c.y-hh, c.z+hd}, {c.x+hw, c.y-hh, c.z+hd}, {c.x+hw, c.y+hh, c.z+hd}, {c.x-hw, c.y+hh, c.z+hd}, {0,0,1}, su_min, su_max, sv_min, sv_max);
    addFace({c.x+hw, c.y-hh, c.z-hd}, {c.x-hw, c.y-hh, c.z-hd}, {c.x-hw, c.y+hh, c.z-hd}, {c.x+hw, c.y+hh, c.z-hd}, {0,0,-1}, su_min, su_max, sv_min, sv_max);
    addFace({c.x-hw, c.y+hh, c.z-hd}, {c.x+hw, c.y+hh, c.z-hd}, {c.x+hw, c.y+hh, c.z+hd}, {c.x-hw, c.y+hh, c.z+hd}, {0,1,0}, su_min, su_max, tv_min, tv_max);
}

Eng::service::MeshData MeshBuilderUtils::BuildItemMeshData(
    const BlockRegistry& reg, BlockId id)
{
    Eng::service::MeshData data;
    const auto& def = reg.Get(id);

    if (def.render.isCross) {
        bool isTorch = def.render.luminance > 0;
        glm::vec4 light = isTorch ? glm::vec4(0.0f, 1.0f, 0.6f, 0.2f)
                                  : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        AppendSprite(reg, id, glm::vec3(0.0f), light, data.vertices, data.indices);
    } else {
        AppendBlock(reg, id, glm::vec3(0.0f), data.vertices, data.indices);
    }
    return data;
}

} // namespace game::client