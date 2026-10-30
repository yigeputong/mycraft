#include "game/client/ClientWorld.h"
#include "game/core/BlockRegistry.h"
#include "game/core/LightEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace game::client {

static bool AabbInFrustum(const Frustum& f, const glm::vec3& mn, const glm::vec3& mx) {
    for (const auto& p : f.planes) {
        glm::vec3 n = glm::vec3(p);
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

void ClientWorld::AddChunk(int cx, int cz, const std::vector<BlockId>& blocksData,
                           const std::array<uint8_t, CHUNK_VOLUME>& skyData,
                           const std::array<LightRGB, CHUNK_VOLUME>& blockLightData,
                           Eng::client::MeshPtr mesh) {
    ClientChunk cc;
    cc.chunk = std::make_shared<Chunk>();
    cc.chunk->chunk_x = cx;
    cc.chunk->chunk_z = cz;
    
    std::memcpy(cc.chunk->blocks.data(), blocksData.data(), CHUNK_VOLUME * sizeof(BlockId));
    
    cc.chunk->sky = skyData;
    cc.chunk->block = blockLightData;
    
    cc.mesh = std::move(mesh);

    // 计算高度图和 AABB
    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            int topY = 0;
            BlockId topB = AIR_ID;
            for (int y = CHUNK_SIZE_Y - 1; y >= 0; --y) {
                BlockId b = ChunkGet(*cc.chunk, lx, y, lz);
                if (b != AIR_ID && b != UNKNOWN_ID) {
                    topY = y;
                    topB = b;
                    break;
                }
            }
            cc.heights[lx * 16 + lz] = (int16_t)topY;
            cc.topBlock[lx * 16 + lz] = topB;
        }
    }
    cc.aabbMin = glm::vec3(cx * CHUNK_SIZE_X, 0.0f, cz * CHUNK_SIZE_Z);
    cc.aabbMax = cc.aabbMin + glm::vec3(CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z);
    
    m_chunks[ChunkKey(cx, cz)] = std::move(cc);
    m_pending.erase(ChunkKey(cx, cz));

    // 只轻量级触发邻居重建
    RebuildNeighbor(cx - 1, cz);
    RebuildNeighbor(cx + 1, cz);
    RebuildNeighbor(cx, cz - 1);
    RebuildNeighbor(cx, cz + 1);
}

void ClientWorld::RebuildNeighbor(int cx, int cz) {
    uint64_t k = ChunkKey(cx, cz);
    if (m_chunks.count(k)) {
        m_dirtyChunks.insert(k); // 只标记自己，不级联
    }
}

void ClientWorld::SetBlock(int wx, int wy, int wz, BlockId id) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return;
    int cx = wx >> 4, cz = wz >> 4;
    int lx = wx & 15, lz = wz & 15;
    auto it = m_chunks.find(ChunkKey(cx, cz));
    if (it != m_chunks.end()) {
        ChunkSet(*it->second.chunk, lx, wy, lz, id);
        MarkDirty(ChunkKey(cx, cz));
        if (lx == 0)  MarkDirty(ChunkKey(cx - 1, cz));
        if (lx == 15) MarkDirty(ChunkKey(cx + 1, cz));
        if (lz == 0)  MarkDirty(ChunkKey(cx, cz - 1));
        if (lz == 15) MarkDirty(ChunkKey(cx, cz + 1));
    }
}

BlockId ClientWorld::GetBlockAt(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return UNKNOWN_ID; // ★ 越界返回 UNKNOWN_ID
    int cx = wx >> 4, cz = wz >> 4;
    int lx = wx & 15, lz = wz & 15;
    auto it = m_chunks.find(ChunkKey(cx, cz));
    if (it == m_chunks.end()) return UNKNOWN_ID; // ★ 未加载区块返回 UNKNOWN_ID，防止物理穿透
    return ChunkGet(*it->second.chunk, lx, wy, lz);
}

