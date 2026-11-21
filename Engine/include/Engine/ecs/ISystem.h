#pragma once

namespace Eng::ecs {

// ============================================================
// 系统基类。两个模板参数把「世界长什么样」和「tick 上下文长什么样」
// 全部交给业务决定，引擎不认识任何组件，也不认识渲染。
//
// 注意这里**没有** Render。渲染不是 tick 的一部分：它有自己的
// 上下文（相机、着色器、图元批处理），也有自己的调用时机。
// 把 Render 塞进系统基类，等于让引擎 ECS 依赖渲染后端 —— 那样
// 服务端就永远裁不掉渲染代码。
// ============================================================
template <typename WorldT, typename TickContextT>
class ISystemT {
public:
    virtual ~ISystemT() = default;

    virtual void Update(WorldT& world, const TickContextT& ctx) = 0;
};

}  // namespace Eng::ecs