#pragma once

#include "Engine/service/IRenderService.h"
#include "Engine/service/AssetService.h"
#include "Engine/tool/RAIIThread.h"
#include "Engine/tool/Log.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

namespace Eng::client {

// ★ 引擎层统一的视锥体，修复了 [0,1] 深度范围的提取
struct Frustum {
    glm::vec4 planes[6];

    void UpdatePlanes(const glm::mat4& vp) {
        auto row = [&](int r) { return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]); };
        glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

        planes[0] = r3 + r0; // Left
        planes[1] = r3 - r0; // Right
        planes[2] = r3 + r1; // Bottom
        planes[3] = r3 - r1; // Top
        planes[4] = r2;      // Near (因为 Z ∈ [0,1]，所以是 r2 而不是 r3 + r2)
        planes[5] = r3 - r2; // Far

        for (auto& p : planes) {
            float len = glm::length(glm::vec3(p));
            if (len > 0.0f) p /= len;
        }
    }

    bool TestAABB(const glm::vec3& mn, const glm::vec3& mx) const {
        for (int i = 0; i < 6; ++i) {
            const auto& p = planes[i];
            glm::vec3 n(p);
            float d = p.w;
            glm::vec3 v(n.x >= 0 ? mx.x : mn.x,
                        n.y >= 0 ? mx.y : mn.y,
                        n.z >= 0 ? mx.z : mn.z);
            if (glm::dot(n, v) + d < 0.0f) return false;
        }
        return true;
    }
};

// ★ 纯数据 Buffer 组合
struct RenderMesh {
    service::BufferPtr vbo;
    service::BufferPtr ibo;
    uint32_t indexCount  = 0;
    uint32_t vertexCount = 0;
    bool     indexed     = false;
};

class SceneRenderer {
public:
    SceneRenderer(service::IRenderService* rhi, service::AssetService* assets);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // ==================== 区块数据接口 ====================
    void OnChunkMeshReady(uint64_t chunkKey,
                          std::vector<service::Vertex>&& vertices,
                          std::vector<uint32_t>&& indices,
                          const glm::vec3& aabbMin,
                          const glm::vec3& aabbMax);

    void OnChunkUnloaded(uint64_t chunkKey);

    // ==================== 统一渲染接口 ====================
    void BeginScene();
    
    void SubmitMesh(const RenderMesh& mesh, service::ShaderPtr shader, 
                    const service::TexturePtr& diffuseTex, 
                    const service::rhi::PerObjectData& data = {});
                    
    void SubmitInstancedMesh(const RenderMesh& mesh, service::ShaderPtr shader, 
                             const service::TexturePtr& diffuseTex, 
                             const std::vector<service::rhi::PerObjectData>& instances);

    // ==================== 水面（透明）渲染接口 ====================
    void SubmitWaterChunk(uint64_t chunkKey, std::vector<service::Vertex>&& vertices, std::vector<uint32_t>&& indices, const glm::vec3& aabbMin, const glm::vec3& aabbMax);
    void RemoveWaterChunk(uint64_t chunkKey);

    // 接收统一的 Frustum 进行剔除
    void EndScene(const Frustum& frustum,
                  const service::ShaderPtr& terrainShader, const service::TexturePtr& atlasTexture);

    void Clear();

private:
    struct ChunkRenderData {
        std::vector<service::Vertex> vertices;
        std::vector<uint32_t> indices;
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
    };

    struct ChunkMeshData {
        uint32_t firstIndex  = 0;
        uint32_t indexCount  = 0;
        uint32_t vertexCount = 0;
    };

    struct GlobalGeometry {
        std::vector<service::Vertex> vertices;
        std::vector<uint32_t> indices;
        std::unordered_map<uint64_t, ChunkMeshData> chunkMeshes;
        size_t vertexCursor = 0;
        size_t indexCursor  = 0;
    };

    struct DrawCommand {
        enum class Type : std::uint8_t { Mesh, InstancedMesh } type;
        RenderMesh mesh;
        service::ShaderPtr shader = nullptr;
        service::TexturePtr diffuse;
        service::rhi::PerObjectData data{};
        std::vector<service::rhi::PerObjectData> instances{};
    };

    struct WaterChunk {
        RenderMesh mesh;
        glm::vec3 aabbMin;
        glm::vec3 aabbMax;
    };

    void TriggerAsyncRebuild();
    void ProcessPendingRebuild();
    void AppendIncremental(uint64_t key, const ChunkRenderData& data);

    service::IRenderService* m_rhi = nullptr;
    service::AssetService*   m_assets = nullptr;

    std::unique_ptr<Eng::tool::Log> m_logger = std::make_unique<Eng::tool::Log>();

    // CPU 侧权威副本
    std::unordered_map<uint64_t, ChunkRenderData> m_allChunks;

    // GPU 侧常驻资源
    service::BufferPtr m_terrainVbo;
    service::BufferPtr m_terrainIbo;
    service::BufferPtr m_indirectBuffer;
    size_t m_terrainVboCapacityBytes     = 0;
    size_t m_terrainIboCapacityBytes     = 0;
    size_t m_indirectBufferCapacityBytes = 0;

    std::unordered_map<uint64_t, ChunkMeshData> m_chunkMeshTable;
    size_t m_vertexCursor = 0;
    size_t m_indexCursor  = 0;
    size_t m_garbageVertices = 0;
    size_t m_garbageIndices  = 0;

    // 异步重建
    std::mutex m_pendingMutex;
    std::unique_ptr<GlobalGeometry> m_pendingGeometry;
    std::atomic<bool> m_rebuildInProgress{false};
    std::unique_ptr<Eng::tool::RAIIThread> m_rebuildThread;

    // 每帧指令队列
    std::vector<service::rhi::DrawIndexedIndirectCommand> m_indirectCommands;
    std::vector<DrawCommand> m_drawQueue;

    // 水面资源
    service::ShaderPtr m_waterShader;
    std::unordered_map<uint64_t, WaterChunk> m_waterChunks;
};

} // namespace Eng::client