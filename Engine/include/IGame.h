#pragma once

namespace Eng {

class Engine;

class IGame {
public:
    virtual ~IGame() = default;

    // 加载资源、初始化场景、创建实体
    virtual void OnStart(Engine& engine) = 0;

    virtual void RunServer(Engine& engine) = 0;
    virtual void RunClient(Engine& engine) = 0;

    // 每帧更新：游戏逻辑、物理、AI、输入处理，返回 ture 退出
    virtual bool OnUpdate(Engine& engine, float deltaTime) = 0;

    // 每帧渲染：发出绘制命令（Renderer 执行）
    virtual void OnRender(Engine& engine) = 0;

    // 清理游戏特定资源（网格、纹理等）
    virtual void OnShutdown(Engine& engine) = 0;
};
    
} // namespace Eng
