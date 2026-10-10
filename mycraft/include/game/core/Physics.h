#pragma once
#include <functional>
#include <cmath>
#include <glm/glm.hpp>

namespace game {

// ===== 玩家体型 =====
constexpr float PLAYER_WIDTH   = 0.6f;
constexpr float PLAYER_HEIGHT  = 1.8f;
constexpr float PLAYER_HALF_W  = PLAYER_WIDTH * 0.5f;
constexpr float PLAYER_EYE     = 1.62f;

// ===== 玩家运动 =====
constexpr float WALK_SPEED  = 4.3f;
constexpr float JUMP_SPEED  = 9.0f;
constexpr float GRAVITY     = 32.0f;
constexpr float TERMINAL_VELOCITY = 78.4f;
constexpr float FLY_SPEED   = 15.0f;    // ★ 新增

// ===== 掉落物物理 =====
constexpr float POINT_HALF     = 0.15f;   // 半高（用于底部碰撞检测）
constexpr float POINT_GRAVITY  = 20.0f;
constexpr float POINT_FRICTION = 0.8f;

// ===== 通用查询 =====
// 查询方块是否实心（由调用方注入：服务端查 m_chunks，客户端查已加载区块）
using SolidQuery = std::function<bool(int, int, int)>;

// ============================================================
// 玩家物理
// ============================================================
struct PlayerMotion {
    glm::vec3 position{0.0f};   // 脚底
    glm::vec3 velocity{0.0f};
    bool      onGround = false;
    bool      flyMode  = false;   // ★ 飞行模式纳入
};

// 步进一次物理。客户端每帧调、服务端每 tick 调，逻辑完全一致。
// flyMode = true 时：
//   - 忽略重力和 AABB 碰撞
//   - moveDir.y 有效（上下飞）
//   - 位置直接位移
inline void StepPlayer(PlayerMotion& m,
                       glm::vec3 moveDir,
                       bool jump,
                       float dt,
                       const SolidQuery& isSolid)
{
    // ---- 飞行 ----
    if (m.flyMode) {
        // 全轴方向都有效（上下也能飞）
        if (glm::length(moveDir) > 0.001f)
            moveDir = glm::normalize(moveDir);
        m.position += moveDir * FLY_SPEED * dt;
        m.velocity = glm::vec3(0.0f);
        m.onGround = false;
        return;
    }

    // ---- 地面物理 ----
    auto collides = [&](const glm::vec3& pos) {
        int minX = (int)std::floor(pos.x - PLAYER_HALF_W);
        int maxX = (int)std::floor(pos.x + PLAYER_HALF_W);
        int minY = (int)std::floor(pos.y);
        int maxY = (int)std::floor(pos.y + PLAYER_HEIGHT);
        int minZ = (int)std::floor(pos.z - PLAYER_HALF_W);
        int maxZ = (int)std::floor(pos.z + PLAYER_HALF_W);

        for (int x = minX; x <= maxX; ++x)
            for (int y = minY; y <= maxY; ++y)
                for (int z = minZ; z <= maxZ; ++z)
                    if (isSolid(x, y, z)) return true;
        return false;
    };

    // 1. 水平速度
    glm::vec3 horiz = moveDir;
    horiz.y = 0.0f;
    if (glm::length(horiz) > 0.001f) horiz = glm::normalize(horiz);
    m.velocity.x = horiz.x * WALK_SPEED;
    m.velocity.z = horiz.z * WALK_SPEED;

    // 2. 起跳
    if (jump && m.onGround) {
        m.velocity.y = JUMP_SPEED;
        m.onGround = false;
    }

    // 3. 分轴移动
    m.position.x += m.velocity.x * dt;
    if (collides(m.position)) {
        m.position.x -= m.velocity.x * dt;
        m.velocity.x = 0;
    }
    m.position.z += m.velocity.z * dt;
    if (collides(m.position)) {
        m.position.z -= m.velocity.z * dt;
        m.velocity.z = 0;
    }
    m.position.y += m.velocity.y * dt;
    if (collides(m.position)) {
        m.position.y -= m.velocity.y * dt;
        if (m.velocity.y < 0) m.onGround = true;
        m.velocity.y = 0;
    } else {
        if (m.velocity.y < 0) m.onGround = false;
    }

    // 4. 重力
    m.velocity.y -= GRAVITY * dt;
    if (m.velocity.y < -TERMINAL_VELOCITY)
        m.velocity.y = -TERMINAL_VELOCITY;
}

// ============================================================
// 掉落物物理（点实体，带半高 0.15）
// ============================================================
struct PointEntity {
    glm::vec3 position{0.0f};   // 方块中心
    glm::vec3 velocity{0.0f};
};

// 步进一次物理：
//   - 施加重力
//   - 位移 + 地面碰撞
//   - 落地时保留水平速度 20% 衰减
//
// 计时器（pickupDelay / age）由调用方管理，不属于物理。
inline void StepPointEntity(PointEntity& e, float dt,
                            const SolidQuery& isSolid)
{
    // 重力
    e.velocity.y -= POINT_GRAVITY * dt;

    // 位移
    glm::vec3 next = e.position + e.velocity * dt;

    // 地面碰撞：检查底部所在方块
    int solidY = static_cast<int>(std::floor(next.y - POINT_HALF));
    if (isSolid(static_cast<int>(std::floor(next.x)),
                solidY,
                static_cast<int>(std::floor(next.z)))) {
        next.y = static_cast<float>(solidY) + 1.0f + POINT_HALF;
        e.velocity.y = 0.0f;
        e.velocity.x *= POINT_FRICTION;
        e.velocity.z *= POINT_FRICTION;
    }

    e.position = next;
}

} // namespace game