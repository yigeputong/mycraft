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

namespace game::client {

struct MeshTask {
    uint64_t chunkKey;
    Chunk chunk;
    Chunk nXm, nXp, nZm, nZp;
    bool hasN[4] = {false, false, false, false};
};

struct MeshResult {
    uint64_t chunkKey;
    std::vector<Eng::client::Vertex> vertices;
    std::vector<uint32_t>            indices;
    std::array<uint8_t, CHUNK_VOLUME> sky;
    std::array<game::LightRGB, CHUNK_VOLUME> block;
};

class MeshManager {
public:
    void Start();
    void Stop();

    void Submit(uint64_t key, const Chunk& chunk, 
                const Chunk* nXm, const Chunk* nXp, const Chunk* nZm, const Chunk* nZp);

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
};

} // namespace game::client