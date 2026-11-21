#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "Engine/ecs/ISystem.h"

namespace Eng::ecs {

// ============================================================
// 线性调度器：按注册顺序依次 Update，不做依赖图、不做分组、不做并行。
//
// 之所以只做到这一步：现在的执行顺序是隐式写在 Game.cpp 里的三条语句，
// 顺序本身就是语义（先拾取再物理，还是先物理再拾取，结果不同）。
// 显式注册只是把这条隐式规则挪到一个看得见的地方，不改变它。
//
// 系统对象由调度器独占持有，业务拿到的是引用，生命周期与调度器一致。
// 调度器不是线程安全的，必须与它驱动的世界在同一线程使用。
// ============================================================
template <typename WorldT, typename TickContextT>
class SystemScheduler {
public:
    using SystemT = ISystemT<WorldT, TickContextT>;

    SystemScheduler() = default;
    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;

    // 注册一个系统，返回它的引用（需要在外部配置状态时用）。
    template <typename T, typename... Args>
    T& Add(Args&&... args) {
        static_assert(std::is_base_of_v<SystemT, T>,
                      "System must derive from Eng::ecs::ISystemT<WorldT, TickContextT>");

        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *owned;
        m_systems.push_back(std::move(owned));
        return ref;
    }

    void UpdateAll(WorldT& world, const TickContextT& ctx) {
        for (const auto& system : m_systems) {
            system->Update(world, ctx);
        }
    }

    void Clear() noexcept { m_systems.clear(); }

    [[nodiscard]] std::size_t Size() const noexcept { return m_systems.size(); }
    [[nodiscard]] bool Empty() const noexcept { return m_systems.empty(); }

private:
    std::vector<std::unique_ptr<SystemT>> m_systems;
};

}  // namespace Eng::ecs