#pragma once
#include "../core/World.h"
#include <client/Render/RenderAPI.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <atomic>
#include <functional>
#include <unordered_set>

namespace game { class BlockRegistry; }

namespace game::client {

struct MeshTask {
    uint64_t chunkKey;
    std::shared_ptr<Chunk> chunk;
    std::shared_ptr<Chunk> nXm, nXp, nZm, nZp;
    const game::BlockRegistry* reg = nullptr;
};

struct MeshResult {
    uint64_t chunkKey;
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t>            indices;
};

class MeshManager {
public:
    void SetRegistry(const game::BlockRegistry* reg) { m_registry = reg; }
    void Start();
    void Stop();

    void Submit(uint64_t key, std::shared_ptr<Chunk> chunk, 
                         std::shared_ptr<Chunk> nXm, std::shared_ptr<Chunk> nXp, 
                         std::shared_ptr<Chunk> nZm, std::shared_ptr<Chunk> nZp);

    void DrainResults(std::function<void(MeshResult&&)>&& onComplete);

    ~MeshManager() { Stop(); }
private:
    void WorkerLoop();
    static void BuildChunkMeshData(MeshTask& task, MeshResult& result);

    std::vector<std::thread>              m_workers;
    std::mutex                            m_taskMutex;
    std::condition_variable               m_taskCv;
    std::queue<std::shared_ptr<MeshTask>> m_taskQueue;
    std::atomic<bool>                     m_stop{false};

    std::mutex                            m_resultMutex;
    std::queue<MeshResult>                m_resultQueue;

    std::unordered_set<uint64_t>          m_inflightTasks;
    const game::BlockRegistry*            m_registry = nullptr;
};

} // namespace game::client