#include "core/Log.h"
#include "core/MessageReader.h"
#include "core/MessageWriter.h"
#include "core/NetworkTransport.h"
#include "core/MemoryTransport.h"
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

namespace game::server {

void GameServer::StartWorkers(uint32_t seed) {
    unsigned hw = std::thread::hardware_concurrency();
    unsigned n  = std::clamp(hw > 2 ? hw - 2 : 2u, 2u, 12u);
    m_workersStop = false;
    for (unsigned i = 0; i < n; ++i) {
        m_workers.emplace_back([this, seed] { WorkerLoop(seed); });
    }
}

void GameServer::StopWorkers() {
    {
        std::lock_guard lk(m_taskMutex);
        m_workersStop = true;
    }
    m_taskCv.notify_all();
    for (auto& t : m_workers) if (t.joinable()) t.join();
    m_workers.clear();
}

void GameServer::WorkerLoop(uint32_t seed) {
    TerrainGenerator terrain(seed, m_registry); // ★ 必须把注册表传给生成器
    while (true) {
        ChunkTask task;
        {
            std::unique_lock lk(m_taskMutex);
            m_taskCv.wait(lk, [this] {
                return m_workersStop.load() || !m_taskQueue.empty();
            });
            if (m_workersStop.load() && m_taskQueue.empty()) return;
            task = m_taskQueue.front();
            m_taskQueue.pop();
        }

        ChunkResult r;
        r.key   = task.key;
        r.chunk = terrain.GenerateChunk(task.cx, task.cz);
        LightEngine::InitRegion(m_registry, r.chunk, nullptr, nullptr, nullptr, nullptr); // ★ 传注册表

        {
            std::lock_guard lk(m_resultMutex);
            m_resultQueue.push(r);
        }
    }
}

void GameServer::SendChunkTo(int clientId, const Chunk& chunk) {
    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkData));
    w.Write<int32_t>(chunk.chunk_x);
    w.Write<int32_t>(chunk.chunk_z);
    w.Write<uint16_t>(CHUNK_SIZE_Y);
    
    // 1. 方块数据
    w.WriteBytes(reinterpret_cast<const uint8_t*>(chunk.blocks.data()),
                 chunk.blocks.size() * sizeof(BlockId));
                 
    // 2. 天空光数组 (65536 字节)
    w.WriteBytes(chunk.sky.data(), chunk.sky.size());
    
    // 3. 方块光数组 (65536 * 3 字节)
    w.WriteBytes(reinterpret_cast<const uint8_t*>(chunk.block.data()), 
                 chunk.block.size() * sizeof(LightRGB));

    m_server->SendTo(clientId, w.GetBuffer());
}

void GameServer::DrainChunkResults() {
    constexpr int kMaxSendsPerTick = 4;

    int sends = 0;
    for (;;) {
        ChunkResult r;
        {
            std::lock_guard lk(m_resultMutex);
            if (m_resultQueue.empty() || sends >= kMaxSendsPerTick) break;
            r = m_resultQueue.front();
            m_resultQueue.pop();
        }

        auto cit = m_pendingChanges.find(r.key);
        if (cit != m_pendingChanges.end()) {
            for (const auto& c : cit->second) {
                int lx = c.x & 15;
                int lz = c.z & 15;
                ChunkSet(r.chunk, lx, c.y, lz, c.block); // ★ 直接传 uint16_t 的 BlockId
            }
        }

        // 存表
        m_chunks.emplace(r.key, r.chunk);
        m_inFlight.erase(r.key);

        // 发给等待的 client
        auto it = m_waitingClients.find(r.key);
        if (it != m_waitingClients.end()) {
            const Chunk& c = m_chunks.at(r.key);
            for (int cid : it->second) SendChunkTo(cid, c);
            m_waitingClients.erase(it);
        }

        ++sends;
    }
}

