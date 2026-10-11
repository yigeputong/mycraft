#include "core/Log.h"
#include "core/MessageReader.h"
#include "core/MessageWriter.h"
#include "core/NetworkServer.h"
#include "game/GameServer.h"
#include "game/Protocol.h"
#include "game/server/WorldGenerator.h"
#include "game/core/RayCast.h"
#include "game/core/Physics.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <random>

namespace game::server {

void GameServer::StartWorkers(uint32_t seed) {
    unsigned hw = std::thread::hardware_concurrency();
    unsigned n  = std::clamp(hw > 2 ? hw - 2 : 2u, 2u, 12u);
    logInfo(m_logger, "[Server] Starting " << n << " chunk workers");
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
    TerrainGenerator terrain(seed);   // ★ 每个 worker 独立，避免噪声共享状态
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

        {
            std::lock_guard lk(m_resultMutex);
            m_resultQueue.push(std::move(r));
        }
    }
}

void GameServer::SendChunkTo(int clientId, const Chunk& chunk) {
    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkData));
    w.Write<int32_t>(chunk.chunk_x);
    w.Write<int32_t>(chunk.chunk_z);
    w.Write<uint16_t>(CHUNK_SIZE_Y);
    w.WriteBytes(reinterpret_cast<const uint8_t*>(chunk.blocks.data()),
                 chunk.blocks.size() * sizeof(BlockType));
    m_server.SendTo(clientId, w.GetBuffer());
}

void GameServer::DrainChunkResults() {
    constexpr int kMaxSendsPerTick = 32;

    int sends = 0;
    for (;;) {
        ChunkResult r;
        {
            std::lock_guard lk(m_resultMutex);
            if (m_resultQueue.empty() || sends >= kMaxSendsPerTick) break;
            r = std::move(m_resultQueue.front());
            m_resultQueue.pop();
        }

        // 存表
        m_chunks.emplace(r.key, std::move(r.chunk));
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

bool GameServer::Start(uint16_t port) {
    if (m_running.load()) return false;
    m_running.store(true);
    m_thread = std::thread([this, port]() {
        // 服务端线程里初始化监听
        if (!m_server.Listen(port)) {
            logError(m_logger, "[Server] Listen failed");
            m_running.store(false);
            return;
        }

        m_server.SetConnectCallback([this](int id) {
            ServerPlayer p;
            p.id = (uint32_t)id;
            m_players.push_back(p);

            // 只发一条"你的 ID"给这个客户端
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(net::MessageType::PlayerId));   // YourId
            w.WriteU32BE((uint32_t)id);
            m_server.SendTo(id, w.GetBuffer());

            logInfo(m_logger, "[Server] Player " << id << " joined");
        });

        m_server.SetMessageCallback([this](int id, const std::vector<uint8_t>& data) {
            HandleMessage(id, data);
        });

        m_server.SetDisconnectCallback([this](int id) {
            // 玩家
            m_players.erase(std::remove_if(m_players.begin(), m_players.end(),
                [id](const ServerPlayer& p) { return p.id == (uint32_t)id; }), m_players.end());

            // 该 client 的待发 chunk 请求
            std::queue<PendingChunkRequest> keep;
            while (!m_chunkQueue.empty()) {
                if (m_chunkQueue.front().clientId != id) keep.push(m_chunkQueue.front());
                m_chunkQueue.pop();
            }
            m_chunkQueue = std::move(keep);

            logInfo(m_logger, "[Server] Player " << id << " left");
        });

        Run();
    });
    return true;
}

void GameServer::Stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();
    m_server.Shutdown();
}

