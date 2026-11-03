#include "Engine/tool/Log.h"
#include "Engine/tool/MessageReader.h"
#include "Engine/tool/MessageWriter.h"
#include "Engine/tool/Transport.h"
#include "Engine/tool/NetworkTransport.h"
#include "game/GameServer.h"
#include "game/Protocol.h"
#include "game/server/WorldGenerator.h"
#include "game/core/RayCast.h"
#include "game/core/Physics.h"
#include "game/core/LightEngine.h"

#include <SDL3/SDL.h>
#include <chrono>
#include <cstddef>
#include <random>
#include <algorithm>

namespace game::server {

// ============================================================
// 流水线进度查询（供客户端 UI “小太阳” 使用）
// ============================================================
GenPhase GameServer::GetChunkProgress(int cx, int cz) const {
    uint64_t key = ChunkKey(cx, cz);
    std::shared_lock lock(m_genMutex); // 共享锁，UI 读取不阻塞
    auto it = m_generatingChunks.find(key);
    if (it != m_generatingChunks.end()) {
        return it->second->phase.load(std::memory_order_relaxed);
    }
    if (m_chunks.contains(key)) {
        return GenPhase::Ready;
    }
    return GenPhase::Empty;
}

// ============================================================
// 线程池与流水线调度
// ============================================================
void GameServer::StartWorkers(uint32_t seed) {
    unsigned hw = std::thread::hardware_concurrency();
    unsigned n  = std::clamp(hw > 2 ? hw - 2 : 2u, 2u, 12u);
    m_workersStop = false;
    
    for (unsigned i = 0; i < n; ++i) {
        // 使用 RAIIThread，自动处理 stop_token 和 join
        m_workers.push_back(std::make_unique<Eng::tool::RAIIThread>(
            [this, seed](const std::stop_token& st) {
                WorkerLoop(seed, st);
            }
        ));
    }
}

void GameServer::StopWorkers() {
    {
        std::lock_guard lk(m_taskMutex);
        m_workersStop = true;
    }
    m_taskCv.notify_all();
    
    // RAIIThread 析构会自动 request_stop 并 join
    m_workers.clear(); 
}

void GameServer::WorkerLoop(uint32_t seed, const std::stop_token& st) {
    TerrainGenerator terrain(seed, m_registry);
    
    while (true) {
        Eng::tool::UniqueFunction task;
        {
            std::unique_lock lk(m_taskMutex);
            m_taskCv.wait(lk, [this, &st] {
                return m_workersStop.load() || st.stop_requested() || !m_taskQueue.empty();
            });
            if ((m_workersStop.load() || st.stop_requested()) && m_taskQueue.empty()) {
                return;
            }
            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }
        task(); // 执行闭包
    }
}

void GameServer::DrainChunkResults() {
    constexpr int kMaxSendsPerTick = 64;
    int sends = 0;

    for (;;) {
        ChunkResult r;
        {
            std::lock_guard lk(m_resultMutex);
            if (m_resultQueue.empty() || sends >= kMaxSendsPerTick) break;
            r = std::move(m_resultQueue.front());
            m_resultQueue.pop();
        }

        // 应用存档修改
        auto cit = m_pendingChanges.find(r.key);
        if (cit != m_pendingChanges.end()) {
            for (const auto& c : cit->second) {
                int lx = c.x & 15;
                int lz = c.z & 15;
                ChunkSet(*r.chunk, lx, c.y, lz, c.block);
            }
        }

        if (r.chunk) {
            // ★ 规避 performance-move-const-arg：
            // Chunk 是 trivially copyable，std::move(*r.chunk) 会触发警告。
            // 直接传 *r.chunk 底层会自动拷贝。
            m_chunks.emplace(r.key, *r.chunk);
        }

        // 从生成中列表移除
        {
            std::unique_lock lock(m_genMutex);
            m_generatingChunks.erase(r.key);
        }

        // 发给等待的 client
        auto it = m_waitingClients.find(r.key);
        if (it != m_waitingClients.end()) {
            const Chunk& c = m_chunks.at(r.key);
            for (int cid : it->second) {
                SendChunkTo(cid, c);
            }
            m_waitingClients.erase(it);
        }
        ++sends;
    }
}

void GameServer::SendChunkTo(int clientId, const Chunk& chunk) {
    Eng::tool::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkData));
    w.Write<int32_t>(chunk.chunk_x);
    w.Write<int32_t>(chunk.chunk_z);
    w.Write<uint16_t>(CHUNK_SIZE_Y);
    
    w.WriteBytes(reinterpret_cast<const uint8_t*>(chunk.blocks.data()),
                 chunk.blocks.size() * sizeof(BlockId));
                 
    w.WriteBytes(chunk.sky.data(), chunk.sky.size());
    
    w.WriteBytes(reinterpret_cast<const uint8_t*>(chunk.block.data()), 
                 chunk.block.size() * sizeof(LightRGB));

    m_server->SendTo(clientId, w.GetBuffer());
}