bool GameServer::Start(uint16_t port, const std::string& saveDir, bool useMemory) {
    if (m_running.load()) return false;

    // 服务端初始化注册表
    m_registry.LoadAll("assets/block");

    // ---- 存档 ----
    m_save = std::make_unique<WorldSave>(saveDir);

    uint32_t seed = 0;
    glm::vec3 spawn(0.0f, 256.0f, 0.0f);
    if (m_save->LoadLevel(seed, spawn)) {
        m_worldSeed = seed;
        m_spawnPos  = spawn;
        logInfo(m_logger, "[Server] Loaded world, seed=" << seed);
    } else {
        seed = static_cast<uint32_t>(std::time(nullptr))
            ^ static_cast<uint32_t>(std::clock());
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
        m_server = std::make_unique<Eng::MemoryServerTransport>();
    } else {
        m_server = std::make_unique<Eng::NetworkServerTransport>();
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

        // 你的 ID
        {
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(net::MessageType::PlayerId));
            w.WriteU32BE((uint32_t)id);
            m_server->SendTo(id, w.GetBuffer());
        }
        // seed
        {
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(net::MessageType::ServerInfo));
            w.WriteU32BE(m_worldSeed);
            w.WriteVec3(m_spawnPos);
            m_server->SendTo(id, w.GetBuffer());
        }

        // 补发所有现有掉落物
        for (auto& [eid, e] : m_entities) {
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(net::MessageType::EntitySpawn));
            w.Write<uint32_t>(eid);
            w.WriteVec3(e.position);
            w.Write<uint16_t>(e.itemType); // ★ BlockId
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

    // 先让网络停止接收和发送，打破子线程的阻塞
    if (m_server) {
        m_server->Shutdown();
    }

    // 然后再等待子线程优雅退出
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

std::shared_ptr<Eng::MemoryPipe> GameServer::GetMemoryPipe() const {
    if (auto* mem = dynamic_cast<Eng::MemoryServerTransport*>(m_server.get())) {
        return mem->m_pipe;
    }
    return nullptr;
}

