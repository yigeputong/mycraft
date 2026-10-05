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

namespace game::server {

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
            m_players.erase(std::remove_if(m_players.begin(), m_players.end(),
                [id](const ServerPlayer& p) { return p.id == (uint32_t)id; }), m_players.end());
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
    constexpr float TICK_INTERVAL = 1.0f / TPS;   // 20 TPS
    auto last = std::chrono::steady_clock::now();
    float accumulator = 0.0f;

    logInfo(m_logger, "[Server] Server Run");
    uint32_t seed = static_cast<uint32_t>(std::time(nullptr)) ^ static_cast<uint32_t>(std::clock());
    m_terrain = TerrainGenerator(seed);
    logInfo(m_logger, "[Server] World Seed = " << seed);

    while (m_running.load()) {
        // 1. 网络收发
        m_server.PollAccept();
        m_server.Update();

        // 2. 累积时间
        auto now = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(now - last).count();
        last = now; // 每帧都更新
        accumulator += frameTime;

        // 3. 固定步长 tick
        while (accumulator >= TICK_INTERVAL) {

            auto tickStart = std::chrono::steady_clock::now();

            constexpr int kChunksPerTick = 4;
            for (int i = 0; i < kChunksPerTick && !m_chunkQueue.empty(); ++i) {
                auto req = m_chunkQueue.front();
                m_chunkQueue.pop();
                Chunk& chunk = GetOrCreateChunk(req.cx, req.cz);
                Eng::MessageWriter w;
                w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkData));
                w.Write<int32_t>(req.cx);
                w.Write<int32_t>(req.cz);
                w.Write<uint16_t>(CHUNK_SIZE_Y);
                w.WriteBytes(reinterpret_cast<const uint8_t*>(chunk.blocks.data()),
                            chunk.blocks.size() * sizeof(BlockType));
                m_server.SendTo(req.clientId, w.GetBuffer());
            }

            TickWorld(TICK_INTERVAL);
            TickEntities(TICK_INTERVAL);
            BroadcastWorldState();

            auto tickEnd = std::chrono::steady_clock::now();
            m_lastTickMs = std::chrono::duration<float, std::milli>(tickEnd - tickStart).count();

            accumulator -= TICK_INTERVAL;
        }

        SDL_Delay(1);
    }
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

            for (auto& p : m_players) {
                if (p.id == (uint32_t)clientId) {
                    p.moveDir = in.moveDir;     // 存世界方向
                    p.yaw     = in.look.x;
                    p.pitch   = in.look.y;
                    p.jump    = in.jump;
                    if (in.dig)   HandleDig(p);
                    if (in.place) HandlePlace(p, static_cast<BlockType>(in.placeBlock));
                    break;
                }
            }
            break;
        }
        case game::net::MessageType::ChunkRequest: {
            int cx = r.Read<int>();
            int cz = r.Read<int>();

            m_chunkQueue.push({clientId, cx, cz});

            // Chunk& chunk = GetOrCreateChunk(cx, cz);

            // Eng::MessageWriter w;
            // w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkData));
            // w.Write<uint32_t>(cx);
            // w.Write<uint32_t>(cz);
            // w.Write<uint16_t>(CHUNK_SIZE_Y);
            // w.WriteBytes(reinterpret_cast<const uint8_t*>(chunk.blocks.data()),
            //             chunk.blocks.size() * sizeof(BlockType));

            // m_server.SendTo(clientId, w.GetBuffer());
            break;
        }
    }
}

void GameServer::ApplyPlayerInput(int clientId, const net::PlayerInput& in) {
    for (auto& p : m_players) {
        if (p.id == (uint32_t)clientId) {
            p.moveDir = in.moveDir;
            p.yaw     = in.look.x;
            p.pitch   = in.look.y;
            p.jump    = in.jump;
            return;
        }
    }
}

void GameServer::TickWorld(float dt) {
    for (auto& p : m_players) {
        PlayerMotion me;
        me.position = p.position;
        me.velocity = p.velocity;
        me.onGround = p.onGround;

        game::StepPlayer(me, p.moveDir, p.jump, dt,
            [this](int x, int y, int z) { return IsSolidAt(x, y, z); });

        p.position = me.position;
        p.velocity = me.velocity;
        p.onGround = me.onGround;
        p.jump = false;
    }
}

void GameServer::SpawnItemDrop(const glm::vec3& pos, BlockType type) {
    ItemEntity e;
    e.id          = m_nextEntityId++;
    e.pos         = pos;
    e.vel         = glm::vec3(
        ((rand() % 100) / 100.0f - 0.5f) * 2.0f,
        2.5f,
        ((rand() % 100) / 100.0f - 0.5f) * 2.0f);
    e.pickupDelay = 0.5f;
    e.itemType    = type;

    m_entities[e.id] = e;

    // 广播给所有客户端
    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::EntitySpawn));
    w.Write<uint32_t>(e.id);
    w.WriteVec3(e.pos);
    w.Write<uint16_t>(static_cast<uint16_t>(type));
    m_server.Broadcast(w.GetBuffer());

    logDebug(m_logger, "[Server] Spawn item drop id=" << e.id
        << " type=" << (int)type);
}

void GameServer::TickEntities(float dt) {
    std::vector<uint32_t> toRemove;

    for (auto& [id, e] : m_entities) {
        // 重力
        e.vel.y -= 20.0f * dt;

        // 简单移动 + 地面碰撞
        glm::vec3 next = e.pos + e.vel * dt;

        if (IsSolidAt(static_cast<int>(std::floor(next.x)),
                      static_cast<int>(std::floor(next.y - 0.15f)),
                      static_cast<int>(std::floor(next.z)))) {
            next.y = std::floor(next.y) + 1.0f + 0.15f;
            e.vel.y = 0.0f;
            e.vel.x *= 0.8f;   // 地面摩擦
            e.vel.z *= 0.8f;
        }
        e.pos = next;

        if (e.pickupDelay > 0) e.pickupDelay -= dt;
        e.age += dt;

        // 超时（5 分钟）→ 消失
        if (e.age > 300.0f) {
            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::EntityDestroy));
            w.Write<uint32_t>(id);
            w.Write<uint8_t>(0);   // reason=0 超时
            m_server.Broadcast(w.GetBuffer());
            toRemove.push_back(id);
            continue;
        }

        // 拾取检测
        if (e.pickupDelay <= 0.0f) {
            for (auto& p : m_players) {
                glm::vec3 center = p.position + glm::vec3(0.0f, 0.9f, 0.0f);
                float d2 = glm::distance(e.pos, center);
                if (d2 < 1.5f * 1.5f) {
                    Eng::MessageWriter w;
                    w.Write(static_cast<uint8_t>(game::net::MessageType::EntityDestroy));
                    w.Write<uint32_t>(id);
                    w.Write<uint8_t>(1);   // reason=1 被捡走
                    m_server.SendTo(p.id, w.GetBuffer());
                    toRemove.push_back(id);
                    logDebug(m_logger, "[Server] Player " << p.id
                        << " picked up id=" << id);
                    break;
                }
            }
        }
    }

    for (uint32_t id : toRemove) m_entities.erase(id);
}

Chunk& GameServer::GetOrCreateChunk(int cx, int cz) {
    const uint64_t key = ChunkKey(cx, cz);

    auto it = m_chunks.find(key);
    if (it != m_chunks.end()) {
        return it->second;   // 命中缓存，直接返回
    }

    // 未命中，生成一份
    it = m_chunks.emplace(key, m_terrain.GenerateChunk(cx, cz)).first;
    Chunk& chunk = it->second;

    return chunk;
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