void GameServer::Run() {
    constexpr float TICK_INTERVAL = 1.0f / TPS;
    auto last = std::chrono::steady_clock::now();
    float accumulator = 0.0f;

    logInfo(m_logger, "[Server] Server Run");
    uint32_t seed = static_cast<uint32_t>(std::time(nullptr)) ^ static_cast<uint32_t>(std::clock());
    m_terrain = std::make_unique<TerrainGenerator>(seed);
    logInfo(m_logger, "[Server] World Seed = " << seed);

    StartWorkers(seed);

    while (m_running.load()) {
        m_server.PollAccept();
        m_server.Update();

        auto now = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(now - last).count();
        last = now;
        accumulator += frameTime;

        while (accumulator >= TICK_INTERVAL) {
            auto tickStart = std::chrono::steady_clock::now();

            // ---- 消费客户端请求，派发给 worker ----
            while (!m_chunkQueue.empty()) {
                auto req = m_chunkQueue.front();
                m_chunkQueue.pop();
                uint64_t key = ChunkKey(req.cx, req.cz);

                auto it = m_chunks.find(key);
                if (it != m_chunks.end()) {
                    SendChunkTo(req.clientId, it->second);   // 缓存命中
                    continue;
                }

                m_waitingClients[key].push_back(req.clientId);   // 记下等结果的人

                if (m_inFlight.count(key)) continue;             // 已在生成
                m_inFlight.insert(key);
                {
                    std::lock_guard lk(m_taskMutex);
                    m_taskQueue.push({req.cx, req.cz, key});
                }
                m_taskCv.notify_one();
            }

            // ---- 收结果 ----
            DrainChunkResults();

            TickWorld(TICK_INTERVAL);
            TickEntities(TICK_INTERVAL);
            BroadcastWorldState();

            auto tickEnd = std::chrono::steady_clock::now();
            m_lastTickMs = std::chrono::duration<float, std::milli>(tickEnd - tickStart).count();

            accumulator -= TICK_INTERVAL;
        }

        SDL_Delay(1);
    }

    StopWorkers();   // ★
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
        default:
        // 服务端不收 S→C 类消息
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
            if (in.place) HandlePlace(p, static_cast<BlockType>(in.placeBlock));
            return;
        }
    }
}

void GameServer::TickWorld(float dt) {
    for (auto& p : m_players) {
        game::PlayerMotion m;
        m.position = p.position;
        m.velocity = p.velocity;
        m.onGround = p.onGround;
        m.flyMode  = p.flyMode;   // ★

        game::StepPlayer(m, p.moveDir, p.jump, dt,
            [this](int x, int y, int z) { return IsSolidAt(x, y, z); });

        p.position = m.position;
        p.velocity = m.velocity;
        p.onGround = m.onGround;
        p.jump = false;
    }
}

void GameServer::SpawnItemDrop(const glm::vec3& pos, BlockType type) {
    static thread_local std::mt19937 s_rng(std::random_device{}());
    static thread_local std::uniform_real_distribution<float> s_dist(-1.0f, 1.0f);
    ItemEntity e;
    e.id       = AllocEntityId();
    e.position = pos;
    e.velocity = glm::vec3(s_dist(s_rng) * 2.0f, 2.5f, s_dist(s_rng) * 2.0f);
    e.pickupDelay = 0.5f;
    e.itemType    = type;

    m_entities[e.id] = e;

    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::EntitySpawn));
    w.Write<uint32_t>(e.id);
    w.WriteVec3(e.position);                       // ★ e.pos → e.position
    w.Write<uint16_t>(static_cast<uint16_t>(type));
    m_server.Broadcast(w.GetBuffer());
}

