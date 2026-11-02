# Mycraft 引擎开发 — AI 上下文恢复与架构规范

> **本文档是 AI 开发协作的上下文恢复入口。**
> 每次开始新对话时，AI 必须完整阅读本文档以恢复项目状态。
> 文档包含：项目概况、铁律、当前架构、排坑指南、进度跟踪、AI 协作约定。

---

## 一、 项目概况与铁律（⚠️ 必读）

**Mycraft**：Minecraft 风格体素游戏 + 自制体素专用引擎。
**技术栈**：C++23 + CMake + Ninja + vcpkg。编译器主力为 `clang-cl`。
**图形库**：SDL3、volk、Vulkan 1.3 (Dynamic Rendering)、OpenGL 4.6 (DSA)、glm、ImGui、nlohmann_json、VMA。

### 🚨 铁律 1：现代 C++ 与系统交互规范

- **内存与数据**：严禁裸指针（`new/delete`），必须用 `std::unique_ptr` / `std::shared_ptr`。数据传输用 `std::span`，字符串用 `std::string_view`。
- **字符串格式化**：全面使用 `std::format`，禁止 `std::to_string`（防止 Locale 乱码坑）。
- **文件读取**：统一使用 `SDL_IOFromFile` / `SDL_ReadIO` 等 SDL3 原生接口（解决 Windows 下中文路径乱码），禁止使用 `std::filesystem` 和 `std::ifstream` 读文件。
- **并发**：禁止裸 `std::thread`，统一用 `Eng::tool::RAIIThread`。禁止手动 lock/unlock，一律 RAII 锁。
- **平台隔离**：`<windows.h>` 锁死在 `Platform.cpp`，业务层绝不写 `#ifdef _WIN32`。

### 🚨 铁律 2：命名与所有权三定律

1. **Service 层（`*Service`）**：跟 `Engine` 同寿，被动调用。（如 `IRenderService`, `AssetService`, `WindowService`）。
2. **Manager 层（`*Manager`）**：跟 Game 状态同寿，主动运行（有 Update / 后台线程）。（如 `MeshManager`, `LODManager`）。
3. **Tool 层（无后缀）**：静态单例或栈对象，函数式调用。（如 `Log`, `Input`, `Window`, `*Transport`, `RAIIThread`）。

### 🚨 铁律 3：渲染架构的“绝对去脑化”

- **RHI 必须纯粹**：`IRenderService` 只负责无状态的指令下发（`CreateBuffer`, `BindVertexBuffer`, `DrawIndexed`）。
- **禁止业务语义侵入**：RHI 层**严禁**出现 `Mesh`, `Material`, `PipelineType`, `GlobalState` 等业务概念。所有管线状态必须通过 `rhi::GraphicsPipelineState` 显式传递，所有常量通过 `PushConstants` 或 `BindUniformBuffer` 传递。
- **渲染大脑归 SceneRenderer**：所有绘制状态的管理、视锥体计算（`Frustum`）、Buffer 的生命周期、MDI 指令的组装，全部在 `SceneRenderer` 内部完成。业务层不碰底层图形 API。

### 🚨 铁律 4：贯彻 RAII 与资源延迟销毁

- **资源生命周期自治**：所有 GPU 资源（`BufferPtr`, `TexturePtr`）基于智能指针 RAII 管理。业务层用完直接离开作用域或 `clear()`，**绝不允许**手动 `WaitIdle` 来阻塞等待资源释放。
- **底层延迟销毁兜底**：RHI 后端（Vulkan/OpenGL）在拦截到资源析构时，必须将其塞入多帧延迟销毁队列（Frame-in-Flight 队列），等待 GPU 执行完 N 帧后再真正调用底层销毁 API。
- **绝不卡帧**：严禁为了同步而频繁调用 `vkDeviceWaitIdle` 或 `glFinish`，务必利用延迟队列保障异步安全。

---

## 二、 当前架构现状（技术实现）

### 1. RHI 层（纯无状态）

- **硬件探测**：`Window` 层通过 `Probe` 深度查询并保存 `RenderCapabilities`。
- **资源抽象**：只暴露 `Buffer`、`Texture`、`Shader`、`Framebuffer` 四种句柄。
- **同步机制**：Vulkan 的同步依赖于 Frame-in-Flight（Fence + Semaphore），资源析构走延迟队列。

