
# Mycraft 引擎开发 — AI 上下文恢复与架构规范

> **本文档是 AI 开发协作的上下文恢复入口。**
> 每次开始新对话时，AI 必须完整阅读本文档以恢复项目状态。
> 文档包含：项目概况、铁律、当前架构、排坑指南、进度跟踪、AI 协作约定。

---

## 一、 项目概况与铁律（⚠️ 必读）

**Mycraft**：Minecraft 风格体素游戏 + 自制体素专用引擎。
**技术栈**：C++23 + CMake + Ninja + vcpkg（动态链接 DLL）。编译器主力为 `clang-cl`。
**图形库**：SDL3、SDL3_image、SDL3_net、volk、Vulkan 1.3 (Dynamic Rendering)、OpenGL 4.6 (DSA)、glm、ImGui、nlohmann_json、VMA。

### 🚨 铁律 1：现代 C++ 与系统交互规范

1. **全面拥抱现代 C++（C++23 起步，随时准备 C++26）**：
   - **绝不为了兼容 C 接口而妥协**。业务层与渲染引擎层完全不写 C 接口，禁止裸指针和宏（除非对接底层第三方 C 库）。
   - **数据传输**：优先 `std::span` 代替 `(指针, 长度)` 或 `const std::vector&`。
   - **字符串处理**：优先 `std::string_view`。
   - **内存管理**：完全使用 `std::unique_ptr` / `std::shared_ptr`，结合 `std::expected` 或异常处理错误。
   - **模式设计**：`enum class` + Designated Initializers + RAII，拒绝 C 风格标志位。
   - **并发编程**：禁止裸 `std::thread`，统一使用 `Eng::tool::RAIIThread`（包装 `std::thread` + `std::stop_source`，兼容移动端）。禁止手动 lock/unlock，一律 RAII 锁。
   - **平台隔离**：所有 `<windows.h>` 相关代码锁死在 `Engine/src/tool/Platform.cpp`，头文件零泄漏。业务层绝不写 `#ifdef _WIN32`。
   - **API 选型优先级**：`SDL3` > `标准库` > `系统原生 API`。（为移动端适配做准备）

### 🚨 铁律 2：命名与所有权三定律

**看名字就知类的归属和生命周期。**

1. **Service 层（`*Service`）— 全局唯一服务**
   - 生命周期：跟 `Engine` 同寿；所有权：`Engine` 拥有。
   - 被动调用：你调它才干活。
   - 有多个实现的纯虚接口带 `I` 前缀：`IRenderService`。

2. **Manager 层（`*Manager`）— 业务层子系统**
   - 生命周期：跟 Game 状态同寿；所有权：`MyGame` 拥有。
   - 主动运行：有 `Update` / 后台线程 / 异步任务。

3. **Tool 层（无后缀）— 纯工具**
   - 生命周期：静态单例 or 栈对象。
   - 函数式调用，或只持有“当前这一刻”的状态。
   - 例：`Log` / `Input` / `Window` / `MessageWriter` / `*Transport` / `BlockRegistry` / `RAIIThread`。

### 🚨 铁律 3：依赖洁癖与日志用法

- **依赖洁癖**：已装库必须榨干（SDL3、ImGui、nlohmann_json），不新增不必要依赖。
- **日志用法**：流式宏，不需要 `.get()`，每个类配一个：

   ```cpp

   std::unique_ptr<Eng::tool::Log> m_logger = std::make_unique<Eng::tool::Log>();
   logInfo(m_logger, "[Tag] Value: " << val);

   ```

- **Vulkan 结构体初始化**：优先 `vk::XXXCreateInfo` 指定初始化。VMA 用 `reinterpret_cast<const VkImageCreateInfo*>(&cppStruct)` 零开销转换。

---

## 二、 当前架构与分层图

### 📐 分层图

```text
┌───────────────────────────┐
│  Engine（单例）                                      │
│  ├── IRenderService*      ← Engine 拥有             │
│  ├── AssetService*        ← Engine 拥有             │
│  ├── WindowService*       ← Engine 拥有             │
│  ├── Window* m_mainWindow ← 非拥有                  │
│  └── IGame* m_game        ← Engine 拥有             │
└───────────────────────────┘
                    │ engine.GetRenderService()
                    ▼
┌───────────────────────────┐
│  MyGame（Game 层）                                   │
│  ├── SceneRenderer*   ← Game 拥有（渲染大脑）      │
│  ├── MeshManager*     ← Game 拥有（多线程 Mesh）   │
│  ├── LODManager*      ← Game 拥有（LOD 生成）      │
│  ├── ClientWorld*     ← Game 拥有（数据层）        │
│  ├── ClientUI*        ← Game 拥有                  │
│  └── 物理/输入/网络/热栏                            │
└───────────────────────────┘
                    │ m_sceneRenderer->Render()
                    ▼
┌────────────────────────┐
│  SceneRenderer（渲染大脑，引擎层提供）               │
│  ├── 持有 VBO/IBO 与 MDI 指令                       │
│  ├── 异步重建/显存压实                              │
│  └── 视锥剔除 + DrawMDI 提交                        │
└────────────────────────┘
```

### 🔑 核心决策

1. **Service 归属**：`IRenderService`、`AssetService`、`WindowService` 的生命周期归 `Engine` 拥有。
2. **渲染大脑归属**：`SceneRenderer` 位于引擎层，但由 `MyGame` 拥有并驱动。
3. **渲染资源解耦**：`SceneRenderer` 构造只接收 RHI 和 Assets。地形图集由 `MyGame` 通过 `SetAtlasTexture` 注入。
4. **只允许 `main.cpp` 使用 `Engine::GetInstance()`**，其他通过 `engine.GetXxx()` 或构造注入。