// ============================================================
// 服务器主循环
// ============================================================
void GameServer::Run() {
    constexpr float TICK_INTERVAL = 1.0f / TPS;
    auto last = std::chrono::steady_clock::now();
    float accumulator = 0.0f;

    m_terrain = std::make_unique<TerrainGenerator>(m_worldSeed, m_registry); 
    StartWorkers(m_worldSeed);

    while (m_running.load()) {
        m_server->Update();

        auto now = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(now - last).count();
        last = now;
        accumulator += frameTime;

        while (accumulator >= TICK_INTERVAL) {
            auto tickStart = std::chrono::steady_clock::now();

            // -------- 处理区块请求（带背压控制） --------
            while (!m_chunkQueue.empty()) {
                // ★ 背压核心：如果当前生成中的区块已经达到上限，暂停本次派发
                {
                    std::shared_lock lock(m_genMutex);
                    if (m_generatingChunks.size() >= MAX_CONCURRENT_GENERATING) {
                        break; // 直接退出 while，把剩余请求留到下一个 tick
                    }
                }

                auto req = m_chunkQueue.front();
                m_chunkQueue.pop();
                uint64_t key = ChunkKey(req.cx, req.cz);

                // 如果已经完成，直接发（或者你可以在接收请求时去重）
                if (m_chunks.contains(key)) {
                    SendChunkTo(req.clientId, m_chunks.at(key));
                    continue;
                }

                // 检查是否已经在流水线中
                std::unique_lock lock(m_genMutex);
                if (m_generatingChunks.contains(key)) {
                    m_waitingClients[key].push_back(req.clientId);
                    continue;
                }
                
                m_waitingClients[key].push_back(req.clientId);

                auto inFlight = std::make_shared<InFlightChunk>();
                inFlight->key = key;
                inFlight->ctx = std::make_unique<ChunkGenContext>();
                inFlight->ctx->cx = req.cx;
                inFlight->ctx->cz = req.cz;
                m_generatingChunks[key] = inFlight;

                std::lock_guard lk(m_taskMutex);
                m_taskQueue.emplace([this, inFlight]() {
                    // 1. 密度与生物群系
                    m_terrain->GeneratePhase_BiomeAndDensity(*inFlight->ctx);
                    inFlight->phase.store(GenPhase::TerrainBlocks, std::memory_order_relaxed);
                    
                    // 2. 填充方块
                    m_terrain->GeneratePhase_TerrainBlocks(*inFlight->ctx);
                    inFlight->phase.store(GenPhase::Features, std::memory_order_relaxed);
                    
                    // 3. 装饰物（树木等）
                    m_terrain->GeneratePhase_Features(*inFlight->ctx);
                    inFlight->phase.store(GenPhase::Lighting, std::memory_order_relaxed);
                    
                    // 4. 光照计算
                    LightEngine::InitRegion(m_registry, *inFlight->ctx->chunk, nullptr, nullptr, nullptr, nullptr);
                    inFlight->phase.store(GenPhase::Ready, std::memory_order_relaxed);
                    
                    // 5. 推入结果队列
                    std::lock_guard lk4(m_resultMutex);
                    m_resultQueue.push({ inFlight->key, std::move(inFlight->ctx->chunk) });
                });
                m_taskCv.notify_one();
            }

            DrainChunkResults();

            TickWorld(TICK_INTERVAL);
            TickEntities(TICK_INTERVAL);

            m_saveAccum += TICK_INTERVAL;
            if (m_dirty && m_saveAccum >= kSaveIntervalSec) {
                std::vector<BlockChange> flat;
                for (auto& [k, v] : m_pendingChanges) {
                    flat.insert(flat.end(), v.begin(), v.end());
                }
                m_save->SaveChanges(flat);
                m_dirty = false;
                m_saveAccum = 0.0f;
            }

            BroadcastWorldState();

            auto tickEnd = std::chrono::steady_clock::now();
            m_lastTickMs = std::chrono::duration<float, std::milli>(tickEnd - tickStart).count();

            accumulator -= TICK_INTERVAL;
        }

        SDL_Delay(1);
    }

    StopWorkers();
}

