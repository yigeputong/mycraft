#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <random>

#include "Engine/tool/MessageReader.h"
#include "Engine/tool/MessageWriter.h"
#include "Engine/tool/NetworkTransport.h"
#include "Engine/tool/Transport.h"

#include "game/GameServer.h"
#include "game/Protocol.h"
#include "game/core/LightEngine.h"
#include "game/core/Physics.h"
#include "game/core/RayCast.h"
#include "game/core/ecs/PhysicsSystem.h"
#include "game/server/WorldGenerator.h"

namespace game::server {

// ============================================================
// ECS 辅助方法
// ============================================================
game::core::ecs::EntityId GameServer::CreateNetworkEntity(uint32_t& outNetId) {
    outNetId = m_nextNetEntityId++;
    auto eid = m_ecsWorld.CreateEntity();
    m_netToEcsMap[outNetId] = eid;
    m_ecsToNetMap[eid] = outNetId;
    return eid;
}

game::server::ServerEntity* GameServer::GetEntityByNetId(uint32_t netId) {
    auto it = m_netToEcsMap.find(netId);
    if (it != m_netToEcsMap.end()) {
        return m_ecsWorld.GetEntity(it->second);
    }
    return nullptr;
}

// ============================================================
// 流水线进度查询
// ============================================================
GenPhase GameServer::GetChunkProgress(int cx, int cz) const {
    uint64_t key = ChunkKey(cx, cz);
    std::shared_lock lock(m_genMutex);
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
    unsigned n = std::clamp(hw > 2 ? hw - 2 : 2u, 2u, 12u);
    m_workersStop = false;

    for (unsigned i = 0; i < n; ++i) {
        m_workers.push_back(
            std::make_unique<Eng::tool::RAIIThread>([this, seed](const std::stop_token& st) { WorkerLoop(seed, st); }));
    }
}

void GameServer::StopWorkers() {
    {
        std::lock_guard lk(m_taskMutex);
        m_workersStop = true;
    }
    m_taskCv.notify_all();
    m_workers.clear();
}

void GameServer::WorkerLoop(uint32_t seed, const std::stop_token& st) {
    TerrainGenerator terrain(seed, m_registry);
    while (true) {
        Eng::tool::UniqueFunction task;
        {
            std::unique_lock lk(m_taskMutex);
            m_taskCv.wait(lk,
                          [this, &st] { return m_workersStop.load() || st.stop_requested() || !m_taskQueue.empty(); });
            if ((m_workersStop.load() || st.stop_requested()) && m_taskQueue.empty())
                return;
            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }
        task();
    }
}

void GameServer::DrainChunkResults() {
    constexpr int kMaxSendsPerTick = 64;
    int sends = 0;

    for (;;) {
        ChunkResult r;
        {
            std::lock_guard lk(m_resultMutex);
            if (m_resultQueue.empty() || sends >= kMaxSendsPerTick)
                break;
            r = std::move(m_resultQueue.front());
            m_resultQueue.pop();
        }

        auto cit = m_pendingChanges.find(r.key);
        if (cit != m_pendingChanges.end()) {
            for (const auto& c : cit->second) {
                int lx = c.x & 15;
                int lz = c.z & 15;
                ChunkSet(*r.chunk, lx, c.y, lz, c.block);
            }
            int cx = r.chunk->chunk_x;
            int cz = r.chunk->chunk_z;
            auto* cXm = GetCachedChunk(cx - 1, cz);
            auto* cXp = GetCachedChunk(cx + 1, cz);
            auto* cZm = GetCachedChunk(cx, cz - 1);
            auto* cZp = GetCachedChunk(cx, cz + 1);
            LightEngine::InitRegion(m_registry, *r.chunk, cXm, cXp, cZm, cZp);
        }

        if (r.chunk)
            m_chunks.emplace(r.key, *r.chunk);
        {
            std::unique_lock lock(m_genMutex);
            m_generatingChunks.erase(r.key);
        }

        auto it = m_waitingClients.find(r.key);
        if (it != m_waitingClients.end()) {
            const Chunk& c = m_chunks.at(r.key);
            for (int cid : it->second)
                SendChunkTo(cid, c);
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
    w.WriteBytes(reinterpret_cast<const uint8_t*>(chunk.blocks.data()), chunk.blocks.size() * sizeof(BlockId));
    w.WriteBytes(chunk.sky.data(), chunk.sky.size());
    w.WriteBytes(reinterpret_cast<const uint8_t*>(chunk.block.data()), chunk.block.size() * sizeof(LightRGB));
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
        if (m_save)
            m_save->RefreshLock();

        auto now = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(now - last).count();
        last = now;
        accumulator += frameTime;

        while (accumulator >= TICK_INTERVAL) {
            auto tickStart = std::chrono::steady_clock::now();

            while (!m_chunkQueue.empty()) {
                {
                    std::shared_lock lock(m_genMutex);
                    if (m_generatingChunks.size() >= MAX_CONCURRENT_GENERATING)
                        break;
                }

                auto req = m_chunkQueue.front();
                m_chunkQueue.pop();
                uint64_t key = ChunkKey(req.cx, req.cz);

                if (m_chunks.contains(key)) {
                    SendChunkTo(req.clientId, m_chunks.at(key));
                    continue;
                }

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
                    m_terrain->GeneratePhase_BiomeAndDensity(*inFlight->ctx);
                    inFlight->phase.store(GenPhase::TerrainBlocks, std::memory_order_relaxed);
                    m_terrain->GeneratePhase_TerrainBlocks(*inFlight->ctx);
                    inFlight->phase.store(GenPhase::Features, std::memory_order_relaxed);
                    m_terrain->GeneratePhase_Features(*inFlight->ctx);
                    inFlight->phase.store(GenPhase::Lighting, std::memory_order_relaxed);
                    LightEngine::InitRegion(m_registry, *inFlight->ctx->chunk, nullptr, nullptr, nullptr, nullptr);
                    inFlight->phase.store(GenPhase::Ready, std::memory_order_relaxed);
                    std::lock_guard lk4(m_resultMutex);
                    m_resultQueue.push({inFlight->key, std::move(inFlight->ctx->chunk)});
                });
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

// ============================================================
// 生命周期与网络回调
// ============================================================
bool GameServer::Start(uint16_t port, const std::string& worldId, bool useMemory) {
    if (m_running.load())
        return false;

    m_registry.LoadAll("assets/block");
    m_save = std::make_unique<WorldSave>(worldId);
    if (!m_save->Lock()) {
        m_save->Unlock();
        return false;
    }

    WorldMetadata meta;
    glm::vec3 spawn(0.0f, 256.0f, 0.0f);
    if (m_save->LoadLevel(meta, spawn)) {
        m_worldSeed = meta.seed;
        m_spawnPos = spawn;
    } else {
        m_save->Unlock();
        return false;
    }

    bool needNewSpawn = (m_spawnPos.y < 60.0f || m_spawnPos.y > 120.0f);
    if (needNewSpawn) {
        TerrainGenerator gen(m_worldSeed, m_registry);
        glm::vec3 foundPos(0.0f);
        bool found = false;
        BlockId waterId = m_registry.Get("mycraft:water").id;

        for (int r = 0; r <= 3 && !found; ++r) {
            for (int cx = -r; cx <= r && !found; ++cx) {
                for (int cz = -r; cz <= r && !found; ++cz) {
                    if (r > 0 && std::abs(cx) != r && std::abs(cz) != r)
                        continue;
                    Chunk chunk = gen.GenerateChunk(cx, cz);
                    uint64_t key = ChunkKey(cx, cz);
                    if (!m_chunks.contains(key))
                        m_chunks.emplace(key, chunk);

                    for (int lx = 0; lx < 16 && !found; ++lx) {
                        for (int lz = 0; lz < 16 && !found; ++lz) {
                            for (int y = CHUNK_SIZE_Y - 1; y > 0; --y) {
                                BlockId b = ChunkGet(chunk, lx, y, lz);
                                if (b != AIR_ID && b != waterId) {
                                    BlockId h1 = ChunkGet(chunk, lx, y + 1, lz);
                                    BlockId h2 = ChunkGet(chunk, lx, y + 2, lz);
                                    if (h1 == AIR_ID && h2 == AIR_ID && y > SEA_LEVEL) {
                                        foundPos = glm::vec3(cx * 16 + lx + 0.5f, y + 1.0f, cz * 16 + lz + 0.5f);
                                        found = true;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (found) {
            m_spawnPos = foundPos;
            for (auto& [key, chunk] : m_chunks) {
                int cx = chunk.chunk_x;
                int cz = chunk.chunk_z;
                LightEngine::InitRegion(m_registry, chunk, GetCachedChunk(cx - 1, cz), GetCachedChunk(cx + 1, cz),
                                        GetCachedChunk(cx, cz - 1), GetCachedChunk(cx, cz + 1));
            }
        } else {
            m_spawnPos = glm::vec3(0.5f, 150.0f, 0.5f);
        }
        m_save->SaveLevel(meta, m_spawnPos);
    }

    auto changes = m_save->LoadChanges();
    for (auto& c : changes)
        m_pendingChanges[ChunkKey(c.x >> 4, c.z >> 4)].push_back(c);

    if (useMemory)
        m_server = std::make_unique<Eng::tool::MemoryServerTransport>();
    else
        m_server = std::make_unique<Eng::tool::NetworkServerTransport>();

    if (!m_server->Listen(port)) {
        m_save->Unlock();
        return false;
    }

    m_server->SetConnectCallback([this](int id) {
        uint32_t netId = (uint32_t)id;

        // 1. 初始化玩家实体
        {
            std::lock_guard<std::mutex> lock(m_worldMutex);
            auto eid = CreateNetworkEntity(netId);
            auto* e = m_ecsWorld.GetEntity(eid);
            if (!e)
                return;

            e->transform.position = m_spawnPos;
            e->serverPlayer = ServerPlayerComponent{};
            e->serverPlayer->clientId = netId;
            e->serverPlayer->inventory[0].type = m_registry.Get("mycraft:grass_block").id;
            e->serverPlayer->inventory[0].count = 64;
            e->serverPlayer->inventory[1].type = m_registry.Get("mycraft:dirt").id;
            e->serverPlayer->inventory[1].count = 64;
        }

        // 2. 发送网络包 (无需加锁)
        {
            Eng::tool::MessageWriter w;
            w.Write(static_cast<uint8_t>(net::MessageType::PlayerId));
            w.WriteU32BE(netId);
            m_server->SendTo(id, w.GetBuffer());
        }
        {
            Eng::tool::MessageWriter w;
            w.Write(static_cast<uint8_t>(net::MessageType::ServerInfo));
            w.WriteU32BE(m_worldSeed);
            w.WriteVec3(m_spawnPos);
            m_server->SendTo(id, w.GetBuffer());
        }

        // 3. 发送已有实体 (读取实体列表时加锁)
        {
            std::lock_guard<std::mutex> lock(m_worldMutex);
            for (auto& ent : m_ecsWorld.GetEntities()) {
                if (!ent.active || !ent.item)
                    continue;
                uint32_t itemNetId = m_ecsToNetMap[ent.id];
                Eng::tool::MessageWriter w;
                w.Write(static_cast<uint8_t>(game::net::MessageType::EntitySpawn));
                w.Write<uint32_t>(itemNetId);
                w.WriteVec3(ent.transform.position);
                w.Write<uint16_t>(ent.item->itemType);
                m_server->SendTo(id, w.GetBuffer());
            }
        }

        // 4. 发送背包 (内部已有锁，放心调用)
        SendInventoryUpdate(id);
        logInfo(m_logger, "[Server] Player " << id << " joined");
    });

    m_server->SetMessageCallback([this](int id, const std::vector<uint8_t>& data) { HandleMessage(id, data); });

    m_server->SetDisconnectCallback([this](int id) {
        std::lock_guard<std::mutex> lock(m_worldMutex);
        uint32_t netId = (uint32_t)id;
        auto it = m_netToEcsMap.find(netId);
        if (it != m_netToEcsMap.end()) {
            auto* e = m_ecsWorld.GetEntity(it->second);
            if (e && e->serverPlayer) {
                if (m_save && !e->serverPlayer->name.empty()) {
                    m_save->SavePlayer(e->serverPlayer->name, e->transform.position, e->serverPlayer->yaw,
                                       e->serverPlayer->pitch, e->serverPlayer->inventory);
                }
                m_ecsToNetMap.erase(e->id);
                m_ecsWorld.DestroyEntity(e->id);
            }
            m_netToEcsMap.erase(it);
        }
    });

    m_running.store(true);
    m_thread = std::thread([this]() { Run(); });
    return true;
}

void GameServer::Stop() {
    m_running.store(false);
    if (m_server)
        m_server->Shutdown();
    if (m_thread.joinable())
        m_thread.join();
    std::lock_guard<std::mutex> lock(m_worldMutex);
    if (m_save) {
        for (auto& e : m_ecsWorld.GetEntities()) {
            if (e.active && e.serverPlayer && !e.serverPlayer->name.empty()) {
                m_save->SavePlayer(e.serverPlayer->name, e.transform.position, e.serverPlayer->yaw,
                                   e.serverPlayer->pitch, e.serverPlayer->inventory);
            }
        }
        if (m_dirty) {
            std::vector<BlockChange> flat;
            for (auto& [k, v] : m_pendingChanges)
                flat.insert(flat.end(), v.begin(), v.end());
            m_save->SaveChanges(flat);
        }
        m_save->Unlock();
    }
    m_ecsWorld.Clear();
    m_netToEcsMap.clear();
    m_ecsToNetMap.clear();
}

std::shared_ptr<Eng::tool::MemoryPipe> GameServer::GetMemoryPipe() const {
    if (auto* mem = dynamic_cast<Eng::tool::MemoryServerTransport*>(m_server.get()))
        return mem->m_pipe;
    return nullptr;
}

void GameServer::HandleMessage(int clientId, const std::vector<uint8_t>& data) {
    if (data.empty())
        return;
    auto type = static_cast<game::net::MessageType>(data[0]);
    Eng::tool::MessageReader r(std::span<const uint8_t>(data).subspan(1));

    switch (type) {
        case game::net::MessageType::PlayerInput: {
            game::net::PlayerInput in;
            in.moveDir = r.ReadVec3();
            in.look = r.ReadVec2();
            in.jump = r.Read<uint8_t>() != 0;
            in.dig = r.Read<uint8_t>() != 0;
            in.place = r.Read<uint8_t>() != 0;
            in.placeBlock = r.Read<uint16_t>();
            in.fly = r.Read<uint8_t>() != 0;
            ApplyPlayerInput(clientId, in);
            if (in.place)
                SendInventoryUpdate(clientId);
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
            if (nlen > 32)
                nlen = 32;
            auto bytes = r.ReadBytes(nlen);
            std::string name(bytes.begin(), bytes.end());
            std::lock_guard<std::mutex> lock(m_worldMutex);
            auto* e = GetEntityByNetId((uint32_t)clientId);
            if (e && e->serverPlayer) {
                e->serverPlayer->name = name;
                glm::vec3 sp;
                float sy = 0, spitch = 0;
                if (m_save && m_save->LoadPlayer(name, sp, sy, spitch, e->serverPlayer->inventory)) {
                    e->transform.position = sp;
                    e->serverPlayer->yaw = sy;
                    e->serverPlayer->pitch = spitch;
                    if (e->transform.position.y < 60.0f || e->transform.position.y > 150.0f)
                        e->transform.position = m_spawnPos;
                } else {
                    e->transform.position = m_spawnPos;
                }
            }
            break;
        }
        case game::net::MessageType::ClickContainer:
            HandleClickContainer(clientId, r);
            break;
        default:
            break;
    }
}

void GameServer::ApplyPlayerInput(int clientId, const net::PlayerInput& in) {
    std::lock_guard<std::mutex> lock(m_worldMutex);
    auto* e = GetEntityByNetId((uint32_t)clientId);
    if (!e || !e->serverPlayer)
        return;

    e->serverPlayer->moveInput = in.moveDir;
    e->serverPlayer->yaw = in.look.x;
    e->serverPlayer->pitch = in.look.y;
    e->serverPlayer->jumpInput = in.jump;
    e->serverPlayer->flyMode = in.fly;

    if (in.dig)
        HandleDig(*e);
    if (in.place)
        HandlePlace(*e, in.placeBlock);
}

void GameServer::TickWorld(float dt) {
    std::lock_guard<std::mutex> lock(m_worldMutex);
    for (auto& e : m_ecsWorld.GetEntities()) {
        if (!e.active || !e.serverPlayer)
            continue;

        game::PlayerMotion m;
        m.position = e.transform.position;
        m.velocity = e.serverPlayer->velocity;
        m.onGround = e.serverPlayer->onGround;
        m.flyMode = e.serverPlayer->flyMode;

        game::StepPlayer(m, e.serverPlayer->moveInput, e.serverPlayer->jumpInput, dt,
                         [this](int x, int y, int z) { return IsSolidAt(x, y, z); });

        e.transform.position = m.position;
        e.serverPlayer->velocity = m.velocity;
        e.serverPlayer->onGround = m.onGround;
        e.serverPlayer->jumpInput = false;
    }
}

void GameServer::SpawnItemDrop(const glm::vec3& pos, BlockId id) {
    static thread_local std::mt19937 s_rng(std::random_device{}());
    static thread_local std::uniform_real_distribution<float> s_dist(-1.0f, 1.0f);
    uint32_t netId = 0;
    auto eid = CreateNetworkEntity(netId);
    auto* e = m_ecsWorld.GetEntity(eid);
    if (!e)
        return;

    e->transform.position = pos;
    e->velocity =
        game::core::ecs::VelocityComponent{.moveDir = glm::normalize(glm::vec3(s_dist(s_rng), 0.0f, s_dist(s_rng))),
                                           .speed = 2.0f,
                                           .yVelocity = 2.5f,
                                           .onGround = false};
    e->aabb = game::core::ecs::AABBComponent{.halfExtents = glm::vec3(0.15f, 0.15f, 0.15f), .positionIsFeet = false};
    e->item =
        game::core::ecs::ItemComponent{.itemType = id, .count = 1, .pickupDelay = 0.5f, .bobTime = 0.0f, .age = 0.0f};

    Eng::tool::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::EntitySpawn));
    w.Write<uint32_t>(netId);
    w.WriteVec3(pos);
    w.Write<uint16_t>(id);
    m_server->Broadcast(w.GetBuffer());
}

void GameServer::TickEntities(float dt) {
    game::core::ecs::SystemContext ctx;
    ctx.dt = dt;
    ctx.playerPos = glm::vec3(0.0f);
    ctx.isSolidAt = [this](int x, int y, int z) { return IsSolidAt(x, y, z); };

    static game::core::ecs::PhysicsSystem<ServerEntityManager> physicsSystem;
    physicsSystem.Update(m_ecsWorld, ctx);

    std::vector<game::core::ecs::EntityId> toDestroy;
    std::vector<int> clientsToSync;

    {
        std::lock_guard<std::mutex> lock(m_worldMutex);
        for (auto& e : m_ecsWorld.GetEntities()) {
            if (!e.active || !e.item)
                continue;
            auto& item = *e.item;
            uint32_t netId = m_ecsToNetMap[e.id];

            {
                Eng::tool::MessageWriter w;
                w.Write(static_cast<uint8_t>(game::net::MessageType::EntityUpdate));
                w.Write<uint32_t>(netId);
                w.WriteVec3(e.transform.position);
                m_server->Broadcast(w.GetBuffer());
            }

            item.age += dt;
            if (item.pickupDelay > 0)
                item.pickupDelay -= dt;

            if (item.age > 300.0f) {
                Eng::tool::MessageWriter w;
                w.Write(static_cast<uint8_t>(game::net::MessageType::EntityDestroy));
                w.Write<uint32_t>(netId);
                w.Write<uint8_t>(0);
                w.WriteU32BE(0);
                m_server->Broadcast(w.GetBuffer());
                toDestroy.push_back(e.id);
                continue;
            }

            if (item.pickupDelay <= 0.0f) {
                for (auto& playerEnt : m_ecsWorld.GetEntities()) {
                    if (!playerEnt.active || !playerEnt.serverPlayer)
                        continue;
                    glm::vec3 center = playerEnt.transform.position + glm::vec3(0.0f, 0.9f, 0.0f);
                    if (glm::distance(e.transform.position, center) < 1.5f) {
                        Eng::tool::MessageWriter w;
                        w.Write(static_cast<uint8_t>(game::net::MessageType::EntityDestroy));
                        w.Write<uint32_t>(netId);
                        w.Write<uint8_t>(1);
                        w.WriteU32BE(playerEnt.serverPlayer->clientId);
                        m_server->Broadcast(w.GetBuffer());

                        AddToInventory(playerEnt, item.itemType, 1);
                        playerEnt.serverPlayer->inventoryStateId++;
                        clientsToSync.push_back(playerEnt.serverPlayer->clientId);
                        toDestroy.push_back(e.id);
                        break;
                    }
                }
            }
        }
    }

    // 销毁实体
    if (!toDestroy.empty()) {
        std::lock_guard<std::mutex> lock(m_worldMutex);
        for (auto eid : toDestroy) {
            auto it = m_ecsToNetMap.find(eid);
            if (it != m_ecsToNetMap.end()) {
                m_netToEcsMap.erase(it->second);
                m_ecsToNetMap.erase(it);
            }
            m_ecsWorld.DestroyEntity(eid);
        }
    }

    // 在锁外部进行网络同步
    for (int cid : clientsToSync) {
        SendInventoryUpdate(cid);
    }
}

Chunk* GameServer::GetCachedChunk(int cx, int cz) {
    auto it = m_chunks.find(ChunkKey(cx, cz));
    return it == m_chunks.end() ? nullptr : &it->second;
}

void GameServer::HandleDig(ServerEntity& playerEntity) {
    // 判空必须在这里做：HandleDig 由消息分发直接调用，参数只保证「实体存在」，
    // 不保证「还是玩家」。断线清理 / 槽位复用的窗口期，serverPlayer 可能已被 reset。
    if (!playerEntity.active || !playerEntity.serverPlayer.has_value())
        return;

    auto& p = *playerEntity.serverPlayer;
    glm::vec3 front;
    front.x = cosf(p.yaw) * cosf(p.pitch);
    front.y = sinf(p.pitch);
    front.z = sinf(p.yaw) * cosf(p.pitch);
    front = glm::normalize(front);

    glm::vec3 eye = playerEntity.transform.position + glm::vec3(0.0f, game::PLAYER_EYE, 0.0f);
    auto hit = game::RayCast(m_registry, eye, front, REACH_DISTANCE,
                             [this](int x, int y, int z) { return GetBlockAt(x, y, z); });
    if (!hit.hit)
        return;

    BlockId oldBlock = GetBlockAt(hit.bx, hit.by, hit.bz);
    if (oldBlock == AIR_ID || oldBlock == UNKNOWN_ID)
        return;

    SetBlockAt(hit.bx, hit.by, hit.bz, AIR_ID);
    BroadcastBlockChange(hit.bx, hit.by, hit.bz, AIR_ID);

    int cx = game::WorldToChunkX(hit.bx);
    int cz = game::WorldToChunkZ(hit.bz);
    int lx = game::WorldToLocalX(hit.bx);
    int lz = game::WorldToLocalZ(hit.bz);
    auto it = m_chunks.find(game::ChunkKey(cx, cz));
    if (it != m_chunks.end())
        game::LightEngine::OnBlockChanged(m_registry, it->second, lx, hit.by, lz, oldBlock, AIR_ID);

    SpawnItemDrop(glm::vec3(hit.bx + 0.5f, hit.by + 0.5f, hit.bz + 0.5f), oldBlock);
}

void GameServer::HandlePlace(ServerEntity& playerEntity, BlockId type) {
    if (!playerEntity.active || !playerEntity.serverPlayer.has_value())
        return;

    if (m_registry.Get(type).id == UNKNOWN_ID)
        return;

    auto& p = *playerEntity.serverPlayer;

    int slotIndex = -1;
    for (int i = 0; i < 36; ++i) {
        if (p.inventory[i].type == type && p.inventory[i].count > 0) {
            slotIndex = i;
            break;
        }
    }
    if (slotIndex == -1)
        return;

    glm::vec3 front;
    front.x = cos(p.yaw) * cos(p.pitch);
    front.y = sin(p.pitch);
    front.z = sin(p.yaw) * cos(p.pitch);
    front = glm::normalize(front);

    glm::vec3 eye = playerEntity.transform.position + glm::vec3(0.0f, game::PLAYER_EYE, 0.0f);
    auto hit = game::RayCast(m_registry, eye, front, REACH_DISTANCE,
                             [this](int x, int y, int z) { return GetBlockAt(x, y, z); });
    if (!hit.hit)
        return;

    int px = hit.bx + hit.nx;
    int py = hit.by + hit.ny;
    int pz = hit.bz + hit.nz;
    if (IsInsidePlayer(playerEntity, px, py, pz))
        return;

    BlockId oldBlock = GetBlockAt(px, py, pz);
    if (oldBlock != AIR_ID)
        return;

    SetBlockAt(px, py, pz, type);
    BroadcastBlockChange(px, py, pz, type);

    int cx = game::WorldToChunkX(px);
    int cz = game::WorldToChunkZ(pz);
    int lx = game::WorldToLocalX(px);
    int lz = game::WorldToLocalZ(pz);
    auto it = m_chunks.find(game::ChunkKey(cx, cz));
    if (it != m_chunks.end())
        game::LightEngine::OnBlockChanged(m_registry, it->second, lx, py, lz, oldBlock, type);

    p.inventory[slotIndex].count--;
    if (p.inventory[slotIndex].count <= 0)
        p.inventory[slotIndex] = {};
    p.inventoryStateId++;
}

void GameServer::AddToInventory(ServerEntity& playerEntity, BlockId id, int count) {
    if (!playerEntity.active || !playerEntity.serverPlayer.has_value())
        return;

    if (id == AIR_ID || count <= 0)
        return;

    auto& inv = playerEntity.serverPlayer->inventory;

    for (int i = 0; i < 9; ++i) {
        if (inv[i].type == id && inv[i].count < 64) {
            int move = std::min(64 - inv[i].count, count);
            inv[i].count += move;
            count -= move;
            if (count <= 0)
                return;
        }
    }
    for (int i = 0; i < 9; ++i) {
        if (inv[i].isEmpty()) {
            inv[i].type = id;
            inv[i].count = count;
            return;
        }
    }
    for (int i = 9; i < 36; ++i) {
        if (inv[i].type == id && inv[i].count < 64) {
            int move = std::min(64 - inv[i].count, count);
            inv[i].count += move;
            count -= move;
            if (count <= 0)
                return;
        }
    }
    for (int i = 9; i < 36; ++i) {
        if (inv[i].isEmpty()) {
            inv[i].type = id;
            inv[i].count = count;
            return;
        }
    }
}

BlockId GameServer::GetBlockAt(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_SIZE_Y)
        return AIR_ID;
    auto it = m_chunks.find(ChunkKey(wx >> 4, wz >> 4));
    if (it == m_chunks.end())
        return AIR_ID;
    return ChunkGet(it->second, wx & 15, wy, wz & 15);
}

void GameServer::SetBlockAt(int wx, int wy, int wz, BlockId bt) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y)
        return;
    int cx = wx >> 4;
    int cz = wz >> 4;
    auto it = m_chunks.find(ChunkKey(cx, cz));
    if (it == m_chunks.end())
        return;
    ChunkSet(it->second, wx & 15, wy, wz & 15, bt);
    m_pendingChanges[ChunkKey(cx, cz)].push_back({wx, wy, wz, bt});
    m_dirty = true;
}

bool GameServer::IsInsidePlayer(ServerEntity& playerEntity, int bx, int by, int bz) {
    glm::vec3 pos = playerEntity.transform.position;
    glm::vec3 pmin{pos.x - PLAYER_HALF_W, pos.y, pos.z - PLAYER_HALF_W};
    glm::vec3 pmax{pos.x + PLAYER_HALF_W, pos.y + PLAYER_HEIGHT, pos.z + PLAYER_HALF_W};
    glm::vec3 bmin{static_cast<float>(bx), static_cast<float>(by), static_cast<float>(bz)};
    glm::vec3 bmax{bmin.x + 1.0f, bmin.y + 1.0f, bmin.z + 1.0f};
    return (pmin.x < bmax.x && pmax.x > bmin.x) && (pmin.y < bmax.y && pmax.y > bmin.y) &&
           (pmin.z < bmax.z && pmax.z > bmin.z);
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
                if (IsSolidAt(x, y, z))
                    return true;
    return false;
}

bool GameServer::IsSolidAt(int wx, int wy, int wz) const {
    return m_registry.IsSolid(GetBlockAt(wx, wy, wz));
}

void GameServer::BroadcastWorldState() {
    std::lock_guard<std::mutex> lock(m_worldMutex);
    Eng::tool::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::WorldState));

    uint32_t count = 0;
    for (auto& e : m_ecsWorld.GetEntities())
        if (e.active && e.serverPlayer)
            count++;
    w.WriteU32BE(count);
    w.Write<float>(m_lastTickMs);

    for (auto& e : m_ecsWorld.GetEntities()) {
        if (!e.active || !e.serverPlayer)
            continue;

        // 上一行的判定已保证 optional 有值；clang-tidy 的 optional 追踪不跨语句，
        // 看不到这条事实，所以这里显式解引用一次并缓存成引用。
        const ServerPlayerComponent& sp = *e.serverPlayer;  // NOLINT(bugprone-unchecked-optional-access)

        w.WriteU32BE(sp.clientId);
        uint8_t nlen = static_cast<uint8_t>(std::min<size_t>(sp.name.size(), 32));
        w.Write<uint8_t>(nlen);
        for (uint8_t i = 0; i < nlen; ++i)
            w.Write<uint8_t>((uint8_t)sp.name[i]);
        w.WriteVec3(e.transform.position);
        w.Write(sp.yaw);
        w.Write(sp.pitch);
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

void GameServer::HandleClickContainer(int clientId, Eng::tool::MessageReader& r) {
    int32_t stateId = r.Read<int32_t>();
    int16_t slot = r.Read<int16_t>();
    uint8_t button = r.Read<uint8_t>();
    uint8_t mode = r.Read<uint8_t>();

    bool needResync = false;

    // 把要生成的掉落物先记录下来，出锁之后再真正生成
    struct DropInfo {
        glm::vec3 pos;
        BlockId type;
        int count;
    };
    std::vector<DropInfo> dropsToSpawn;

    {
        // 锁范围只包住数据读写
        std::lock_guard<std::mutex> lock(m_worldMutex);
        auto* e = GetEntityByNetId((uint32_t)clientId);
        if (!e || !e->serverPlayer)
            return;
        auto& p = *e->serverPlayer;

        if (stateId != p.inventoryStateId) {
            needResync = true;
        } else {
            if (mode == 4) {
                ItemStack* targetStack = nullptr;
                if (slot == -1)
                    targetStack = &p.heldItem;
                else if (slot >= 0 && slot < 36)
                    targetStack = &p.inventory[slot];

                if (targetStack && !targetStack->isEmpty()) {
                    int dropCount = (button == 1) ? 1 : targetStack->count;
                    dropCount = std::min(dropCount, targetStack->count);

                    // 记录掉落物信息，不在这里直接调用 SpawnItemDrop
                    dropsToSpawn.push_back(
                        {e->transform.position + glm::vec3(0, 1.5f, 0), targetStack->type, dropCount});

                    targetStack->count -= dropCount;
                    if (targetStack->count <= 0)
                        *targetStack = {};
                }
            } else if (mode == 0 && slot >= 0 && slot < 36) {
                auto& inv = p.inventory[slot];
                auto& held = p.heldItem;

                if (button == 0) {
                    if (held.isEmpty()) {
                        held = inv;
                        inv = {};
                    } else if (inv.isEmpty()) {
                        inv = held;
                        held = {};
                    } else if (inv.type == held.type && inv.count < 64) {
                        int move = std::min(64 - inv.count, held.count);
                        inv.count += move;
                        held.count -= move;
                        if (held.count <= 0)
                            held = {};
                    } else
                        std::swap(inv, held);
                } else if (button == 1) {
                    if (held.isEmpty()) {
                        int half = (inv.count + 1) / 2;
                        held = inv;
                        held.count = half;
                        inv.count -= half;
                        if (inv.count <= 0)
                            inv = {};
                    } else if (inv.isEmpty()) {
                        int half = (held.count + 1) / 2;
                        inv = held;
                        inv.count = half;
                        held.count -= half;
                        if (held.count <= 0)
                            held = {};
                    } else if (inv.type == held.type && inv.count < 64) {
                        inv.count++;
                        held.count--;
                        if (held.count <= 0)
                            held = {};
                    }
                }
            }
            p.inventoryStateId++;
        }
    }

    if (needResync) {
        SendInventoryUpdate(clientId);
        return;
    }

    // 出锁之后，再安全地生成掉落物（此时 SpawnItemDrop 里的加锁和广播都不会死锁）
    for (const auto& drop : dropsToSpawn) {
        for (int i = 0; i < drop.count; ++i) {
            SpawnItemDrop(drop.pos, drop.type);
        }
    }

    // 出锁之后，再发送背包同步
    SendInventoryUpdate(clientId);
}

void GameServer::SendInventoryUpdate(int clientId) {
    Eng::tool::MessageWriter w;
    {
        // 只在读取背包数据时加锁
        std::lock_guard<std::mutex> lock(m_worldMutex);
        auto* e = GetEntityByNetId((uint32_t)clientId);
        if (!e || !e->serverPlayer)
            return;
        auto& p = *e->serverPlayer;

        w.Write(static_cast<uint8_t>(game::net::MessageType::InventoryUpdate));
        w.Write<int32_t>(p.inventoryStateId);
        for (const auto& item : p.inventory) {
            w.Write<uint16_t>(item.type);
            w.Write<int32_t>(item.count);
        }
        w.Write<uint16_t>(p.heldItem.type);
        w.Write<int32_t>(p.heldItem.count);
    }  // 锁在这里提前释放

    // 无锁状态下发送网络包
    m_server->SendTo(clientId, w.GetBuffer());
}

}  // namespace game::server