### 2. 引擎层（渲染大脑 `SceneRenderer`）

- **数据抽象**：业务层通过 `RenderMesh`（包含 `vbo`, `ibo`, `indexCount`）提交几何数据。
- **视锥剔除**：全局唯一定义在 `SceneRenderer.h` 的 `Frustum` 结构体，对齐了 `[0, 1]` 深度范围（`planes[4] = r2`）。
- **透明与不透明分离**：`Opaque Pass` 走 MDI，`Transparent Pass`（水面）走独立 RenderMesh + 独立 Blend 状态。

### 3. 业务层（Game）

- **全局 UBO 业务化**：`GlobalState`（相机矩阵、光照）已挪回 `Game` 层，由业务层自己创建 UBO 并 `BindUniformBuffer`。
- **生命周期重构**：`EnterGame` / `ExitGame` 严格管理会话状态，杜绝重进游戏的状态残留。

---

## 三、 核心教训与排坑指南

1. **深度与裁剪面坑**：因为全局开启了 `GLM_FORCE_DEPTH_ZERO_TO_ONE`，近平面提取公式是 `planes[4] = r2`，而不是传统的 `r3 + r2`。
2. **Vulkan 画面上下颠倒坑**：RHI 暴露了 `MakePerspectiveMatrix` 接口，由 Vulkan 后端内部偷偷翻转 Y 轴并重映射 Z，业务层拿到的永远是适配当前后端的矩阵。
3. **资源销毁闪屏坑**：如果高频更新大 Buffer（如地形重建）导致画面闪烁甚至露底，说明 GPU 还在用旧的 Buffer 但被 CPU 提前释放了。必须使用延迟销毁队列解决。

---

## 四、 当前进度与下一步

### ✅ 已完成（阶段 1 ~ 5）

- 架构改名重组、所有权梳理、性能排坑。
- DrawCall 收口、硬件能力探测、水体透明分离、RayCast 接入。
- **RHI 彻底无状态化**：全面废弃旧的 `Mesh` 和 `PipelineType` 接口，引入 `GraphicsPipelineState`。
- **纯 Buffer 数据流**：全面接入 `RenderMesh` 和 `CreateBuffer`，消除所有业务与底层的强耦合。
- **视锥剔除收口**：修复了视锥分三处写的错误逻辑，全部统一到 `SceneRenderer::Frustum`。

### 🚧 当前任务：延迟销毁队列（RAII 最后一块拼图）

- [ ] 在 `Engine/tool/DeferredDeleter.h` 中实现通用的多帧延迟销毁器。
- [ ] OpenGL 和 Vulkan 后端各持有一个 `DeferredDeleter`。
- [ ] 在 `DestroyBufferInternal` 中拦截析构，推迟 2 帧（`MAX_FRAMES_IN_FLIGHT`）后真正销毁。
- [ ] 在 `BeginFrame` 中调用 `Process`，在 `Shutdown` 中调用 `Flush`。
- [ ] 彻底解决 Vulkan Validation Layer 的 `VkBuffer currently in use` 警告和画面闪烁问题。

### 📋 未来计划

1. **真正的 Instance Buffer**（当前掉落物还是循环提交 PushConstants，需要写顶点 Divisor 机制）。
2. 玩法闭环：长按挖掘、背包拖拽 UI、粒子系统。

---

## 五、 AI 协作约定

**你是这个项目的结对程序员。请严格遵守以下规则：**

1. **绝对禁止瞎猜**：在修改底层逻辑（特别是坐标运算、结构体定义、多线程同步）时，如果当前上下文没有包含相关文件的具体实现，**必须先主动向用户索要该文件的完整代码**。
2. **拒绝无意义缩写**：变量命名精确到“多几个字母不炸”。`cx` 必须写成 `playerChunkX`。
3. **输出代码必须完整**：在修改核心函数时，不要用 `// ... 剩余代码不变` 糊弄，必须给出完整的函数体。
4. **遵循架构**：不要在 `MyGame` 里写渲染数据管理，交给 `SceneRenderer`。也不要在 RHI 里加任何业务语义。
5. **逐步实施**：不要一次性给出几千行大改，分步骤确认，确保每次编译都能跑通。
6. **平台差异对齐**：写系统交互时优先用 SDL3。写多线程时用 `RAIIThread`。