// ============================================================
// 生命周期与网络回调
// ============================================================
bool GameServer::Start(uint16_t port, const std::string& saveDir, bool useMemory) {
    if (m_running.load()) return false;

    m_registry.LoadAll("assets/block");
    m_save = std::make_unique<WorldSave>(saveDir);

    uint32_t seed = 0;
    glm::vec3 spawn(0.0f, 256.0f, 0.0f);
    if (m_save->LoadLevel(seed, spawn)) {
        m_worldSeed = seed;
        m_spawnPos  = spawn;
        logInfo(m_logger, "[Server] Loaded world, seed=" << seed);
    } else {
        seed = static_cast<uint32_t>(std::time(nullptr)) ^ static_cast<uint32_t>(std::clock());
        m_worldSeed = seed;
        m_spawnPos  = spawn;
        m_save->SaveLevel(seed, spawn);
        logInfo(m_logger, "[Server] New world, seed=" << seed);
    }

    auto changes = m_save->LoadChanges();
    for (auto& c : changes) {
        uint64_t k = ChunkKey(c.x >> 4, c.z >> 4);
        m_pendingChanges[k].push_back(c);
    }
    logInfo(m_logger, "[Server] Loaded " << changes.size() << " block changes");

    {
        unsigned hw = std::thread::hardware_concurrency();
        unsigned n  = std::clamp(hw > 2 ? hw - 2 : 2u, 2u, 12u);
        logInfo(m_logger, "[Server] Server Run");
        logInfo(m_logger, "[Server] World Seed = " << m_worldSeed);
        logInfo(m_logger, "[Server] Starting " << n << " chunk workers");
    }

    if (useMemory) {
        m_server = std::make_unique<Eng::tool::MemoryServerTransport>();
    } else {
        m_server = std::make_unique<Eng::tool::NetworkServerTransport>();
    }
    if (!m_server->Listen(port)) {
        logError(m_logger, "[Server] Listen failed");
        m_running.store(false);
        return false;
    }

    m_server->SetConnectCallback([this](int id) {
        std::lock_guard<std::mutex> lock(m_playersMutex);
        ServerPlayer p;
        p.id = (uint32_t)id;
        m_players.push_back(p);

        {
            Eng::tool::MessageWriter w;
            w.Write(static_cast<uint8_t>(net::MessageType::PlayerId));
            w.WriteU32BE((uint32_t)id);
            m_server->SendTo(id, w.GetBuffer());
        }
        {
            Eng::tool::MessageWriter w;
            w.Write(static_cast<uint8_t>(net::MessageType::ServerInfo));
            w.WriteU32BE(m_worldSeed);
            w.WriteVec3(m_spawnPos);
            m_server->SendTo(id, w.GetBuffer());
        }

        for (auto& [eid, e] : m_entities) {
            Eng::tool::MessageWriter w;
            w.Write(static_cast<uint8_t>(net::MessageType::EntitySpawn));
            w.Write<uint32_t>(eid);
            w.WriteVec3(e.position);
            w.Write<uint16_t>(e.itemType);
            m_server->SendTo(id, w.GetBuffer());
        }

        logInfo(m_logger, "[Server] Player " << id << " joined");
    });

    m_server->SetMessageCallback([this](int id, const std::vector<uint8_t>& data) {
        HandleMessage(id, data);
    });

    m_server->SetDisconnectCallback([this](int id) {
        std::lock_guard<std::mutex> lock(m_playersMutex);
        for (auto& p : m_players) {
            if (p.id == (uint32_t)id) {
                if (m_save && !p.name.empty()) {
                    m_save->SavePlayer(p.name, p.position, p.yaw, p.pitch);
                }
                break;
            }
        }
        m_players.erase(std::remove_if(m_players.begin(), m_players.end(),
            [id](const ServerPlayer& p) { return p.id == (uint32_t)id; }), m_players.end());

        std::queue<PendingChunkRequest> keep;
        while (!m_chunkQueue.empty()) {
            if (m_chunkQueue.front().clientId != id) keep.push(m_chunkQueue.front());
            m_chunkQueue.pop();
        }
        m_chunkQueue = std::move(keep);

        logInfo(m_logger, "[Server] Player " << id << " left");
    });

    m_running.store(true);
    m_thread = std::thread([this]() { Run(); });
    return true;
}

