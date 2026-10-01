#include "core/Log.h"
#include "core/MessageReader.h"
#include "core/MessageWriter.h"
#include "core/NetworkServer.h"
#include "game/GameServer.h"
#include "game/Protocol.h"
#include "game/server/WorldGenerator.h"
#include "game/core/RayCast.h"
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
    m_server.Shutdown();
    if (m_thread.joinable()) m_thread.join();
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
            TickWorld(TICK_INTERVAL);
            BroadcastWorldState();
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

            for (auto& p : m_players) {
                if (p.id == (uint32_t)clientId) {
                    p.moveDir = in.moveDir;     // 存世界方向
                    p.yaw     = in.look.x;
                    p.pitch   = in.look.y;
                    if (in.dig)   HandleDig(p);
                    if (in.place) HandlePlace(p);
                    break;
                }
            }
            break;
        }
        case game::net::MessageType::ChunkRequest: {
            int cx = r.Read<int>();
            int cz = r.Read<int>();

            Chunk& chunk = GetOrCreateChunk(cx, cz);

            Eng::MessageWriter w;
            w.Write(static_cast<uint8_t>(game::net::MessageType::ChunkData));
            w.Write<uint32_t>(cx);
            w.Write<uint32_t>(cz);
            w.Write<uint16_t>(CHUNK_SIZE_Y);
            w.WriteBytes(reinterpret_cast<const uint8_t*>(chunk.blocks.data()),
                        chunk.blocks.size() * sizeof(BlockType));

            m_server.SendTo(clientId, w.GetBuffer());
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
            return;
        }
    }
}

void GameServer::TickWorld(float dt) {
    for (auto& p : m_players) {
        if (glm::length(p.moveDir) > 0.001f) {
            glm::vec3 delta = glm::normalize(p.moveDir);
            p.position += delta * m_moveSpeed * dt;
        }
    }
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

    int stoneCount = 0, grassCount = 0, dirtCount = 0,
        sandCount = 0, airCount = 0, waterCount = 0;
    for (BlockType b : chunk.blocks) {
        switch (b) {
            case BlockType::Stone:      stoneCount++; break;
            case BlockType::GrassBlock: grassCount++; break;
            case BlockType::Dirt:       dirtCount++;  break;
            case BlockType::Sand:       sandCount++;  break;
            case BlockType::Air:        airCount++;   break;
            case BlockType::Water:      waterCount++; break;
            default: break;
        }
    }
    logInfo(m_logger, "[World] chunk(" << cx << "," << cz << ") Stone=" << stoneCount
               << " Grass=" << grassCount
               << " Water=" << waterCount
               << " Air=" << airCount
               << " Dirt=" << dirtCount
               << " Sand=" << sandCount);

    return chunk;
}

void GameServer::HandleDig(ServerPlayer& p) {
    glm::vec3 front;
    front.x = cos(p.yaw) * cos(p.pitch);
    front.y = sin(p.pitch);
    front.z = sin(p.yaw) * cos(p.pitch);
    front = glm::normalize(front);

    auto hit = RayCast(p.position, front, REACH_DISTANCE,
        [this](int x, int y, int z) { return GetBlockAt(x, y, z); });
    if (!hit.hit) return;

    SetBlockAt(hit.bx, hit.by, hit.bz, BlockType::Air);
    BroadcastBlockChange(hit.bx, hit.by, hit.bz, BlockType::Air);
}

void GameServer::HandlePlace(ServerPlayer& p) {
    glm::vec3 front;
    front.x = cos(p.yaw) * cos(p.pitch);
    front.y = sin(p.pitch);
    front.z = sin(p.yaw) * cos(p.pitch);
    front = glm::normalize(front);

    auto hit = RayCast(p.position, front, REACH_DISTANCE,
        [this](int x, int y, int z) { return GetBlockAt(x, y, z); });
    if (!hit.hit) return;

    // 放在命中面的外侧
    int px = hit.bx + hit.nx;
    int py = hit.by + hit.ny;
    int pz = hit.bz + hit.nz;

    // 别把方块放进自己身体里
    if (IsInsidePlayer(p, px, py, pz)) return;

    SetBlockAt(px, py, pz, BlockType::Stone);   // 先都放石头
    BroadcastBlockChange(px, py, pz, BlockType::Stone);
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
    // 玩家 AABB：以 position 为脚底中心
    constexpr float HALF_W = 0.3f;   // 半宽
    constexpr float HEIGHT = 1.8f;

    glm::vec3 pmin{p.position.x - HALF_W, p.position.y,        p.position.z - HALF_W};
    glm::vec3 pmax{p.position.x + HALF_W, p.position.y + HEIGHT, p.position.z + HALF_W};

    // 方块 AABB：[bx, bx+1] × [by, by+1] × [bz, bz+1]
    glm::vec3 bmin{(float)bx, (float)by, (float)bz};
    glm::vec3 bmax{bmin.x + 1.0f, bmin.y + 1.0f, bmin.z + 1.0f};

    // 三个轴都重叠才算相交
    bool overlapX = pmin.x < bmax.x && pmax.x > bmin.x;
    bool overlapY = pmin.y < bmax.y && pmax.y > bmin.y;
    bool overlapZ = pmin.z < bmax.z && pmax.z > bmin.z;

    return overlapX && overlapY && overlapZ;
}

void GameServer::BroadcastWorldState() {
    Eng::MessageWriter w;
    w.Write(static_cast<uint8_t>(game::net::MessageType::WorldState));
    w.WriteU32BE((uint32_t)m_players.size());

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