void GameServer::TickEntities(float dt) {
    std::vector<uint32_t> toRemove;

    for (auto& [id, e] : m_entities) {
        // ★ 直接传 e —— 它已经继承自 PointEntity
        game::StepPointEntity(e, dt,
            [this](int x, int y, int z) { return IsSolidAt(x, y, z); });

        // 计时器
        if (e.pickupDelay > 0) e.pickupDelay -= dt;
        e.age += dt;

        // 广播位置
        {
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::EntityUpdate));
            w.Write<uint32_t>(id);
            w.WriteVec3(e.position);              // ★ e.pos → e.position
            m_server.Broadcast(w.GetBuffer());
        }

        // 超时
        if (e.age > 300.0f) {
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::EntityDestroy));
            w.Write<uint32_t>(id);
            w.Write<uint8_t>(0);
            m_server.Broadcast(w.GetBuffer());
            toRemove.push_back(id);
            continue;
        }

        // 拾取检测
        if (e.pickupDelay <= 0.0f) {
            for (auto& p : m_players) {
                glm::vec3 center = p.position + glm::vec3(0.0f, 0.9f, 0.0f);
                float d2 = glm::distance(e.position, center);   // ★
                if (d2 < 1.5f * 1.5f) {
                    Eng::MessageWriter w;
                    w.Write(static_cast<uint8_t>(game::net::MessageType::EntityDestroy));
                    w.Write<uint32_t>(id);
                    w.Write<uint8_t>(1);
                    m_server.SendTo(p.id, w.GetBuffer());
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
    front.x = cos(p.yaw) * cos(p.pitch);
    front.y = sin(p.pitch);
    front.z = sin(p.yaw) * cos(p.pitch);
    front = glm::normalize(front);

    glm::vec3 eye = p.position + glm::vec3(0.0f, game::PLAYER_EYE, 0.0f);

    auto hit = game::RayCast(eye, front, REACH_DISTANCE,
        [this](int x, int y, int z) { return GetBlockAt(x, y, z); });
    if (!hit.hit) return;

    BlockType oldBlock = GetBlockAt(hit.bx, hit.by, hit.bz);

    SetBlockAt(hit.bx, hit.by, hit.bz, BlockType::Air);
    BroadcastBlockChange(hit.bx, hit.by, hit.bz, BlockType::Air);

    // 挖掉非空气方块 → 生成掉落物
    if (oldBlock != BlockType::Air && oldBlock != BlockType::Void) {
        SpawnItemDrop(
            glm::vec3(hit.bx + 0.5f, hit.by + 0.5f, hit.bz + 0.5f),
            oldBlock);
    }
}

void GameServer::HandlePlace(ServerPlayer& p, BlockType type) {
    glm::vec3 front;
    front.x = cos(p.yaw) * cos(p.pitch);
    front.y = sin(p.pitch);
    front.z = sin(p.yaw) * cos(p.pitch);
    front = glm::normalize(front);

    glm::vec3 eye = p.position + glm::vec3(0.0f, game::PLAYER_EYE, 0.0f);

    auto hit = game::RayCast(eye, front, REACH_DISTANCE,
        [this](int x, int y, int z) { return GetBlockAt(x, y, z); });
    if (!hit.hit) return;

    // 放在命中面的外侧
    int px = hit.bx + hit.nx;
    int py = hit.by + hit.ny;
    int pz = hit.bz + hit.nz;

    // 别把方块放进自己身体里
    if (IsInsidePlayer(p, px, py, pz)) return;
    if (GetBlockAt(px, py, pz) != BlockType::Air) return;

    SetBlockAt(px, py, pz, type);
    BroadcastBlockChange(px, py, pz, type);
}

BlockType GameServer::GetBlockAt(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return BlockType::Air;
    int cx = wx >> 4;
    int cz = wz >> 4;
    int lx = wx & 15;
    int lz = wz & 15;
    auto it = m_chunks.find(ChunkKey(cx, cz));
    if (it == m_chunks.end()) return BlockType::Air;
    return ChunkGet(it->second, lx, wy, lz);
}

void GameServer::SetBlockAt(int wx, int wy, int wz, BlockType bt) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return;
    int cx = wx >> 4;
    int cz = wz >> 4;
    int lx = wx & 15;
    int lz = wz & 15;
    auto it = m_chunks.find(ChunkKey(cx, cz));
    if (it == m_chunks.end()) return;
    ChunkSet(it->second, lx, wy, lz, bt);
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
    BlockType b = GetBlockAt(wx, wy, wz);
    return b != BlockType::Air && b != BlockType::Water;
}

void GameServer::BroadcastWorldState() {
    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::WorldState));
    w.WriteU32BE((uint32_t)m_players.size());
    w.Write<float>(m_lastTickMs);

    for (auto& p : m_players) {
        w.WriteU32BE(p.id);
        w.WriteVec3(p.position);
        w.Write(p.yaw);
        w.Write(p.pitch);
    }
    m_server.Broadcast(w.GetBuffer());
}

void GameServer::BroadcastBlockChange(int bx, int by, int bz, BlockType type) {
    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::BlockChange));
    w.Write<int32_t>(bx);
    w.Write<int32_t>(by);
    w.Write<int32_t>(bz);
    w.Write<uint16_t>(static_cast<uint16_t>(type));
    m_server.Broadcast(w.GetBuffer());
}

} // namespace game