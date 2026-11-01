#include "Engine/client/SceneRenderer.h"
#include "Engine/tool/Log.h"
#include <ranges>
#include <format>
#include <stdexcept>

namespace Eng::client {

SceneRenderer::SceneRenderer(service::IRenderService* rhi, service::AssetService* assets)
    : m_rhi(rhi), m_assets(assets) {

    if (!m_rhi) {
        throw std::invalid_argument("SceneRenderer: rhi is null");
    }
    if (!m_assets) {
        throw std::invalid_argument("SceneRenderer: assets is null");
    }

    // 内部自行加载标准地形 shader（Opaque + Standard 顶点布局）
    m_terrainShader = m_assets->LoadShader(m_assets->MakeShaderCreateInfo("mesh"));
    if (!m_terrainShader) {
        throw std::runtime_error(
            "SceneRenderer: failed to load 'mesh' shader. "
            "Check assets/shaders/build/mesh/ for vert/frag (.spv) files.");
    }

    m_indirectCommands.reserve(2048);
}

SceneRenderer::~SceneRenderer() {
    if (m_rebuildThread) {
        m_rebuildThread.reset(); // 会自动 request_stop 并 join
    }
}

void SceneRenderer::SetAtlasTexture(service::TexturePtr atlas) {
    m_atlasTexture = std::move(atlas);
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

    if (!m_rebuildInProgress.load()) {
        AppendIncremental(chunkKey, m_allChunks[chunkKey]);
    }
}

void SceneRenderer::OnChunkUnloaded(uint64_t chunkKey) {
    if (auto it = m_liveGeometry.chunkMeshes.find(chunkKey); it != m_liveGeometry.chunkMeshes.end()) {
        m_garbageVertices += it->second.vertexCount;
        m_garbageIndices  += it->second.indexCount;
        it->second.indexCount = 0;
        it->second.vertexCount = 0;
    }
    m_allChunks.erase(chunkKey);
}

void SceneRenderer::AppendIncremental(uint64_t key, const ChunkRenderData& data) {
    size_t maxVertices = m_vboCapacity / sizeof(service::Vertex);
    size_t maxIndices  = m_iboCapacity / sizeof(uint32_t);

    bool outOfCapacity = (m_liveGeometry.vertexCursor + data.vertices.size() > maxVertices) ||
                         (m_liveGeometry.indexCursor + data.indices.size() > maxIndices);

    bool tooMuchGarbage = (m_garbageVertices > m_liveGeometry.vertexCursor * 0.8) &&
                          (m_garbageVertices > 2000000);

    if (outOfCapacity || tooMuchGarbage || m_liveGeometry.vertexCursor == 0) {
        TriggerAsyncRebuild();
        return;
    }

    uint32_t vOffset = static_cast<uint32_t>(m_liveGeometry.vertexCursor);
    uint32_t iOffset = static_cast<uint32_t>(m_liveGeometry.indexCursor);

    std::vector<uint32_t> globalizedIndices;
    globalizedIndices.reserve(data.indices.size());
    for (uint32_t idx : data.indices) {
        globalizedIndices.push_back(idx + vOffset);
    }

    try {
        m_rhi->UpdateGlobalGeometry(vOffset, data.vertices, iOffset, globalizedIndices);
    } catch (const std::out_of_range& e) {
        logWarning(m_logger, "[SceneRenderer] Incremental failed: " << e.what() << ". Falling back to rebuild.");
        TriggerAsyncRebuild();
        return;
    }

    m_liveGeometry.vertices.insert(m_liveGeometry.vertices.end(), data.vertices.begin(), data.vertices.end());
    m_liveGeometry.indices.insert(m_liveGeometry.indices.end(), globalizedIndices.begin(), globalizedIndices.end());

    m_liveGeometry.chunkMeshes[key] = ChunkMeshData{
        .firstIndex  = iOffset,
        .indexCount  = static_cast<uint32_t>(data.indices.size()),
        .vertexCount = static_cast<uint32_t>(data.vertices.size())
    };

    m_liveGeometry.vertexCursor += data.vertices.size();
    m_liveGeometry.indexCursor  += data.indices.size();
}

void SceneRenderer::TriggerAsyncRebuild() {
    if (m_rebuildInProgress.exchange(true)) return;

    auto snapshot = m_allChunks;

    // 先重置旧的线程（如果有的话），触发 join
    m_rebuildThread.reset(); 

    m_rebuildThread = std::make_unique<Eng::tool::RAIIThread>(
        [this, snap = std::move(snapshot)](const std::stop_token& st) {
            auto newGeo = std::make_unique<GlobalGeometry>();

            size_t totalV = 0, totalI = 0;
            for (const auto& chunk : snap | std::views::values) {
                totalV += chunk.vertices.size();
                totalI += chunk.indices.size();
            }

            newGeo->vertices.resize(totalV);
            newGeo->indices.resize(totalI);

            service::Vertex* vOut = newGeo->vertices.data();
            uint32_t* iOut = newGeo->indices.data();
            size_t vCursor = 0, iCursor = 0;

            for (const auto& [key, chunk] : snap) {
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

            std::lock_guard lock(m_pendingMutex);
            m_pendingGeometry = std::move(newGeo);
        });
}

void SceneRenderer::ProcessPendingRebuild() {
    if (!m_rebuildInProgress.load()) return;

    std::lock_guard lock(m_pendingMutex);
    if (!m_pendingGeometry) return;

    m_liveGeometry = std::move(*m_pendingGeometry);
    m_pendingGeometry.reset();

    m_garbageVertices = 0;
    m_garbageIndices = 0;

    m_rhi->UploadGlobalGeometry(
        std::span<const service::Vertex>(m_liveGeometry.vertices),
        std::span<const uint32_t>(m_liveGeometry.indices),
        m_vboCapacity, m_iboCapacity
    );

    m_liveGeometry.vertices.reserve(m_vboCapacity / sizeof(service::Vertex));
    m_liveGeometry.indices.reserve(m_iboCapacity / sizeof(uint32_t));

    m_rebuildInProgress = false;
    logInfo(m_logger, std::format("[SceneRenderer] Async Rebuild Complete. Verts: {}", m_liveGeometry.vertexCursor));
}

void SceneRenderer::Render(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    (void)cameraPos;  // 预留：未来距离排序 / 阴影 / 特殊效果

    if (!m_rhi || !m_terrainShader || !m_atlasTexture) return;

    ProcessPendingRebuild();

    m_indirectCommands.clear();
    for (const auto& [key, cc] : m_liveGeometry.chunkMeshes) {
        if (cc.indexCount == 0) continue;

        // ★ 崩溃修复：m_allChunks 与 m_liveGeometry 可能因异步 rebuild 出现瞬态不一致
        auto chunkIt = m_allChunks.find(key);
        if (chunkIt == m_allChunks.end()) continue;

        if (!TestAABB(chunkIt->second.aabbMin, chunkIt->second.aabbMax, viewProj)) continue;

        m_indirectCommands.push_back({
            .indexCount    = cc.indexCount,
            .instanceCount = 1,
            .firstIndex    = cc.firstIndex,
            .vertexOffset  = 0,
            .firstInstance = 0
        });
    }

    if (!m_indirectCommands.empty()) {
        service::Material mat{ .diffuse = m_atlasTexture };
        m_rhi->UploadIndirectCommands(m_indirectCommands);
        m_rhi->DrawMDI(m_terrainShader, mat, static_cast<uint32_t>(m_indirectCommands.size()));
    }
}

bool SceneRenderer::TestAABB(const glm::vec3& mn, const glm::vec3& mx, const glm::mat4& viewProj) const {
    glm::vec4 planes[6];
    auto row = [&](int r) { return glm::vec4(viewProj[0][r], viewProj[1][r], viewProj[2][r], viewProj[3][r]); };
    auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

    planes[0] = r3 + r0; planes[1] = r3 - r0; planes[2] = r3 + r1;
    planes[3] = r3 - r1; planes[4] = r3 + r2; planes[5] = r3 - r2;

    for (const auto& p : planes) {
        glm::vec3 n(p);
        float d = p.w;
        glm::vec3 v(
            n.x >= 0 ? mx.x : mn.x,
            n.y >= 0 ? mx.y : mn.y,
            n.z >= 0 ? mx.z : mn.z
        );
        if (glm::dot(n, v) + d < 0.0f) return false;
    }
    return true;
}

void SceneRenderer::Clear() {
    if (m_rebuildThread) {
        m_rebuildThread.reset(); // 停止后台线程
    }
    {
        std::lock_guard lock(m_pendingMutex);
        m_pendingGeometry.reset();
    }
    m_allChunks.clear();
    m_liveGeometry = {};
    m_vboCapacity = 0;
    m_iboCapacity = 0;
    m_garbageVertices = 0;
    m_garbageIndices = 0;
    m_rebuildInProgress = false;
    m_indirectCommands.clear();
}

} // namespace Eng::client