void GameServer::Stop() {
    m_running.store(false);

    if (m_server) {
        m_server->Shutdown();
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_save) {
        for (auto& p : m_players) {
            if (!p.name.empty()) {
                m_save->SavePlayer(p.name, p.position, p.yaw, p.pitch);
            }
        }
    }

    if (m_save && m_dirty) {
        std::vector<BlockChange> flat;
        for (auto& [k, v] : m_pendingChanges) {
            flat.insert(flat.end(), v.begin(), v.end());
        }
        m_save->SaveChanges(flat);
        m_dirty = false;
    }

    logInfo(m_logger, "[Server] Stopped");
}

std::shared_ptr<Eng::tool::MemoryPipe> GameServer::GetMemoryPipe() const {
    if (auto* mem = dynamic_cast<Eng::tool::MemoryServerTransport*>(m_server.get())) {
        return mem->m_pipe;
    }
    return nullptr;
}

void GameServer::HandleMessage(int clientId, const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    auto type = static_cast<game::net::MessageType>(data[0]);
    Eng::tool::MessageReader r(std::span<const uint8_t>(data).subspan(1));

    switch (type) {
        case game::net::MessageType::PlayerInput: {
            game::net::PlayerInput in;
            in.moveDir = r.ReadVec3();
            in.look    = r.ReadVec2();
            in.jump    = r.Read<uint8_t>() != 0;
            in.dig     = r.Read<uint8_t>() != 0;
            in.place   = r.Read<uint8_t>() != 0;
            in.placeBlock = r.Read<uint16_t>();
            in.fly = r.Read<uint8_t>() != 0;

            ApplyPlayerInput(clientId, in);
            break;
        }
        case game::net::MessageType::ChunkRequest: {
            int cx = r.Read<int>();
            int cz = r.Read<int>();
            m_chunkQueue.push({clientId, cx, cz});
            break;
        }
        case game::net::MessageType::ClientHello: {
            uint8_t nlen = r.Read<uint8_t>();
            if (nlen > 32) nlen = 32;
            auto bytes = r.ReadBytes(nlen);
            std::string name(bytes.begin(), bytes.end());

            for (auto& p : m_players) {
                if (p.id == (uint32_t)clientId) {
                    p.name = name;
                    glm::vec3 sp; float sy = 0, spitch = 0;
                    if (m_save && m_save->LoadPlayer(name, sp, sy, spitch)) {
                        p.position = sp; p.yaw = sy; p.pitch = spitch;
                        logInfo(m_logger, "[Server] Player '" << name << "' restored");
                    } else {
                        logInfo(m_logger, "[Server] Player '" << name << "' is new");
                    }
                    break;
                }
            }
            break;
        }
        default:
            break;
    }
}

