#include "game/client/MeshBuilderUtils.h"
#include "game/client/TextureArray.h"

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
    static int ResolveFaceLayer(const BlockRender& r, int f) {
        const BlockFaceTex& ft = (f == 2) ? r.ForTop()
                            : (f == 3) ? r.ForBottom()
                                        : r.ForSide();
        if (ft.tile.empty()) return -1;
        return ResolveLayer(ft.tile); // ★ 改为 ResolveLayer
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

        int layer = ResolveFaceLayer(def.render, f);
        if (layer < 0) layer = 0;

        for (int v = 0; v < 4; ++v) {
            faces[f].du[v] = (faceOffsets[f][v][faces[f].u_axis] < 0.0f) ? -1 : 1;
            faces[f].dv[v] = (faceOffsets[f][v][faces[f].v_axis] < 0.0f) ? -1 : 1;

            Eng::service::Vertex vert;
            vert.position = center + glm::vec3(faceOffsets[f][v][0], faceOffsets[f][v][1], faceOffsets[f][v][2]);
            vert.normal = glm::vec3(faceNormals[f][0], faceNormals[f][1], faceNormals[f][2]);
            
            // ★ 直接使用 0~1 的 UV
            vert.uv = { faceUVs[v][0], 1.0f - faceUVs[v][1] };
            vert.layer = static_cast<float>(layer); // ★ 赋值 layer
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

    const BlockFaceTex& ft = def.render.ForSide();
    int layer = ft.tile.empty() ? 0 : ResolveLayer(ft.tile);
    if (layer < 0) layer = 0;

    // ★ 不再需要 GetTileUV，直接固定 0~1
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;

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
            v.position = p; v.normal = n; v.uv = uv; 
            v.layer = static_cast<float>(layer); // ★ 加上 layer
            v.ao = 1.0f;
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
    int layer = ft.tile.empty() ? 0 : ResolveLayer(ft.tile);
    if (layer < 0) layer = 0;

    // 原图 16x16：木棍在 x∈[7,9]，火焰在 v∈[0,6]
    constexpr float PIXEL_7  = 7.0f / 16.0f;
    constexpr float PIXEL_9  = 9.0f / 16.0f;
    constexpr float PIXEL_6  = 6.0f / 16.0f;

    // 在 Texture Array 中，UV 范围就是 0~1
    float su_min = PIXEL_7;
    float su_max = PIXEL_9;

    float sv_min = PIXEL_6;
    float sv_max = 1.0f; 
    
    float tv_min = 0.0f;
    float tv_max = PIXEL_6;

    glm::vec3 c = glm::vec3(x + 0.5f, y + 0.3125f, z + 0.5f);
    float hw = 0.0625f, hh = 0.3125f, hd = 0.0625f;

    auto addFace = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                       glm::vec3 norm, float umin, float umax, float vmin, float vmax) {
        uint32_t base = static_cast<uint32_t>(outV.size());
        
        // ★ 修复初始化列表报错：位置、法线、uv、layer、ao、light
        outV.push_back({p0, norm, {umin, vmax}, static_cast<float>(layer), 1.0f, light});
        outV.push_back({p1, norm, {umax, vmax}, static_cast<float>(layer), 1.0f, light});
        outV.push_back({p2, norm, {umax, vmin}, static_cast<float>(layer), 1.0f, light});
        outV.push_back({p3, norm, {umin, vmin}, static_cast<float>(layer), 1.0f, light});
        
        outI.insert(outI.end(), {base, base+1, base+2, base+2, base+3, base});
    };

    addFace({c.x+hw, c.y-hh, c.z-hd}, {c.x+hw, c.y-hh, c.z+hd}, {c.x+hw, c.y+hh, c.z+hd}, {c.x+hw, c.y+hh, c.z-hd}, {1,0,0}, su_min, su_max, sv_min, sv_max);
    addFace({c.x-hw, c.y-hh, c.z+hd}, {c.x-hw, c.y-hh, c.z-hd}, {c.x-hw, c.y+hh, c.z-hd}, {c.x-hw, c.y+hh, c.z+hd}, {-1,0,0}, su_min, su_max, sv_min, sv_max);
    addFace({c.x-hw, c.y-hh, c.z+hd}, {c.x+hw, c.y-hh, c.z+hd}, {c.x+hw, c.y+hh, c.z+hd}, {c.x-hw, c.y+hh, c.z+hd}, {0,0,1}, su_min, su_max, sv_min, sv_max);
    addFace({c.x+hw, c.y-hh, c.z-hd}, {c.x-hw, c.y-hh, c.z-hd}, {c.x-hw, c.y+hh, c.z-hd}, {c.x+hw, c.y+hh, c.z-hd}, {0,0,-1}, su_min, su_max, sv_min, sv_max);
    addFace({c.x-hw, c.y+hh, c.z-hd}, {c.x+hw, c.y+hh, c.z-hd}, {c.x+hw, c.y+hh, c.z+hd}, {c.x-hw, c.y+hh, c.z+hd}, {0,1,0}, su_min, su_max, tv_min, tv_max);
}

game::client::RawMeshData MeshBuilderUtils::BuildItemMeshData(
    const BlockRegistry& reg, BlockId id)
{
    game::client::RawMeshData data;
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