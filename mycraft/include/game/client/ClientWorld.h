#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "game/core/World.h"

namespace game {
class BlockRegistry;
}  // namespace game

namespace game::client {

struct ClientChunk {
    std::shared_ptr<Chunk> chunk;
    std::array<int16_t, 256> heights{};
    std::array<BlockId, 256> topBlock{};  // ★ 改为 BlockId，原 uint8_t 会截断
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
};

class ClientWorld {
public:
    // ================================================================
    // 区块在客户端只有三种状态，每种状态一个变量、一个名字：
    //   未请求        —— 不在 m_requestQueue，不在 m_outstanding，且不可读
    //   已请求未应答  —— 在 m_outstanding
    //   可读          —— isChunkAvailable() 为真
    //
    // 本类不再持有权威区块数据（数据在 ECS 的 ClientEntity::chunkData 里），
    // 所以"可读"必须由调用方注入，不能靠本类自己记账。
    // ================================================================
    using ChunkAvailableQuery = std::function<bool(int cx, int cz)>;

    // ★ 注入注册表
    void SetRegistry(const game::BlockRegistry* reg) { m_registry = reg; }

    void AddChunk(int cx, int cz, std::span<const BlockId> blocksData, const std::array<uint8_t, CHUNK_VOLUME>& skyData,
                  const std::array<LightRGB, CHUNK_VOLUME>& blockLightData);
    void RebuildNeighbor(int cx, int cz);

    // ★ BlockType 全部换成 BlockId
    void SetBlock(int wx, int wy, int wz, BlockId id);
    BlockId GetBlockAt(int wx, int wy, int wz) const;
    bool IsSolidAt(int wx, int wy, int wz) const;

    void UpdateStreaming(const glm::vec3& cameraPos, int renderDistance, const ChunkAvailableQuery& isChunkAvailable,
                         std::function<void(int, int)>&& sendRequest,
                         std::function<void(int, int, const ClientChunk&)>&& onUnload);

    // ★ 事件钩子：两者都只做「撤销该坐标的未应答记录」，但事件不同，所以分开命名
    void OnChunkDataArrived(int cx, int cz);  // ChunkData 到达，请求已兑现
    void OnChunkDestroyed(int cx, int cz);    // 区块实体被卸载销毁，请求作废

    void MarkDirty(uint64_t key);
    std::vector<uint64_t> FetchDirtyChunks(size_t maxCount);

    size_t GetOutstandingCount() const { return m_outstanding.size(); }

    const std::unordered_map<uint64_t, ClientChunk>& GetChunks() const { return m_chunks; }
    void Clear();

private:
    const game::BlockRegistry* m_registry = nullptr;  // ★

    std::unordered_map<uint64_t, ClientChunk> m_chunks;  // 本地副本（当前无人写入，见 M3-B）
    std::unordered_set<uint64_t> m_outstanding;          // 已发出请求、尚未收到数据
    std::unordered_set<uint64_t> m_dirtyChunks;
    int m_lastPlayerChunkX = INT_MIN;
    int m_lastPlayerChunkZ = INT_MIN;
    std::vector<std::pair<int, int>> m_requestQueue;
    size_t m_requestCursor = 0;
};

}  // namespace game::client