---

## 三、 当前架构现状（技术实现）

### 1. RHI 层（稳定优先）

- **智能指针**：`MeshPtr` / `TexturePtr` / `ShaderPtr`。
- **同步机制**：`VulkanRenderService` 底层严格控制了 Fence 与 Semaphore 的生命周期，**不允许**在未经验证的情况下随意引入 `std::async` 或后台异步扩容，极易引发 Release 下的 Fence 死锁与显存暴涨。

### 2. MDI 架构与流式加载

- **全局大 Buffer**：`m_globalVBO` / `m_globalIBO` 按帧双缓冲。
- **业务层分片**：`ClientWorld` 按区块中心距离切片请求，每帧最多发 10 个，彻底杜绝瞬间发包造成的卡顿。
- **网格上传**：`SceneRenderer::SubmitStaticMesh` 采用 `shared_ptr` 零拷贝接收，按帧预算（3ms）增量上传。

### 3. 数据驱动与防卡顿

- **LOD 生成**：哈希已升级为混合哈希，彻底杜绝退化。主线程每帧最多 `CreateMesh` 1 个，防止阻塞。
- **网络收包**：每帧最多处理 15 个网络包（防瞬间解析卡顿）。

---

## 四、 核心教训与排坑指南（新老必读）

### 1. 性能优化的“深渊”（血泪教训）

- **不要轻易在 RHI 加异步扩容**：我们曾尝试在 `VulkanRenderService` 中引入异步销毁和延迟队列，结果在 Release 下引发了几十秒的假死。**目前底层已全面回退到安全版本的同步逻辑**。
- **`std::sort` 必须限制作用域**：不要在 `OnUpdate` 里每帧无脑对全图区块排序，务必用 `if` 包住跨区块判定，并用切片队列分帧发送。
- **LODManager::DrainResults 的 `done++`**：`done++` 必须在 `continue` 之前！否则被跳过的旧数据会导致 `for(;;)` 死循环。这是断点卡死的罪魁祸首。

### 2. Vulkan 渲染同步

- **信号量与帧索引**：`m_presentCompleteSemaphores` 和 `m_renderFinishedSemaphores` 必须按 Swapchain Image 数量创建。Semaphore 用 `m_imageIndex`，Fence 用 `m_frameIndex`。
- **动态状态**：开启 `DynamicState::eViewport` / `eScissor` 后，`beginRendering` 后必须手动设置。

### 3. Shader 与坐标系

- **业务层统一 OpenGL 规范**（Y 轴向上，NDC [-1, 1]）。
- **Vulkan 后端内部偷翻 Y 轴**并映射 Z 到 `[0, 1]`。
- **绝不在 `mesh.vert` 残留实例矩阵**。

### 4. 生命周期与清理

- **UAF 崩溃**：严格销毁顺序：`MyGame::OnShutdown` → `m_game.reset()` → `m_assetService.reset()` → `m_renderService.reset()` → `m_windowService.reset()`。
- **Framebuffer 双销毁**：颜色附件 `texInt.ownsImage = false`。

### 5. 工程化与工具链

- **全局替换警告**：修改类名时，务必检查 `lib/`，用 `git checkout -- <path>` 还原误伤的第三方代码。
- **clangd 配置**：让 `.clang-tidy` 管规则，`.clangd` 只保留 `CompilationDatabase`。

---

## 五、 当前进度与下一步

### ✅ 已完成

- **阶段 1**：架构改名与物理重组。
- **阶段 2**：所有权梳理（Engine 接管 Service，Window 瘦身，SceneRenderer 剥离并下沉）。
- **阶段 2.5：卡顿专项排查与修复**：
  - 修复了 `LODManager` 因 `continue` 导致的死循环和 `LodKeyHash` 退化。
  - 优化了 `ClientWorld::UpdateStreaming` 的跨区块判定、切片排序、队列发送。
  - 限制了网络收包频率（15包/帧），并利用 `std::span` 消除区块解析的 1.6MB 内存拷贝。
  - 回退 RHI 激进优化，确保 Release 不假死。

### 🚧 下一步：阶段 3（DrawCall 收口）

- [ ] 引入 `SceneRenderer::SubmitEntity` / `SubmitSkybox` / `SubmitHand`
- [ ] 将 `MyGame::OnRender` 里的 `renderer->DrawXXX` 收拢，业务层只负责计算数据并提交给大脑。
- [ ] 编译跑通 → commit。

### 📋 未来计划

1. **Mipmap + 各向异性过滤**（当前 atlas 纹理无 mip，远处糊）。
2. **玩法闭环**：长按挖掘、背包拖拽 UI、粒子系统。

---

## 六、 AI 协作约定

**你是这个项目的结对程序员。请严格遵守以下规则：**

1. **绝对禁止瞎猜**：在修改底层逻辑（特别是坐标运算、结构体定义、多线程同步）时，如果当前上下文没有包含相关文件的具体实现，**必须先主动向用户索要该文件的完整代码**。
2. **拒绝无意义缩写**：变量命名精确到“多几个字母不炸”。`cx` 必须写成 `playerChunkX`。
3. **输出代码必须完整**：在修改核心函数时，不要用 `// ... 剩余代码不变` 糊弄，必须给出完整的函数体。
4. **遵循架构**：不要在 `MyGame` 里写渲染数据管理，交给 `SceneRenderer`。
5. **逐步实施**：不要一次性给出几千行大改，分步骤确认，确保每次编译都能跑通。**如果遇到底层瓶颈，先用最稳妥的同步方案跑通，严禁一上来就写“高度并发的异步管线”**。
6. **平台差异对齐**：写系统交互时优先用 SDL3。写多线程时用 `RAIIThread`。
