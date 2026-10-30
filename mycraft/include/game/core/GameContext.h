#pragma once
namespace game {
class BlockRegistry;

// Game 与 GameServer 各自持有一份 BlockRegistry 实体，GameContext 绑定其引用。
// 生命周期：BlockRegistry 必须先于所有 GameContext 引用方构造，后于其析构。
struct GameContext {
    BlockRegistry& registry;
};
} // namespace game