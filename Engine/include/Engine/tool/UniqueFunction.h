#pragma once
#include <memory>
#include <utility>
#include <type_traits>

namespace Eng::tool {

/**
 * @brief 仅支持移动的函数包装器 (对标 std::move_only_function)
 * @details 专为跨平台并发设计，无拷贝开销，完美支持 std::unique_ptr 捕获。
 */
class UniqueFunction {
public:
    UniqueFunction() = default;
    UniqueFunction(std::nullptr_t) noexcept {}

    // 接收任意函数对象、Lambda、函数指针
    template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, UniqueFunction>>>
    UniqueFunction(F&& f) 
        : m_impl(std::make_unique<Impl<std::decay_t<F>>>(std::forward<F>(f))) {}

    // 只允许移动
    UniqueFunction(UniqueFunction&&) noexcept = default;
    UniqueFunction& operator=(UniqueFunction&&) noexcept = default;
    UniqueFunction(const UniqueFunction&) = delete;
    UniqueFunction& operator=(const UniqueFunction&) = delete;

    void operator()() {
        if (m_impl) m_impl->Call();
    }

    explicit operator bool() const noexcept { return m_impl != nullptr; }

private:
    struct Base {
        virtual ~Base() = default;
        virtual void Call() = 0;
    };

    template <typename F>
    struct Impl final : Base {
        F m_f;
        explicit Impl(F&& f) : m_f(std::move(f)) {}
        void Call() override { m_f(); }
    };

    std::unique_ptr<Base> m_impl;
};

} // namespace Eng::tool