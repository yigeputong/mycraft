# AGENTS.md - Mycraft 引擎 AI 代理开发守则

## 🛠️ 构建与测试

- 构建 Debug: `cmake --build build/clang --config Debug`
- 构建 Release: `cmake --build build/clang --config Release`
- 运行测试: （暂无自动化测试，需手动运行 `build/clang/Debug/mycraft.exe`）

## 🚨 绝对禁止事项（红线）

1. **禁止触碰第三方库**：绝不允许修改 `Engine/lib/imgui`、`vcpkg_installed` 中的任何代码。
2. **禁止引入新依赖**：如需新库，必须先向人类申请。
3. **禁止使用裸指针**：业务层和引擎层严禁出现 `new/delete`、`malloc/free`，必须使用 `std::unique_ptr` 或 `std::shared_ptr`。
4. **禁止写 C 风格接口**：数据传输必须用 `std::span`，字符串用 `std::string_view`。
5. **禁止跨平台代码泄漏**：所有包含 `<windows.h>` 的代码必须锁死在 `Platform.cpp`，业务层禁止出现 `#ifdef _WIN32`。

## 🏗️ 架构约定

- **Service 层** (`*Service`)：生命周期同 Engine，被动调用。
- **Manager 层** (`*Manager`)：生命周期同 Game，主动运行（有 Update 或后台线程）。
- **Tool 层** (无后缀)：纯工具，静态单例或栈对象。
- **渲染收口**：所有地形和静态几何体的 MDI 提交都在 `SceneRenderer` 中，禁止在 `MyGame` 里手搓 VBO/IBO。

## 🔍 代码修改流程

1. 修改底层逻辑前，**必须**先阅读 `AI_CONTEXT.md` 确认架构不冲突。
2. 修改完核心逻辑后，必须编译 Debug 版本确保 0 警告。
3. 如果修改了多线程、显存分配相关代码，必须在提交前提醒人类在 Release 模式下测试。

## 🪵 调试与日志规范

- 统一使用 `Eng::tool::Log`，宏定义如 `logInfo(m_logger, "[Tag] Value: " << val)`。
- 不允许使用 `printf` 或 `std::cout` 进行常规调试（除非排查死锁级别的极端 Bug）。
