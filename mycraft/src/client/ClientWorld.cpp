#include "game/client/ClientWorld.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/core/BlockRegistry.h"
#include "game/core/World.h"

namespace game::client {

void ClientWorld::AddChunk(int cx, int cz, std::span<const BlockId> blocksData,
                           const std::array<uint8_t, CHUNK_VOLUME>& skyData,
                           const std::array<LightRGB, CHUNK_VOLUME>& blockLightData) {
    ClientChunk cc;
    cc.chunk = std::make_shared<Chunk>();
    cc.chunk->chunk_x = cx;
    cc.chunk->chunk_z = cz;

    std::memcpy(cc.chunk->blocks.data(), blocksData.data(), CHUNK_VOLUME * sizeof(BlockId));
    cc.chunk->sky = skyData;
    cc.chunk->block = blockLightData;

    // 用纯指针直接读取，千万不要用 ChunkGet，消除所有越界检查开销
    const BlockId* pBlocks = cc.chunk->blocks.data();

    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            int topY = 0;
            BlockId topB = AIR_ID;
            // 从 255 往下扫，扫到非空气立刻 break，通常地表在 60-100 层，只需几十次循环
            for (int y = CHUNK_SIZE_Y - 1; y >= 0; --y) {
                BlockId b = pBlocks[y * 256 + lz * 16 + lx];
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
    m_outstanding.erase(ChunkKey(cx, cz));

    // 触发邻居重建
    RebuildNeighbor(cx - 1, cz);
    RebuildNeighbor(cx + 1, cz);
    RebuildNeighbor(cx, cz - 1);
    RebuildNeighbor(cx, cz + 1);
}

void ClientWorld::RebuildNeighbor(int cx, int cz) {
    uint64_t k = ChunkKey(cx, cz);
    if (m_chunks.count(k)) {
        m_dirtyChunks.insert(k);  // 只标记自己，不级联
    }
}

void ClientWorld::SetBlock(int wx, int wy, int wz, BlockId id) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y)
        return;
    int cx = wx >> 4, cz = wz >> 4;
    int lx = wx & 15, lz = wz & 15;
    auto it = m_chunks.find(ChunkKey(cx, cz));
    if (it != m_chunks.end()) {
        ChunkSet(*it->second.chunk, lx, wy, lz, id);
        MarkDirty(ChunkKey(cx, cz));
        if (lx == 0)
            MarkDirty(ChunkKey(cx - 1, cz));
        if (lx == 15)
            MarkDirty(ChunkKey(cx + 1, cz));
        if (lz == 0)
            MarkDirty(ChunkKey(cx, cz - 1));
        if (lz == 15)
            MarkDirty(ChunkKey(cx, cz + 1));
    }
}

BlockId ClientWorld::GetBlockAt(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_SIZE_Y)
        return UNKNOWN_ID;  // ★ 越界返回 UNKNOWN_ID
    int cx = wx >> 4, cz = wz >> 4;
    int lx = wx & 15, lz = wz & 15;
    auto it = m_chunks.find(ChunkKey(cx, cz));
    if (it == m_chunks.end())
        return UNKNOWN_ID;  // ★ 未加载区块返回 UNKNOWN_ID，防止物理穿透
    return ChunkGet(*it->second.chunk, lx, wy, lz);
}

bool ClientWorld::IsSolidAt(int wx, int wy, int wz) const {
    BlockId id = GetBlockAt(wx, wy, wz);
    if (!m_registry)
        return false;                // 防御性检查
    return m_registry->IsSolid(id);  // ★ 使用实例调用
}

void ClientWorld::MarkDirty(uint64_t key) {
    if (m_chunks.count(key))
        m_dirtyChunks.insert(key);
}

std::vector<uint64_t> ClientWorld::FetchDirtyChunks(size_t maxCount) {
    std::vector<uint64_t> result;
    // 不要盲目 reserve 传入的 maxCount，而是根据实际脏区块数量或最大值来分配
    size_t reserveCount = std::min(maxCount, m_dirtyChunks.size());
    result.reserve(reserveCount);

    for (auto it = m_dirtyChunks.begin(); it != m_dirtyChunks.end() && result.size() < maxCount;) {
        result.push_back(*it);
        // erase 返回下一个有效的迭代器(C++11及以上)
        it = m_dirtyChunks.erase(it);
    }
    return result;
}