void GameServer::Run() {
    constexpr float TICK_INTERVAL = 1.0f / TPS;
    auto last = std::chrono::steady_clock::now();
    float accumulator = 0.0f;

    // 服务端主线程也保留一个 terrain（按需），但 chunk 生成已全在 Worker 里
    m_terrain = std::make_unique<TerrainGenerator>(m_worldSeed, m_registry); // ★

    StartWorkers(m_worldSeed);

    while (m_running.load()) {
        m_server->Update();

        auto now = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(now - last).count();
        last = now;
        accumulator += frameTime;

        while (accumulator >= TICK_INTERVAL) {
            auto tickStart = std::chrono::steady_clock::now();

            while (!m_chunkQueue.empty()) {
                auto req = m_chunkQueue.front();
                m_chunkQueue.pop();
                uint64_t key = ChunkKey(req.cx, req.cz);

                auto it = m_chunks.find(key);
                if (it != m_chunks.end()) {
                    SendChunkTo(req.clientId, it->second);
                    continue;
                }

                m_waitingClients[key].push_back(req.clientId);

                if (m_inFlight.count(key)) continue;
                m_inFlight.insert(key);
                {
                    std::lock_guard lk(m_taskMutex);
                    m_taskQueue.push({req.cx, req.cz, key});
                }
                m_taskCv.notify_one();
            }

            DrainChunkResults();

            TickWorld(TICK_INTERVAL);
            TickEntities(TICK_INTERVAL);
            m_saveAccum += TICK_INTERVAL;
            if (m_dirty && m_saveAccum >= kSaveIntervalSec) {
                std::vector<BlockChange> flat;
                for (auto& [k, v] : m_pendingChanges)
                    flat.insert(flat.end(), v.begin(), v.end());
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

void GameServer::HandleMessage(int clientId, const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    auto type = static_cast<game::net::MessageType>(data[0]);
    Eng::MessageReader r(std::span<const uint8_t>(data).subspan(1));

    switch (type) {
        case game::net::MessageType::PlayerInput: {
            game::net::PlayerInput in;
            in.moveDir = r.ReadVec3();
            in.look    = r.ReadVec2();
            in.jump    = r.Read<uint8_t>() != 0;
            in.dig     = r.Read<uint8_t>() != 0;
            in.place   = r.Read<uint8_t>() != 0;
            in.placeBlock = r.Read<uint16_t>(); // 已经是 BlockId
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
            if (in.place) HandlePlace(p, in.placeBlock); // ★ 直接传 BlockId
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
    e.itemType    = id; // ★

    m_entities[e.id] = e;

    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::EntitySpawn));
    w.Write<uint32_t>(e.id);
    w.WriteVec3(e.position);
    w.Write<uint16_t>(e.itemType); // ★
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
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::EntityUpdate));
            w.Write<uint32_t>(id);
            w.WriteVec3(e.position);
            m_server->Broadcast(w.GetBuffer());
        }

        if (e.age > 300.0f) {
            Eng::MessageWriter w;
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
                    Eng::MessageWriter w;
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
    if (oldBlock == AIR_ID || oldBlock == UNKNOWN_ID) return; // 防止挖空气

    SetBlockAt(hit.bx, hit.by, hit.bz, AIR_ID);
    BroadcastBlockChange(hit.bx, hit.by, hit.bz, AIR_ID);

    // 重算服务端光照
    int cx = game::WorldToChunkX(hit.bx);
    int cz = game::WorldToChunkZ(hit.bz);
    int lx = game::WorldToLocalX(hit.bx);
    int lz = game::WorldToLocalZ(hit.bz);
    auto it = m_chunks.find(game::ChunkKey(cx, cz));
    if (it != m_chunks.end()) {
        game::LightEngine::OnBlockChanged(m_registry, it->second, lx, hit.by, lz, oldBlock, AIR_ID);
    }

    // 挖掉非空气方块 → 生成掉落物
    SpawnItemDrop(
        glm::vec3(hit.bx + 0.5f, hit.by + 0.5f, hit.bz + 0.5f),
        oldBlock);
}

void GameServer::HandlePlace(ServerPlayer& p, BlockId type) {
    // ★ 校验方块是否合法
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

    // 放在命中面的外侧
    int px = hit.bx + hit.nx;
    int py = hit.by + hit.ny;
    int pz = hit.bz + hit.nz;

    // 别把方块放进自己身体里
    if (IsInsidePlayer(p, px, py, pz)) return;
    
    BlockId oldBlock = GetBlockAt(px, py, pz);
    if (oldBlock != AIR_ID) return;

    SetBlockAt(px, py, pz, type);
    BroadcastBlockChange(px, py, pz, type);

    // 重算服务端光照
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
    // 记录改动（供存档）
    m_pendingChanges[ChunkKey(cx, cz)].push_back({ wx, wy, wz, bt });
    m_dirty = true;
}

bool GameServer::IsInsidePlayer(const ServerPlayer& p, int bx, int by, int bz) {

    glm::vec3 pmin{p.position.x - PLAYER_HALF_W, p.position.y,        p.position.z - PLAYER_HALF_W};
    glm::vec3 pmax{p.position.x + PLAYER_HALF_W, p.position.y + PLAYER_HEIGHT, p.position.z + PLAYER_HALF_W};

    // 方块 AABB：[bx, bx+1] × [by, by+1] × [bz, bz+1]
    glm::vec3 bmin{(float)bx, (float)by, (float)bz};
    glm::vec3 bmax{bmin.x + 1.0f, bmin.y + 1.0f, bmin.z + 1.0f};

    // 三个轴都重叠才算相交
    bool overlapX = pmin.x < bmax.x && pmax.x > bmin.x;
    bool overlapY = pmin.y < bmax.y && pmax.y > bmin.y;
    bool overlapZ = pmin.z < bmax.z && pmax.z > bmin.z;

    return overlapX && overlapY && overlapZ;
}

bool GameServer::AABBCollides(const glm::vec3& pos) const {
    int minX = (int)std::floor(pos.x - PLAYER_HALF_W);
    int maxX = (int)std::floor(pos.x + PLAYER_HALF_W);
    int minY = (int)std::floor(pos.y);
    int maxY = (int)std::floor(pos.y + PLAYER_HEIGHT);
    int minZ = (int)std::floor(pos.z - PLAYER_HALF_W);
    int maxZ = (int)std::floor(pos.z + PLAYER_HALF_W);

    for (int x = minX; x <= maxX; ++x)
        for (int y = minY; y <= maxY; ++y)
            for (int z = minZ; z <= maxZ; ++z)
                if (IsSolidAt(x, y, z)) return true;
    return false;
}

bool GameServer::IsSolidAt(int wx, int wy, int wz) const {
    BlockId b = GetBlockAt(wx, wy, wz);
    return m_registry.IsSolid(b); // ★ 使用实例
}

void GameServer::BroadcastWorldState() {
    std::lock_guard<std::mutex> lock(m_playersMutex);
    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::WorldState));
    w.WriteU32BE((uint32_t)m_players.size());
    w.Write<float>(m_lastTickMs);

    for (auto& p : m_players) {
        w.WriteU32BE(p.id);

        uint8_t nlen = (uint8_t)std::min<size_t>(p.name.size(), 32);
        w.Write<uint8_t>(nlen);
        for (uint8_t i = 0; i < nlen; ++i) {
            w.Write<uint8_t>((uint8_t)p.name[i]);
        }

        w.WriteVec3(p.position);
        w.Write(p.yaw);
        w.Write(p.pitch);
    }
    m_server->Broadcast(w.GetBuffer());
}

void GameServer::BroadcastBlockChange(int bx, int by, int bz, BlockId id) {
    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::BlockChange));
    w.Write<int32_t>(bx);
    w.Write<int32_t>(by);
    w.Write<int32_t>(bz);
    w.Write<uint16_t>(id); // ★
    m_server->Broadcast(w.GetBuffer());
}

} // namespace game