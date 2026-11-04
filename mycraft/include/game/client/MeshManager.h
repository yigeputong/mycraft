#pragma once

#include "game/core/World.h"
#include "game/core/BlockRegistry.h"
#include "Engine/tool/RAIIThread.h"
#include "Engine/service/IRenderService.h"

#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <vector>

namespace game::client {

// ============================================================
// 任务结构体：主线程提交给 Worker
// ============================================================
struct MeshTask {
    uint64_t chunkKey = 0;
    uint32_t version = 0; // ★ 新增：用于解决异步乱序覆盖
    
    std::shared_ptr<Chunk> chunk;
    std::shared_ptr<Chunk> nXm;
    std::shared_ptr<Chunk> nXp;
    std::shared_ptr<Chunk> nZm;
    std::shared_ptr<Chunk> nZp;
    
    const BlockRegistry* reg = nullptr;
};

// ============================================================
// 结果结构体：Worker 返回给主线程
// ============================================================
struct MeshResult {
    uint64_t chunkKey = 0;
    uint32_t version = 0; // ★ 带回版本号供主线程校验
    
    std::vector<Eng::service::Vertex> vertices;
    std::vector<uint32_t> indices;
    
    std::vector<Eng::service::Vertex> waterVertices;
    std::vector<uint32_t> waterIndices;
};

// ============================================================
// MeshManager
// ============================================================
class MeshManager {
public:
    MeshManager() = default;
    ~MeshManager() { Stop(); }

    void Start();
    void Stop();

    void SetRegistry(const BlockRegistry* reg) { m_registry = reg; }

    // 提交构建任务
    void Submit(uint64_t key, uint32_t version, 
                std::shared_ptr<Chunk> chunk, 
                std::shared_ptr<Chunk> nXm, std::shared_ptr<Chunk> nXp, 
                std::shared_ptr<Chunk> nZm, std::shared_ptr<Chunk> nZp);

    // 主线程按需拉取结果（严格限制 maxCount，绝不丢弃）
    void DrainResults(size_t maxCount, std::function<void(MeshResult&&)>&& onComplete);

private:
    void WorkerLoop(const std::stop_token& st);
    void BuildChunkMeshData(const MeshTask& task, MeshResult& result);

    const BlockRegistry* m_registry = nullptr;

    // 线程池控制
    std::vector<std::unique_ptr<Eng::tool::RAIIThread>> m_workers;
    std::atomic<bool> m_stop{false};

    // 任务队列
    std::mutex m_taskMutex;
    std::condition_variable m_taskCv;
    std::queue<std::shared_ptr<MeshTask>> m_taskQueue;

    // 结果队列（带背压）
    std::mutex m_resultMutex;
    std::condition_variable m_resultCv;
    std::queue<MeshResult> m_resultQueue;
};

} // namespace game::client