void ClientWorld::UpdateStreaming(const glm::vec3& cameraPos, int renderDistance,
                                  const ChunkAvailableQuery& isChunkAvailable,
                                  std::function<void(int, int)>&& sendRequest,
                                  std::function<void(int, int, const ClientChunk&)>&& onUnload) {
    const int playerChunkX = static_cast<int>(std::floor(cameraPos.x)) >> 4;
    const int playerChunkZ = static_cast<int>(std::floor(cameraPos.z)) >> 4;

    const int unloadRadius = renderDistance + 2;

    // 只有跨越区块边界时，才重新计算需要请求的区块并排序
    if (playerChunkX != m_lastPlayerChunkX || playerChunkZ != m_lastPlayerChunkZ) {
        m_lastPlayerChunkX = playerChunkX;
        m_lastPlayerChunkZ = playerChunkZ;

        // 1. 丢弃超远区块（m_chunks 当前恒空，此处为 LOD 回填路径保留）
        for (auto it = m_chunks.begin(); it != m_chunks.end();) {
            const int chunkX = static_cast<int>(static_cast<int32_t>(it->first & 0xFFFFFFFFu));
            const int chunkZ = static_cast<int>(static_cast<int32_t>(it->first >> 32));
            if (std::abs(chunkX - playerChunkX) > unloadRadius || std::abs(chunkZ - playerChunkZ) > unloadRadius) {
                // 在丢弃前，把真实数据传给 LOD 缓存
                if (onUnload)
                    onUnload(chunkX, chunkZ, it->second);

                m_outstanding.erase(it->first);
                m_dirtyChunks.erase(it->first);
                it = m_chunks.erase(it);
            } else {
                ++it;
            }
        }

        // 2. 生成请求队列：判据是"ECS 里此刻到底有没有可读数据"
        struct RequestTarget {
            int chunkX, chunkZ, distanceSq;
        };
        std::vector<RequestTarget> targets;
        for (int dz = -renderDistance; dz <= renderDistance; ++dz) {
            for (int dx = -renderDistance; dx <= renderDistance; ++dx) {
                const int chunkX = playerChunkX + dx;
                const int chunkZ = playerChunkZ + dz;
                if (isChunkAvailable && isChunkAvailable(chunkX, chunkZ))
                    continue;  // 可读：跳过
                if (m_outstanding.count(ChunkKey(chunkX, chunkZ)))
                    continue;  // 已请求：跳过
                targets.push_back({chunkX, chunkZ, dx * dx + dz * dz});
            }
        }
        std::sort(targets.begin(), targets.end(),
                  [](const RequestTarget& a, const RequestTarget& b) { return a.distanceSq < b.distanceSq; });

        m_requestQueue.clear();
        m_requestQueue.reserve(targets.size());
        for (const auto& target : targets) {
            m_requestQueue.emplace_back(target.chunkX, target.chunkZ);
        }
        m_requestCursor = 0;
    }

    // 3. 无论是否跨区块，每帧最多发 10 个请求（防止瞬间发包卡死网络）
    int sentThisFrame = 0;
    while (m_requestCursor < m_requestQueue.size() && sentThisFrame < 10) {
        const int chunkX = m_requestQueue[m_requestCursor].first;
        const int chunkZ = m_requestQueue[m_requestCursor].second;
        ++m_requestCursor;

        const uint64_t chunkKey = ChunkKey(chunkX, chunkZ);

        // 排队期间已经被别的途径填上了，或者已经发出过请求，就跳过
        if (isChunkAvailable && isChunkAvailable(chunkX, chunkZ))
            continue;
        if (m_outstanding.count(chunkKey))
            continue;

        sendRequest(chunkX, chunkZ);
        m_outstanding.insert(chunkKey);
        ++sentThisFrame;
    }
}

void ClientWorld::OnChunkDataArrived(int cx, int cz) {
    m_outstanding.erase(ChunkKey(cx, cz));
}

void ClientWorld::OnChunkDestroyed(int cx, int cz) {
    m_outstanding.erase(ChunkKey(cx, cz));
    m_dirtyChunks.erase(ChunkKey(cx, cz));
}

void ClientWorld::Clear() {
    m_chunks.clear();
    m_outstanding.clear();
    m_dirtyChunks.clear();
    m_lastPlayerChunkX = INT_MIN;
    m_lastPlayerChunkZ = INT_MIN;
}

}  // namespace game::client