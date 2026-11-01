#pragma once

#include "Engine/service/IRenderService.h"
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
    explicit SceneRenderer(service::IRenderService* rhi);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    /**
     * @brief 接收区块的渲染数据（主线程调用）
     */
    void OnChunkMeshReady(uint64_t chunkKey,
                          std::vector<service::Vertex>&& vertices,
                          std::vector<uint32_t>&& indices,
                          const glm::vec3& aabbMin,
                          const glm::vec3& aabbMax);

    /** @brief 卸载区块 */
    void OnChunkUnloaded(uint64_t chunkKey);

    /**
     * @brief 渲染一帧地形
     */
    void Render(const glm::mat4& viewProj,
                const service::ShaderPtr& terrainShader,
                const service::TexturePtr& atlasTexture);

    /** @brief 彻底清空所有数据 */
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

    void TriggerAsyncRebuild();
    void ProcessPendingRebuild();
    void AppendIncremental(uint64_t key, const ChunkRenderData& data);
    bool TestAABB(const glm::vec3& mn, const glm::vec3& mx, const glm::mat4& viewProj) const;

    service::IRenderService* m_rhi = nullptr;

    std::unique_ptr<Eng::tool::Log> m_logger = std::make_unique<Eng::tool::Log>();

    std::unordered_map<uint64_t, ChunkRenderData> m_allChunks;
    GlobalGeometry m_liveGeometry;

    size_t m_vboCapacity = 0;
    size_t m_iboCapacity = 0;
    size_t m_garbageVertices = 0;
    size_t m_garbageIndices  = 0;

    // 异步重建相关
    std::mutex m_pendingMutex;
    std::unique_ptr<GlobalGeometry> m_pendingGeometry;
    std::atomic<bool> m_rebuildInProgress{false};
    Eng::tool::RAIIThread m_rebuildThread{[](const std::stop_token&){}};

    // MDI 指令缓冲
    std::vector<service::rhi::DrawIndexedIndirectCommand> m_indirectCommands;
};

} // namespace Eng::client