void GameServer::ApplyPlayerInput(int clientId, const net::PlayerInput& in) {
    for (auto& p : m_players) {
        if (p.id == (uint32_t)clientId) {
            p.moveDir = in.moveDir;
            p.yaw     = in.look.x;
            p.pitch   = in.look.y;
            p.jump    = in.jump;
            p.flyMode = in.fly;
            if (in.dig)   HandleDig(p);
            if (in.place) HandlePlace(p, in.placeBlock);
            return;
        }
    }
}

void GameServer::TickWorld(float dt) {
    std::lock_guard<std::mutex> lock(m_playersMutex);
    for (auto& p : m_players) {
        game::PlayerMotion m;
        m.position = p.position;
        m.velocity = p.velocity;
        m.onGround = p.onGround;
        m.flyMode  = p.flyMode;

        game::StepPlayer(m, p.moveDir, p.jump, dt,
            [this](int x, int y, int z) { return IsSolidAt(x, y, z); });

        p.position = m.position;
        p.velocity = m.velocity;
        p.onGround = m.onGround;
        p.jump = false;
    }
}

void GameServer::SpawnItemDrop(const glm::vec3& pos, BlockId id) {
    static thread_local std::mt19937 s_rng(std::random_device{}());
    static thread_local std::uniform_real_distribution<float> s_dist(-1.0f, 1.0f);
    ItemEntity e;
    e.id       = AllocEntityId();
    e.position = pos;
    e.velocity = glm::vec3(s_dist(s_rng) * 2.0f, 2.5f, s_dist(s_rng) * 2.0f);
    e.pickupDelay = 0.5f;
    e.itemType    = id;

    m_entities[e.id] = e;

    Eng::tool::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::EntitySpawn));
    w.Write<uint32_t>(e.id); // 注意：e 被 move 后，e.id 如果还在后面用会报 bugprone-use-after-move！
                             // 所以我们把 w 的写入移到 m_entities 之前，或者用临时变量存 id
    w.WriteVec3(pos);        // 这里直接用传进来的 pos 即可，万无一失
    w.Write<uint16_t>(id);   // 直接用传进来的 id
    
    m_server->Broadcast(w.GetBuffer());
}

void GameServer::TickEntities(float dt) {
    std::vector<uint32_t> toRemove;

    for (auto& [id, e] : m_entities) {
        game::StepPointEntity(e, dt,
            [this](int x, int y, int z) { return IsSolidAt(x, y, z); });

        if (e.pickupDelay > 0) e.pickupDelay -= dt;
        e.age += dt;

        {
            Eng::tool::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::EntityUpdate));
            w.Write<uint32_t>(id);
            w.WriteVec3(e.position);
            m_server->Broadcast(w.GetBuffer());
        }

        if (e.age > 300.0f) {
            Eng::tool::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::EntityDestroy));
            w.Write<uint32_t>(id);
            w.Write<uint8_t>(0);
            w.WriteU32BE(0);
            m_server->Broadcast(w.GetBuffer());
            toRemove.push_back(id);
            continue;
        }

        if (e.pickupDelay <= 0.0f) {
            for (auto& p : m_players) {
                glm::vec3 center = p.position + glm::vec3(0.0f, 0.9f, 0.0f);
                float d2 = glm::distance(e.position, center);
                if (d2 < 1.5f * 1.5f) {
                    Eng::tool::MessageWriter w;
                    w.Write(static_cast<uint8_t>(game::net::MessageType::EntityDestroy));
                    w.Write<uint32_t>(id);
                    w.Write<uint8_t>(1);
                    w.WriteU32BE(p.id);
                    m_server->Broadcast(w.GetBuffer());
                    toRemove.push_back(id);
                    break;
                }
            }
        }
    }

    for (uint32_t id : toRemove) {
        m_entities.erase(id);
        m_freeEntityIds.push_back(id);
    }
}

