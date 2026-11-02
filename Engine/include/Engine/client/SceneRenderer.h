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

/**
 * @brief 现代体素渲染引擎层。
 * @details 负责管理区块大 Buffer、增量更新、异步显存压实、视锥剔除与 MDI 提交。
 */
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
    
    void SubmitMesh(service::MeshPtr mesh, service::ShaderPtr shader, 
                    const service::Material& material, 
                    const service::rhi::PerObjectData& data = {});
                    
    void SubmitInstancedMesh(service::MeshPtr mesh, service::ShaderPtr shader, 
                             const service::Material& material, 
                             const std::vector<service::rhi::PerObjectData>& instances);
                             
    void SubmitSkybox(service::ShaderPtr shader);

    // ==================== 水面（透明）渲染接口 ====================
    void SubmitWaterChunk(uint64_t chunkKey, std::vector<service::Vertex>&& vertices, std::vector<uint32_t>&& indices, const glm::vec3& aabbMin, const glm::vec3& aabbMax);
    void RemoveWaterChunk(uint64_t chunkKey);

    // 业务层传入地形所需的 Shader 和 Atlas，引擎自己决定怎么画 MDI
    void EndScene(const glm::mat4& viewProj, const glm::vec3& cameraPos,
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

    // 绘制指令队列
    struct DrawCommand {
        enum class Type : std::uint8_t { Mesh, InstancedMesh, Skybox } type;
        service::MeshPtr mesh = nullptr;
        service::ShaderPtr shader = nullptr;
        service::Material material{};
        service::rhi::PerObjectData data{};
        std::vector<service::rhi::PerObjectData> instances{};
    };

    // ★ 新增：水面独立区块缓存
    struct WaterChunk {
        service::MeshPtr mesh;
        glm::vec3 aabbMin;
        glm::vec3 aabbMax;
    };

    void TriggerAsyncRebuild();
    void ProcessPendingRebuild();
    void AppendIncremental(uint64_t key, const ChunkRenderData& data);
    bool TestAABB(const glm::vec3& mn, const glm::vec3& mx, const glm::mat4& viewProj) const;

    service::IRenderService* m_rhi = nullptr;
    service::AssetService*   m_assets = nullptr;

    std::unique_ptr<Eng::tool::Log> m_logger = std::make_unique<Eng::tool::Log>();

    std::unordered_map<uint64_t, ChunkRenderData> m_allChunks;
    GlobalGeometry m_liveGeometry;

    size_t m_vboCapacity = 0;
    size_t m_iboCapacity = 0;
    size_t m_garbageVertices = 0;
    size_t m_garbageIndices  = 0;

    std::mutex m_pendingMutex;
    std::unique_ptr<GlobalGeometry> m_pendingGeometry;
    std::atomic<bool> m_rebuildInProgress{false};
    std::unique_ptr<Eng::tool::RAIIThread> m_rebuildThread;

    std::vector<service::rhi::DrawIndexedIndirectCommand> m_indirectCommands;
    std::vector<DrawCommand> m_drawQueue;

    // ★ 新增：水面相关资源
    service::ShaderPtr m_waterShader;
    std::unordered_map<uint64_t, WaterChunk> m_waterChunks;
};

} // namespace Eng::client