# AGENTS.md - Mycraft 引擎 AI 代理开发守则

## 🛠️ 构建与测试

- 构建 Release: `cmake --build build/clang --config Release`
- 运行测试: 需手动运行 `build/clang/Release/mycraft.exe`

## 🚨 绝对禁止事项（红线）

1. **禁止触碰第三方库**：`Engine/lib/imgui`、`vcpkg_installed` 严禁修改。
2. **禁止使用裸指针/宏**：禁止 `new/delete`，禁止面向业务的 C 风格接口。
3. **禁止跨平台代码泄漏**：所有平台特定代码锁死 `Platform.cpp`。
4. **禁止 RHI 带脑子**：严禁在 `IRenderService` 中添加带有业务语义的接口（如“上传全局几何体”），渲染状态的维护必须留在 `SceneRenderer`。

## 🏗️ 架构约定

- **Service**：同 Engine 寿命，被动调用（如 `IRenderService`）。
- **Manager**：同 Game 寿命，主动运行（如 `MeshManager`）。
- **Tool**：无后缀，纯工具（如 `Log`, `RAIIThread`）。

## 🔍 代码修改流程

1. 修改底层逻辑前，必须阅读 `AI_CONTEXT.md` 确认架构。
2. 修改完必须编译 Release 版本，要求 0 Error。如果出现 `[[deprecated]]` 警告，应作为重构提示予以消除。
3. 涉及多线程、显存分配时，必须在 Release 模式下实测。

## 🪵 调试与日志规范

- 统一使用 `Eng::tool::Log`，宏定义如 `logInfo(m_logger, "[Tag] Value: " << val)`。
- 严禁 `printf` / `std::cout`。