bool ClientWorld::IsSolidAt(int wx, int wy, int wz) const {
    BlockId id = GetBlockAt(wx, wy, wz);
    if (!m_registry) return false; // 防御性检查
    return m_registry->IsSolid(id); // ★ 使用实例调用
}

void ClientWorld::MarkDirty(uint64_t key) {
    if (m_chunks.count(key)) m_dirtyChunks.insert(key);
}

std::vector<uint64_t> ClientWorld::FetchDirtyChunks(size_t maxCount) {
    std::vector<uint64_t> result;
    // 不要盲目 reserve 传入的 maxCount，而是根据实际脏区块数量或最大值来分配
    size_t reserveCount = std::min(maxCount, m_dirtyChunks.size());
    result.reserve(reserveCount);

    for (auto it = m_dirtyChunks.begin(); it != m_dirtyChunks.end() && result.size() < maxCount; ) {
        result.push_back(*it);
        // erase 返回下一个有效的迭代器(C++11及以上)
        it = m_dirtyChunks.erase(it);
    }
    return result;
}

void ClientWorld::CollectVisibleMeshes(const Frustum& frustum,
                                       std::vector<Eng::client::MeshPtr>& outMeshes) const {
    outMeshes.clear();
    outMeshes.reserve(m_chunks.size());
    for (const auto& [key, cc] : m_chunks) {
        if (!cc.mesh) continue;
        if (!AabbInFrustum(frustum, cc.aabbMin, cc.aabbMax)) continue;
        outMeshes.push_back(cc.mesh);
    }
}

void ClientWorld::UpdateStreaming(const glm::vec3& cameraPos, int renderDistance,
                                  std::function<void(int, int)>&& sendRequest) {
    int pcx = static_cast<int>(std::floor(cameraPos.x)) >> 4;
    int pcz = static_cast<int>(std::floor(cameraPos.z)) >> 4;

    const int unloadR = renderDistance + 2;

    if (pcx != m_lastPlayerChunkX || pcz != m_lastPlayerChunkZ) {
        m_lastPlayerChunkX = pcx;
        m_lastPlayerChunkZ = pcz;

        for (auto it = m_chunks.begin(); it != m_chunks.end(); ) {
            int cx = static_cast<int>(static_cast<int32_t>(it->first & 0xFFFFFFFFu));
            int cz = static_cast<int>(static_cast<int32_t>(it->first >> 32));
            if (std::abs(cx - pcx) > unloadR || std::abs(cz - pcz) > unloadR) {
                m_pending.erase(it->first);
                m_dirtyChunks.erase(it->first);
                it = m_chunks.erase(it);
            } else {
                ++it;
            }
        }
    }

    struct Need { int cx, cz, d2; };
    std::vector<Need> need;
    for (int dz = -renderDistance; dz <= renderDistance; ++dz) {
        for (int dx = -renderDistance; dx <= renderDistance; ++dx) {
            int cx = pcx + dx;
            int cz = pcz + dz;
            uint64_t key = ChunkKey(cx, cz);
            if (m_chunks.count(key) || m_pending.count(key)) continue;
            need.push_back({cx, cz, dx*dx + dz*dz});
        }
    }

    std::sort(need.begin(), need.end(), [](const Need& a, const Need& b) { return a.d2 < b.d2; });

    int sent = 0;
    for (const auto& n : need) {
        if (sent++ >= 10) break;
        sendRequest(n.cx, n.cz);
        m_pending.insert(ChunkKey(n.cx, n.cz));
    }
}

ClientChunk* ClientWorld::GetChunk(uint64_t key) {
    auto it = m_chunks.find(key);
    return it != m_chunks.end() ? &it->second : nullptr;
}

void ClientWorld::Clear() {
    m_chunks.clear();
    m_pending.clear();
    m_dirtyChunks.clear();
    m_lastPlayerChunkX = INT_MIN;
    m_lastPlayerChunkZ = INT_MIN;
}

} // namespace game::client