Chunk* GameServer::GetCachedChunk(int cx, int cz) {
    auto it = m_chunks.find(ChunkKey(cx, cz));
    return it == m_chunks.end() ? nullptr : &it->second;
}

void GameServer::HandleDig(ServerPlayer& p) {
    glm::vec3 front;
    front.x = cosf(p.yaw) * cosf(p.pitch);
    front.y = sinf(p.pitch);
    front.z = sinf(p.yaw) * cosf(p.pitch);
    front = glm::normalize(front);

    glm::vec3 eye = p.position + glm::vec3(0.0f, game::PLAYER_EYE, 0.0f);

    auto hit = game::RayCast(m_registry, eye, front, REACH_DISTANCE,
        [this](int x, int y, int z) { return GetBlockAt(x, y, z); });
    if (!hit.hit) return;

    BlockId oldBlock = GetBlockAt(hit.bx, hit.by, hit.bz);
    if (oldBlock == AIR_ID || oldBlock == UNKNOWN_ID) return;

    SetBlockAt(hit.bx, hit.by, hit.bz, AIR_ID);
    BroadcastBlockChange(hit.bx, hit.by, hit.bz, AIR_ID);

    int cx = game::WorldToChunkX(hit.bx);
    int cz = game::WorldToChunkZ(hit.bz);
    int lx = game::WorldToLocalX(hit.bx);
    int lz = game::WorldToLocalZ(hit.bz);
    auto it = m_chunks.find(game::ChunkKey(cx, cz));
    if (it != m_chunks.end()) {
        game::LightEngine::OnBlockChanged(m_registry, it->second, lx, hit.by, lz, oldBlock, AIR_ID);
    }

    SpawnItemDrop(
        glm::vec3(hit.bx + 0.5f, hit.by + 0.5f, hit.bz + 0.5f),
        oldBlock);
}

void GameServer::HandlePlace(ServerPlayer& p, BlockId type) {
    if (m_registry.Get(type).id == UNKNOWN_ID) return;

    glm::vec3 front;
    front.x = cos(p.yaw) * cos(p.pitch);
    front.y = sin(p.pitch);
    front.z = sin(p.yaw) * cos(p.pitch);
    front = glm::normalize(front);

    glm::vec3 eye = p.position + glm::vec3(0.0f, game::PLAYER_EYE, 0.0f);

    auto hit = game::RayCast(m_registry, eye, front, REACH_DISTANCE,
    [this](int x, int y, int z) { return GetBlockAt(x, y, z); });
    if (!hit.hit) return;

    int px = hit.bx + hit.nx;
    int py = hit.by + hit.ny;
    int pz = hit.bz + hit.nz;

    if (IsInsidePlayer(p, px, py, pz)) return;
    
    BlockId oldBlock = GetBlockAt(px, py, pz);
    if (oldBlock != AIR_ID) return;

    SetBlockAt(px, py, pz, type);
    BroadcastBlockChange(px, py, pz, type);

    int cx = game::WorldToChunkX(px);
    int cz = game::WorldToChunkZ(pz);
    int lx = game::WorldToLocalX(px);
    int lz = game::WorldToLocalZ(pz);
    auto it = m_chunks.find(game::ChunkKey(cx, cz));
    if (it != m_chunks.end()) {
        game::LightEngine::OnBlockChanged(m_registry, it->second, lx, py, lz, oldBlock, type);
    }
}

BlockId GameServer::GetBlockAt(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return AIR_ID;
    int cx = wx >> 4;
    int cz = wz >> 4;
    int lx = wx & 15;
    int lz = wz & 15;
    auto it = m_chunks.find(ChunkKey(cx, cz));
    if (it == m_chunks.end()) return AIR_ID;
    return ChunkGet(it->second, lx, wy, lz);
}

