#include "Engine/client/SceneRenderer.h"
#include "Engine/tool/Log.h"
#include <algorithm>
#include <ranges>
#include <span>
#include <format>
#include <stdexcept>
#include <cstring>

namespace Eng::client {

SceneRenderer::SceneRenderer(service::IRenderService* rhi, service::AssetService* assets)
    : m_rhi(rhi), m_assets(assets) {
    if (!m_rhi) throw std::invalid_argument("SceneRenderer: rhi is null");
    if (!m_assets) throw std::invalid_argument("SceneRenderer: assets is null");
    
    m_waterShader = m_assets->LoadShader(m_assets->MakeShaderCreateInfo("water", service::rhi::GraphicsPipelineState::PresetTransparent()));
    if (!m_waterShader) {
        logWarning(m_logger, "[SceneRenderer] Failed to load 'water' shader!");
    }
    
    m_indirectCommands.reserve(2048);
    m_drawQueue.reserve(1024);
}

SceneRenderer::~SceneRenderer() {
    if (m_rebuildThread) m_rebuildThread.reset();
}

void SceneRenderer::OnChunkMeshReady(uint64_t chunkKey,
                                    std::vector<service::Vertex>&& vertices,
                                    std::vector<uint32_t>&& indices,
                                    const glm::vec3& aabbMin,
                                    const glm::vec3& aabbMax) {
    if (vertices.empty() || indices.empty()) {
        OnChunkUnloaded(chunkKey);
        return;
    }

    std::lock_guard lock(m_chunkDataMutex);
    if (auto it = m_allChunks.find(chunkKey); it != m_allChunks.end()) {
        m_garbageVertices += it->second.vertices.size();
        m_garbageIndices  += it->second.indices.size();
    }

    m_allChunks[chunkKey] = ChunkRenderData{
        .vertices = std::move(vertices),
        .indices  = std::move(indices),
        .aabbMin  = aabbMin,
        .aabbMax  = aabbMax
    };

    // ★ 如果正在重建，放入延迟队列
    if (m_rebuildInProgress.load()) {
        m_deferredAppends[chunkKey] = m_allChunks[chunkKey];
        return; 
    }
    
    AppendIncremental(chunkKey, m_allChunks[chunkKey]);
}

void SceneRenderer::OnChunkUnloaded(uint64_t chunkKey) {
    std::lock_guard lock(m_chunkDataMutex);
    // 如果网格表里有，就把垃圾统计加上，并剔除
    if (auto it = m_chunkMeshTable.find(chunkKey); it != m_chunkMeshTable.end()) {
        m_garbageVertices += it->second.vertexCount;
        m_garbageIndices  += it->second.indexCount;
        m_chunkMeshTable.erase(it);
    }
    m_allChunks.erase(chunkKey);
    m_deferredAppends.erase(chunkKey);
}

void SceneRenderer::TriggerAsyncRebuild() {
    if (m_rebuildInProgress.exchange(true)) return;

    m_rebuildThread.reset();
    // 移除了 auto snapshot = m_allChunks; 避免瞬间深拷贝导致内存翻倍
    m_rebuildThread = std::make_unique<Eng::tool::RAIIThread>(
        [this](const std::stop_token& st) {
            auto newGeo = std::make_unique<GlobalGeometry>();
            size_t totalV = 0, totalI = 0;

            // 在后台线程加锁读取（不会阻塞主线程太久，因为主线程只是插数据）
            std::lock_guard lock(m_chunkDataMutex);
            for (const auto& chunk : m_allChunks | std::views::values) {
                totalV += chunk.vertices.size();
                totalI += chunk.indices.size();
            }

            newGeo->vertices.resize(totalV);
            newGeo->indices.resize(totalI);

            service::Vertex* vOut = newGeo->vertices.data();
            uint32_t* iOut = newGeo->indices.data();
            size_t vCursor = 0, iCursor = 0;

            for (const auto& [key, chunk] : m_allChunks) {
                if (st.stop_requested()) return;
                if (chunk.vertices.empty()) continue;

                uint32_t vOffset = static_cast<uint32_t>(vCursor);
                newGeo->chunkMeshes[key] = ChunkMeshData{
                    .firstIndex  = static_cast<uint32_t>(iCursor),
                    .indexCount  = static_cast<uint32_t>(chunk.indices.size()),
                    .vertexCount = static_cast<uint32_t>(chunk.vertices.size())
                };

                std::memcpy(vOut + vCursor, chunk.vertices.data(), chunk.vertices.size() * sizeof(service::Vertex));

                for (size_t i = 0; i < chunk.indices.size(); ++i) {
                    iOut[iCursor + i] = chunk.indices[i] + vOffset;
                }

                vCursor += chunk.vertices.size();
                iCursor += chunk.indices.size();
            }

            newGeo->vertexCursor = vCursor;
            newGeo->indexCursor  = iCursor;

            if (st.stop_requested()) return;

            std::lock_guard pendingLock(m_pendingMutex);
            m_pendingGeometry = std::move(newGeo);
        });
}

void SceneRenderer::Clear() {
    if (m_rebuildThread) m_rebuildThread.reset();
    {
        std::lock_guard lock(m_pendingMutex);
        m_pendingGeometry.reset();
    }
    {
        std::lock_guard lock(m_chunkDataMutex);
        m_allChunks.clear();
        m_deferredAppends.clear(); 
    }
    m_chunkMeshTable.clear();

    m_terrainVbo.reset();
    m_terrainIbo.reset();
    m_indirectBuffer.reset();
    m_terrainVboCapacityBytes = 0;
    m_terrainIboCapacityBytes = 0;
    m_indirectBufferCapacityBytes = 0;

    m_vertexCursor = 0;
    m_indexCursor  = 0;
    m_garbageVertices = 0;
    m_garbageIndices = 0;
    m_rebuildInProgress = false;
    m_indirectCommands.clear();
    m_drawQueue.clear();
    m_waterChunks.clear();
}

void SceneRenderer::AppendIncremental(uint64_t key, const ChunkRenderData& data) {
    size_t maxVertices = m_terrainVboCapacityBytes / sizeof(service::Vertex);
    size_t maxIndices  = m_terrainIboCapacityBytes / sizeof(uint32_t);

    bool outOfCapacity = (m_vertexCursor + data.vertices.size() > maxVertices) ||
                         (m_indexCursor  + data.indices.size()  > maxIndices);

    // ★ 核心修改：只要垃圾超过 50 万，或者垃圾占比超过 10%，立刻重建！
    bool tooMuchGarbage = (m_garbageVertices > 500000) || 
                          (m_vertexCursor > 0 && m_garbageVertices > m_vertexCursor * 0.1);

    if (outOfCapacity || tooMuchGarbage || !m_terrainVbo || !m_terrainIbo || m_vertexCursor == 0) {
        TriggerAsyncRebuild();
        return;
    }

    std::vector<uint32_t> globalizedIndices;
    globalizedIndices.reserve(data.indices.size());
    const uint32_t vOffset = static_cast<uint32_t>(m_vertexCursor);
    for (uint32_t idx : data.indices) {
        globalizedIndices.push_back(idx + vOffset);
    }

    m_rhi->UpdateBuffer(
        m_terrainVbo,
        m_vertexCursor * sizeof(service::Vertex),
        std::as_bytes(std::span<const service::Vertex>(data.vertices)));

    m_rhi->UpdateBuffer(
        m_terrainIbo,
        m_indexCursor * sizeof(uint32_t),
        std::as_bytes(std::span<const uint32_t>(globalizedIndices)));

    m_chunkMeshTable[key] = ChunkMeshData{
        .firstIndex  = static_cast<uint32_t>(m_indexCursor),
        .indexCount  = static_cast<uint32_t>(data.indices.size()),
        .vertexCount = static_cast<uint32_t>(data.vertices.size())
    };

    m_vertexCursor += data.vertices.size();
    m_indexCursor  += data.indices.size();
}

void SceneRenderer::ProcessPendingRebuild() {
    if (!m_rebuildInProgress.load()) return;

    std::lock_guard lock(m_pendingMutex);
    if (!m_pendingGeometry) return;

    auto geo = std::move(m_pendingGeometry);
    m_pendingGeometry.reset();

    size_t needVboBytes = std::max<size_t>(geo->vertices.size() * sizeof(service::Vertex) * 2, 128ull * 1024 * 1024);
    size_t needIboBytes = std::max<size_t>(geo->indices.size() * sizeof(uint32_t) * 2, 32ull * 1024 * 1024);

    if (!m_terrainVbo || m_terrainVboCapacityBytes < needVboBytes || m_terrainVboCapacityBytes > needVboBytes * 2) {
        m_terrainVbo.reset();
        m_terrainVbo = m_rhi->CreateBuffer({ .size = needVboBytes, .usage = service::rhi::BufferUsage::Vertex });
        m_terrainVboCapacityBytes = needVboBytes;
    }
    if (!m_terrainIbo || m_terrainIboCapacityBytes < needIboBytes || m_terrainIboCapacityBytes > needIboBytes * 2) {
        m_terrainIbo.reset();
        m_terrainIbo = m_rhi->CreateBuffer({ .size = needIboBytes, .usage = service::rhi::BufferUsage::Index });
        m_terrainIboCapacityBytes = needIboBytes;
    }

    // 1. 上传重建后的老地形数据
    if (!geo->vertices.empty()) {
        m_rhi->UpdateBuffer(m_terrainVbo, 0, std::as_bytes(std::span<const service::Vertex>(geo->vertices)));
    }
    if (!geo->indices.empty()) {
        m_rhi->UpdateBuffer(m_terrainIbo, 0, std::as_bytes(std::span<const uint32_t>(geo->indices)));
    }

    m_chunkMeshTable = std::move(geo->chunkMeshes);
    m_vertexCursor   = geo->vertexCursor;
    m_indexCursor    = geo->indexCursor;

    m_garbageVertices = 0;
    m_garbageIndices  = 0;
    m_rebuildInProgress = false;

    // ★ 打印当前活跃区块数量，观察是否无限增长
    logInfo(m_logger, std::format("[SceneRenderer] Async Rebuild Complete. Verts: {}, Active Chunks: {}", m_vertexCursor, m_allChunks.size()));

    // 2. 强行合并挂起的新网格
    std::vector<uint32_t> globalizedIndices;
    for (auto& [key, data] : m_deferredAppends) {
        // ★ 保护：如果该区块在延迟期间已经被卸载了，直接跳过！
        if (m_allChunks.find(key) == m_allChunks.end()) continue;

        size_t maxVertices = m_terrainVboCapacityBytes / sizeof(service::Vertex);
        size_t maxIndices  = m_terrainIboCapacityBytes / sizeof(uint32_t);

        if (m_vertexCursor + data.vertices.size() <= maxVertices &&
            m_indexCursor + data.indices.size() <= maxIndices) 
        {
            globalizedIndices.clear();
            globalizedIndices.reserve(data.indices.size());
            const uint32_t vOffset = static_cast<uint32_t>(m_vertexCursor);
            for (uint32_t idx : data.indices) {
                globalizedIndices.push_back(idx + vOffset);
            }

            m_rhi->UpdateBuffer(
                m_terrainVbo,
                m_vertexCursor * sizeof(service::Vertex),
                std::as_bytes(std::span<const service::Vertex>(data.vertices)));

            m_rhi->UpdateBuffer(
                m_terrainIbo,
                m_indexCursor * sizeof(uint32_t),
                std::as_bytes(std::span<const uint32_t>(globalizedIndices)));

            m_chunkMeshTable[key] = ChunkMeshData{
                .firstIndex  = static_cast<uint32_t>(m_indexCursor),
                .indexCount  = static_cast<uint32_t>(data.indices.size()),
                .vertexCount = static_cast<uint32_t>(data.vertices.size())
            };

            m_vertexCursor += data.vertices.size();
            m_indexCursor  += data.indices.size();
        }
    }
    m_deferredAppends.clear();
}

// ============================================================
// 统一渲染接口实现
// ============================================================

void SceneRenderer::BeginScene() {
    m_drawQueue.clear();
}

void SceneRenderer::SubmitMesh(const RenderMesh& mesh, service::ShaderPtr shader, 
                               const service::TexturePtr& diffuseTex, 
                               const service::rhi::PerObjectData& data) {
    m_drawQueue.push_back({ 
        .type = DrawCommand::Type::Mesh, 
        .mesh = mesh, 
        .shader = std::move(shader), 
        .diffuse = diffuseTex, 
        .data = data 
    });
}

void SceneRenderer::SubmitInstancedMesh(const RenderMesh& mesh, service::ShaderPtr shader, 
                                        const service::TexturePtr& diffuseTex, 
                                        const std::vector<service::rhi::PerObjectData>& instances) {
    m_drawQueue.push_back({ 
        .type = DrawCommand::Type::InstancedMesh, 
        .mesh = mesh, 
        .shader = std::move(shader), 
        .diffuse = diffuseTex, 
        .instances = instances 
    });
}

void SceneRenderer::SubmitWaterChunk(uint64_t chunkKey, std::vector<service::Vertex>&& vertices, 
                                     std::vector<uint32_t>&& indices, const glm::vec3& aabbMin, const glm::vec3& aabbMax) {
    if (vertices.empty() || indices.empty()) {
        RemoveWaterChunk(chunkKey);
        return;
    }

    RenderMesh mesh;
    mesh.vbo = m_rhi->CreateBuffer({ .size = vertices.size() * sizeof(service::Vertex), .usage = service::rhi::BufferUsage::Vertex });
    m_rhi->UpdateBuffer(mesh.vbo, 0, std::as_bytes(std::span(vertices)));
    
    mesh.ibo = m_rhi->CreateBuffer({ .size = indices.size() * sizeof(uint32_t), .usage = service::rhi::BufferUsage::Index });
    m_rhi->UpdateBuffer(mesh.ibo, 0, std::as_bytes(std::span(indices)));
    
    mesh.indexCount = static_cast<uint32_t>(indices.size());
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexed = true;

    m_waterChunks[chunkKey] = WaterChunk{
        .mesh = std::move(mesh),
        .aabbMin = aabbMin,
        .aabbMax = aabbMax
    };
}

void SceneRenderer::RemoveWaterChunk(uint64_t chunkKey) {
    m_waterChunks.erase(chunkKey);
}

// ★ 提取公共的绘制逻辑，消除代码重复
static void DrawRenderMesh(service::IRenderService* rhi, const RenderMesh& mesh) {
    if (mesh.indexed) rhi->DrawIndexed(mesh.indexCount);
    else              rhi->Draw(mesh.vertexCount);
}

void SceneRenderer::EndScene(const Frustum& frustum, 
                             const service::ShaderPtr& terrainShader, const service::TexturePtr& atlasTexture) {
    if (!m_rhi) return;

    ProcessPendingRebuild();

    // ==========================================================
    // 1. 地形 MDI 绘制（使用统一的 frustum 剔除）
    // ==========================================================
    if (terrainShader && atlasTexture && m_terrainVbo && m_terrainIbo) {
        m_indirectCommands.clear();
        m_indirectCommands.reserve(m_chunkMeshTable.size());

        for (const auto& [key, cc] : m_chunkMeshTable) {
            if (cc.indexCount == 0) continue;

            auto chunkIt = m_allChunks.find(key);
            if (chunkIt == m_allChunks.end()) continue;

            if (!frustum.TestAABB(chunkIt->second.aabbMin, chunkIt->second.aabbMax)) continue;

            m_indirectCommands.push_back({
                .indexCount    = cc.indexCount,
                .instanceCount = 1,
                .firstIndex    = cc.firstIndex,
                .vertexOffset  = 0,
                .firstInstance = 0
            });
        }

        if (!m_indirectCommands.empty()) {
            const size_t needBytes = m_indirectCommands.size() * sizeof(service::rhi::DrawIndexedIndirectCommand);

            if (!m_indirectBuffer || m_indirectBufferCapacityBytes < needBytes) {
                m_indirectBuffer = m_rhi->CreateBuffer({ .size = needBytes, .usage = service::rhi::BufferUsage::Indirect });
                m_indirectBufferCapacityBytes = needBytes;
            }

            m_rhi->UpdateBuffer(
                m_indirectBuffer, 0,
                std::as_bytes(std::span<const service::rhi::DrawIndexedIndirectCommand>(m_indirectCommands)));

            m_rhi->BindShader(terrainShader);
            m_rhi->BindVertexBuffer(m_terrainVbo);
            m_rhi->BindIndexBuffer(m_terrainIbo);
            m_rhi->BindTexture(atlasTexture, 0);
            m_rhi->DrawIndexedIndirect(
                m_indirectBuffer,
                static_cast<uint32_t>(m_indirectCommands.size()),
                sizeof(service::rhi::DrawIndexedIndirectCommand));
        }
    }

    // ==========================================================
    // 2. 绘制队列（LOD、玩家、掉落物、天空盒、手部）
    // ==========================================================
    for (const auto& cmd : m_drawQueue) {
        if (!cmd.shader || !cmd.mesh.vbo) continue;

        m_rhi->BindShader(cmd.shader);
        m_rhi->BindVertexBuffer(cmd.mesh.vbo);
        if (cmd.mesh.indexed) m_rhi->BindIndexBuffer(cmd.mesh.ibo);
        m_rhi->BindTexture(cmd.diffuse, 0);

        if (cmd.type == DrawCommand::Type::Mesh) {
            m_rhi->PushConstants(cmd.shader, std::as_bytes(std::span(&cmd.data, 1)));
            DrawRenderMesh(m_rhi, cmd.mesh); // ★ 调用提取的公共绘制
        } 
        else if (cmd.type == DrawCommand::Type::InstancedMesh) {
            for (const auto& data : cmd.instances) {
                m_rhi->PushConstants(cmd.shader, std::as_bytes(std::span(&data, 1)));
                DrawRenderMesh(m_rhi, cmd.mesh); // ★ 调用提取的公共绘制
            }
        }
    }

    // ==========================================================
    // 3. 绘制水面（透明队列，使用统一的 frustum 剔除）
    // ==========================================================
    if (m_waterShader && atlasTexture) {
        m_rhi->BindShader(m_waterShader);
        m_rhi->BindTexture(atlasTexture, 0);
        for (const auto& [key, wc] : m_waterChunks) {
            if (!frustum.TestAABB(wc.aabbMin, wc.aabbMax)) continue;
            
            m_rhi->BindVertexBuffer(wc.mesh.vbo);
            if (wc.mesh.indexed) m_rhi->BindIndexBuffer(wc.mesh.ibo);
            DrawRenderMesh(m_rhi, wc.mesh); // ★ 调用提取的公共绘制
        }
    }
}

} // namespace Eng::client