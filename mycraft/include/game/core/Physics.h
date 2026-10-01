#pragma once
#include <functional>
#include <glm/glm.hpp>

namespace game {

// ===== 玩家体型 =====
constexpr float PLAYER_WIDTH   = 0.6f;    // AABB 宽（X/Z 同宽）
constexpr float PLAYER_HEIGHT  = 1.8f;    // AABB 高
constexpr float PLAYER_HALF_W  = PLAYER_WIDTH * 0.5f;
constexpr float PLAYER_EYE     = 1.62f;   // 眼睛离脚底高度

// ===== 运动 =====
constexpr float WALK_SPEED  = 4.3f;   // 水平速度 m/s
constexpr float JUMP_SPEED  = 9.0f;   // 起跳初速 m/s（能跳 1.1 格）
constexpr float GRAVITY     = 32.0f;  // 重力加速度 m/s²

// ===== 其他 =====
constexpr float TERMINAL_VELOCITY = 78.4f;   // 下落终端速度

struct PlayerMotion {
    glm::vec3 position{0.0f};   // 脚底
    glm::vec3 velocity{0.0f};
    bool      onGround = false;
};

// 查询方块是否实心（由调用方注入）
using SolidQuery = std::function<bool(int, int, int)>;

// 步进一次物理。客户端每帧调、服务端每 tick 调，逻辑完全一致。
inline void StepPlayer(PlayerMotion& m,
                       glm::vec3 moveDir,   // 水平意图（y 会被忽略）
                       bool jump,
                       float dt,
                       const SolidQuery& isSolid)
{
    // ---- AABB 碰撞（内部工具） ----
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

    // ---- 1. 水平速度 ----
    glm::vec3 horiz = moveDir;
    horiz.y = 0.0f;
    if (glm::length(horiz) > 0.001f) horiz = glm::normalize(horiz);
    m.velocity.x = horiz.x * WALK_SPEED;
    m.velocity.z = horiz.z * WALK_SPEED;

    // ---- 2. 起跳 ----
    if (jump && m.onGround) {
        m.velocity.y = JUMP_SPEED;
        m.onGround = false;
    }

    // ---- 3. 分轴移动 ----
    // X
    m.position.x += m.velocity.x * dt;
    if (collides(m.position)) {
        m.position.x -= m.velocity.x * dt;
        m.velocity.x = 0;
    }
    // Z
    m.position.z += m.velocity.z * dt;
    if (collides(m.position)) {
        m.position.z -= m.velocity.z * dt;
        m.velocity.z = 0;
    }
    // Y
    m.position.y += m.velocity.y * dt;
    if (collides(m.position)) {
        m.position.y -= m.velocity.y * dt;
        if (m.velocity.y < 0) m.onGround = true;
        m.velocity.y = 0;
    } else {
        if (m.velocity.y < 0) m.onGround = false;
    }

    // ---- 4. 重力（先移动后重力） ----
    m.velocity.y -= GRAVITY * dt;
    if (m.velocity.y < -TERMINAL_VELOCITY)
        m.velocity.y = -TERMINAL_VELOCITY;
}

}