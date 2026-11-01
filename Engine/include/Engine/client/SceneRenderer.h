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
 *          由业务层（MyGame）创建并拥有；渲染后端（RHI）和资产服务（AssetService）由 Engine 注入。
 */
class SceneRenderer {
public:
    /**
     * @brief 构造场景渲染器
     * @param rhi    由 Engine 拥有的渲染后端
     * @param assets 由 Engine 拥有的资产服务（用于加载标准 mesh shader）
     * @throw std::runtime_error 当标准 shader 加载失败时
     */
    SceneRenderer(service::IRenderService* rhi, service::AssetService* assets);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    /**
     * @brief 注入地形图集纹理（业务层在启动时调用一次，可替换）
     */
    void SetAtlasTexture(service::TexturePtr atlas);

    /**
     * @brief 接收区块的渲染数据（主线程调用）
     */
    void OnChunkMeshReady(uint64_t chunkKey,
                          std::vector<service::Vertex>&& vertices,
                          std::vector<uint32_t>&& indices,
                          const glm::vec3& aabbMin,
                          const glm::vec3& aabbMax);

    /**
     * @brief 卸载区块（丢弃 GPU 视图，延迟释放 GPU 资源）
     */
    void OnChunkUnloaded(uint64_t chunkKey);

    /**
     * @brief 渲染一帧地形（含视锥剔除 + MDI 提交）
     * @param viewProj  业务层维护的 view-projection 矩阵（OpenGL 规范）
     * @param cameraPos 相机世界坐标（预留：距离排序 / 阴影 / 特殊效果）
     */
    void Render(const glm::mat4& viewProj, const glm::vec3& cameraPos);

    /**
     * @brief 彻底清空所有数据（换关卡 / 回主菜单时调用）
     */
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

    // 外部注入（不拥有）
    service::IRenderService* m_rhi = nullptr;
    service::AssetService*   m_assets = nullptr;

    // 内部持有
    service::ShaderPtr  m_terrainShader;   // 构造时由 m_assets 加载
    service::TexturePtr m_atlasTexture;    // 业务层通过 SetAtlasTexture 注入

    std::unique_ptr<Eng::tool::Log> m_logger = std::make_unique<Eng::tool::Log>();

    std::unordered_map<uint64_t, ChunkRenderData> m_allChunks;
    GlobalGeometry m_liveGeometry;

    size_t m_vboCapacity = 0;
    size_t m_iboCapacity = 0;
    size_t m_garbageVertices = 0;
    size_t m_garbageIndices  = 0;

    // 异步重建
    std::mutex m_pendingMutex;
    std::unique_ptr<GlobalGeometry> m_pendingGeometry;
    std::atomic<bool> m_rebuildInProgress{false};
    std::unique_ptr<Eng::tool::RAIIThread> m_rebuildThread;

    // MDI 指令缓冲
    std::vector<service::rhi::DrawIndexedIndirectCommand> m_indirectCommands;
};

} // namespace Eng::client