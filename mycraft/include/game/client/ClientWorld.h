#pragma once
#include "game/core/World.h"
#include "client/Render/RenderAPI.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <memory>

namespace game { class BlockRegistry; } // ★ 前向声明

namespace game::client {

// 用来做视锥剔除的视锥体结构
struct Frustum {
    glm::vec4 planes[6];
};

struct ClientChunk {
    std::shared_ptr<Chunk>      chunk;
    Eng::client::MeshPtr        mesh;
    std::array<int16_t, 256>    heights{};
    std::array<BlockId, 256>    topBlock{};    // ★ 改为 BlockId，原 uint8_t 会截断
    glm::vec3                   aabbMin{0.0f};
    glm::vec3                   aabbMax{0.0f};
};

class ClientWorld {
public:
    // ★ 注入注册表
    void SetRegistry(const game::BlockRegistry* reg) { m_registry = reg; }

    void AddChunk(int cx, int cz, const std::vector<BlockId>& blocksData,
                  const std::array<uint8_t, CHUNK_VOLUME>& skyData,
                  const std::array<LightRGB, CHUNK_VOLUME>& blockLightData,
                  Eng::client::MeshPtr mesh = nullptr);
    void RebuildNeighbor(int cx, int cz);

    // ★ BlockType 全部换成 BlockId
    void SetBlock(int wx, int wy, int wz, BlockId id);
    BlockId GetBlockAt(int wx, int wy, int wz) const;
    bool IsSolidAt(int wx, int wy, int wz) const;

    void UpdateStreaming(const glm::vec3& cameraPos, int renderDistance,
                         std::function<void(int, int)>&& sendRequest);

    void MarkDirty(uint64_t key);
    std::vector<uint64_t> FetchDirtyChunks(size_t maxCount);

    void CollectVisibleMeshes(const Frustum& frustum,
                              std::vector<Eng::client::MeshPtr>& outMeshes) const;

    size_t GetPendingSize() const { return m_pending.size(); }

    ClientChunk* GetChunk(uint64_t key);
    const std::unordered_map<uint64_t, ClientChunk>& GetChunks() const { return m_chunks; }
    size_t GetMeshCount() const {
        return m_chunks.size();
    }
    void Clear();

private:
    const game::BlockRegistry* m_registry = nullptr; // ★

    std::unordered_map<uint64_t, ClientChunk> m_chunks;
    std::unordered_set<uint64_t>              m_pending;
    std::unordered_set<uint64_t>              m_dirtyChunks;
    int m_lastPlayerChunkX = INT_MIN;
    int m_lastPlayerChunkZ = INT_MIN;
};

} // namespace game::client