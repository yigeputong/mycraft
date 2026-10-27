#pragma once
#include "game/core/World.h"
#include "client/Render/RenderAPI.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

namespace game::client {

// 用来做视锥剔除的视锥体结构（从 Game.h 移过来）
struct Frustum {
    glm::vec4 planes[6];
};

struct ClientChunk {
    Chunk chunk;
    Eng::client::MeshHandle mesh = 0;
    std::array<int16_t, 256> heights{};
    std::array<uint8_t, 256> topBlock{};
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
};

class ClientWorld {
public:
    // 接收区块数据
    void AddChunk(int cx, int cz, const std::vector<uint8_t>& blocksData, Eng::client::MeshHandle mesh);
    
    // 获取/修改区块内的方块
    void SetBlock(int wx, int wy, int wz, BlockType type);
    BlockType GetBlockAt(int wx, int wy, int wz) const;
    bool IsSolidAt(int wx, int wy, int wz) const;

    // 区块流式加载（传入玩家坐标和请求回调）
    void UpdateStreaming(const glm::vec3& cameraPos, int renderDistance, 
                         std::function<void(int, int)>&& sendRequest,
                         std::function<void(Eng::client::MeshHandle)>&& destroyMesh);

    // 脏标记与获取（用于平滑重建 Mesh）
    void MarkDirty(uint64_t key);
    std::vector<uint64_t> FetchDirtyChunks(size_t maxCount);

    // 获取视锥剔除后的可见 Mesh
    void CollectVisibleMeshes(const Frustum& frustum, std::vector<Eng::client::MeshHandle>& outMeshes) const;

    size_t GetPendingSize() const { return m_pending.size(); }

    // 获取区块数据
    ClientChunk* GetChunk(uint64_t key);
    const std::unordered_map<uint64_t, ClientChunk>& GetChunks() const { return m_chunks; }
    void Clear();

private:
    std::unordered_map<uint64_t, ClientChunk> m_chunks;
    std::unordered_set<uint64_t>              m_pending;
    std::unordered_set<uint64_t>              m_dirtyChunks;
    int m_lastPlayerChunkX = INT_MIN;
    int m_lastPlayerChunkZ = INT_MIN;
};

} // namespace game::client