void GameServer::SetBlockAt(int wx, int wy, int wz, BlockId bt) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return;
    int cx = wx >> 4;
    int cz = wz >> 4;
    int lx = wx & 15;
    int lz = wz & 15;
    auto it = m_chunks.find(ChunkKey(cx, cz));
    if (it == m_chunks.end()) return;
    ChunkSet(it->second, lx, wy, lz, bt);
    m_pendingChanges[ChunkKey(cx, cz)].push_back({ wx, wy, wz, bt });
    m_dirty = true;
}

bool GameServer::IsInsidePlayer(const ServerPlayer& p, int bx, int by, int bz) {
    glm::vec3 pmin{p.position.x - PLAYER_HALF_W, p.position.y,        p.position.z - PLAYER_HALF_W};
    glm::vec3 pmax{p.position.x + PLAYER_HALF_W, p.position.y + PLAYER_HEIGHT, p.position.z + PLAYER_HALF_W};

    // ★ 规避 -Wimplicit-int-float-conversion：使用显式 static_cast
    glm::vec3 bmin{static_cast<float>(bx), static_cast<float>(by), static_cast<float>(bz)};
    glm::vec3 bmax{bmin.x + 1.0f, bmin.y + 1.0f, bmin.z + 1.0f};

    bool overlapX = pmin.x < bmax.x && pmax.x > bmin.x;
    bool overlapY = pmin.y < bmax.y && pmax.y > bmin.y;
    bool overlapZ = pmin.z < bmax.z && pmax.z > bmin.z;

    return overlapX && overlapY && overlapZ;
}

bool GameServer::AABBCollides(const glm::vec3& pos) const {
    int minX = static_cast<int>(std::floor(pos.x - PLAYER_HALF_W));
    int maxX = static_cast<int>(std::floor(pos.x + PLAYER_HALF_W));
    int minY = static_cast<int>(std::floor(pos.y));
    int maxY = static_cast<int>(std::floor(pos.y + PLAYER_HEIGHT));
    int minZ = static_cast<int>(std::floor(pos.z - PLAYER_HALF_W));
    int maxZ = static_cast<int>(std::floor(pos.z + PLAYER_HALF_W));

    for (int x = minX; x <= maxX; ++x)
        for (int y = minY; y <= maxY; ++y)
            for (int z = minZ; z <= maxZ; ++z)
                if (IsSolidAt(x, y, z)) return true;
    return false;
}

bool GameServer::IsSolidAt(int wx, int wy, int wz) const {
    BlockId b = GetBlockAt(wx, wy, wz);
    return m_registry.IsSolid(b);
}

void GameServer::BroadcastWorldState() {
    std::lock_guard<std::mutex> lock(m_playersMutex);
    Eng::tool::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::WorldState));
    w.WriteU32BE((uint32_t)m_players.size());
    w.Write<float>(m_lastTickMs);

    for (auto& p : m_players) {
        w.WriteU32BE(p.id);

        // ★ 规避 -Wimplicit-int-conversion：使用显式 static_cast
        uint8_t nlen = static_cast<uint8_t>(std::min<size_t>(p.name.size(), 32));
        w.Write<uint8_t>(nlen);
        for (uint8_t i = 0; i < nlen; ++i) {
            w.Write<uint8_t>(static_cast<uint8_t>(p.name[i]));
        }

        w.WriteVec3(p.position);
        w.Write(p.yaw);
        w.Write(p.pitch);
    }
    m_server->Broadcast(w.GetBuffer());
}

void GameServer::BroadcastBlockChange(int bx, int by, int bz, BlockId id) {
    Eng::tool::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::BlockChange));
    w.Write<int32_t>(bx);
    w.Write<int32_t>(by);
    w.Write<int32_t>(bz);
    w.Write<uint16_t>(id);
    m_server->Broadcast(w.GetBuffer());
}

} // namespace game::server