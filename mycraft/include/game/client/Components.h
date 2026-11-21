#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>

#include "game/client/ModelLoader.h"
#include "game/core/BlockRegistry.h"

namespace game {
struct Chunk;
}

namespace game::client {

// ============================================================
// 客户端专属组件。服务端**看不到这个文件**，所以这里可以放心
// 持有 LoadedModel* / shared_ptr<Chunk> —— 渲染依赖到这一层为止。
// ============================================================

// 本地玩家：平滑插值、预测误差等
struct PlayerComponent {
    glm::vec3 velocity{0.0f};
    bool onGround = false;
    bool flyMode = true;
    float jumpHoldTimer = 0.0f;
    glm::vec3 positionError{0.0f};
};

// 从 ModelLoader.h 搬到这里，全项目只保留这一份定义。
struct RenderableComponent {
    // 如果是猪，model 不为空
    LoadedModel* model = nullptr;
    std::string currentClip = "Walk";
    float animTime = 0.0f;

    // 如果是掉落物，blockType 不为 AIR_ID
    BlockId blockType = AIR_ID;
};

// 区块坐标组件
struct ChunkCoordComponent {
    int cx = 0;
    int cz = 0;
};

// 区块数据组件（持有数据与 Mesh 版本）
struct ChunkComponent {
    std::shared_ptr<Chunk> data = nullptr;  // 共享区块数据
    uint32_t meshVersion = 0;               // 用于校验异步建网格时的版本冲突
    bool isMeshDirty = false;               // 是否需要重新建网格
};

}  // namespace game::client