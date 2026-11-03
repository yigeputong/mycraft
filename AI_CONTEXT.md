# Mycraft 引擎开发 — AI 上下文恢复与架构规范

> **本文档是 AI 开发协作的上下文恢复入口。**
> 每次开始新对话时，AI 必须完整阅读本文档以恢复项目状态。
> 文档包含：项目概况、铁律、当前架构、排坑指南、进度跟踪、AI 协作约定。

---

## 一、 项目概况与平台基线（⚠️ 必读）

**Mycraft**：Minecraft 风格体素游戏 + 自制体素专用引擎。
**技术栈**：C++23 + CMake + Ninja + vcpkg。编译器主力为 `clang-cl`。

### 🎯 平台与 API 基线

- **桌面端**：Windows 10+，目标后端为 **OpenGL 4.6**（全开 DSA、MDI）、**Vulkan 1.3**（Dynamic Rendering、Synchronization2）。
- **移动端**：Android 8 (API 26)+，目标后端为 **OpenGL ES 3.1**（支持 Indirect Draw、SSBO，无 DSA）；iOS 16.0+，目标后端为 **Metal**。

### 🚨 铁律 1：现代 C++ 与系统交互规范

- **内存与数据**：严禁裸指针（`new/delete`），优先 `std::unique_ptr` / `std::shared_ptr`。数据传输用 `std::span`，字符串用 `std::string_view`。
- **并发**：禁止裸 `std::thread`，统一用 `Eng::tool::RAIIThread`。禁止手动 lock/unlock，一律 RAII 锁。
- **平台隔离**：`<windows.h>` 锁死在 `Platform.cpp`，业务层绝不写 `#ifdef _WIN32`。所有平台差异（如移动端大小核调度、GLES/Metal 特性探针）必须在 `Platform.cpp` 或 RHI 内部消化。
- **跨平台一致性**：**严禁使用 `std::mt19937` / `std::shuffle` 作为世界种子生成器**，必须使用手搓的 `xorshift32`，保证 PC 与手机生成完全一致的世界。

### 🚨 铁律 2：命名与所有权三定律

1. **Service 层（`*Service`）**：跟 `Engine` 同寿，被动调用。（如 `IRenderService`, `AssetService`, `WindowService`）。
2. **Manager 层（`*Manager`）**：跟 Game 状态同寿，主动运行（有 Update / 后台线程）。（如 `MeshManager`, `LODManager`）。
3. **Tool 层（无后缀）**：静态单例或栈对象，函数式调用。（如 `Log`, `Input`, `Window`, `*Transport`, `RAIIThread`）。

### 🚨 铁律 3：渲染架构的“去脑化”

- **RHI 必须纯粹**：`IRenderService` 只负责无状态指令下发（`CreateBuffer`, `BindShader`, `DrawIndexed` 等）。
- **禁止业务语义**：RHI 层严禁出现 `Mesh`, `Material`, `PipelineType`, `GlobalState`。状态通过 `GraphicsPipelineState` 传递，常量通过 `PushConstants` 或 `BindUniformBuffer` 传递。
- **渲染大脑归 SceneRenderer**：视锥体计算（`Frustum`）、大 Buffer 生命周期、MDI 组装，全部在 `SceneRenderer` 内部完成。

### 🚨 铁律 4：贯彻 RAII 与多帧延迟销毁

- **资源生命周期自治**：业务层用完 `BufferPtr` 直接销毁，**绝不允许**手动 `WaitIdle`。
- **底层延迟销毁**：RHI 后端拦截资源析构，推入多帧延迟队列（`DeferredDeleter`），等 GPU 执行完 N 帧后安全回收。零卡帧、零 Validation 报错。

---

## 二、 当前架构现状（技术实现）

### 1. RHI 层（纯无状态）

- **资源抽象**：只暴露 `Buffer`, `Texture`, `Shader`, `Framebuffer` 句柄。着色器全面走向 `ShaderBytecode`（支持 SPIR-V，未来需兼容 GLES 的 GLSL 源码）。
- **延迟销毁**：双端后端通过 `Eng::tool::DeferredDeleter` 处理 Frame-in-Flight 安全释放。

### 2. 引擎层（渲染大脑 `SceneRenderer`）

- **纯数据驱动**：业务层提交 `RenderMesh`（`vbo`, `ibo`, `indexCount`），引擎负责组装 MDI。
- **视锥剔除**：全局唯一定义在 `SceneRenderer.h` 的 `Frustum`，修复了 `GLM_FORCE_DEPTH_ZERO_TO_ONE` 下的近平面提取公式（`planes[4] = r2`）。
- **深度预处理**：RHI 暴露 `MakePerspectiveMatrix`，Vulkan 内部翻转 Y 轴并重映射 Z，业务层拿到的矩阵直接可用。

### 3. 游戏逻辑与流水线

- **异步区块生成**：`GameServer` 采用流水线架构，`InFlightChunk` + `move_only_function`/`UniqueFunction` 多线程任务流，拆分 `BiomeAndDensity` -> `TerrainBlocks` -> `Features` -> `Lighting`。
- **地形算法**：彻底节点化。`DensityFunction.h` 提供了 `Add`, `Mul`, `SplineDF`, `SmoothstepDF`, `YGradient` 等节点，在 `TerrainGenerator` 构造函数中像搭积木一样拼装出平原平坦、山脉险峻的地形。
- **性能优化**：
  - `PerlinNoise` 全面无分支化（LUT 查表、常量数组取梯度）。
  - 网格步长拆分：`STEP_XZ = 4`, `STEP_Y = 8`，性能翻倍且平滑度不降。
  - 手写 `FastFloor`、`FastAbs`，抛弃标准库 `cmath` 的边缘开销。

---

## 三、 核心教训与排坑指南

1. **OpenGL 状态污染（画面撕裂）**：渲染透明物体（水面）和天空盒后，必须重置深度写入。解决方案在 `EndFrame` 中强行 `glDepthMask(GL_TRUE)` 和 `glColorMask`。
2. **Vulkan 画面颠倒**：由 RHI 的 `MakePerspectiveMatrix` 接口内部消化翻转，业务层统一按 OpenGL 约定传参。
3. **跨平台资源竞争**：多线程生成区块时，严禁直接在 `LambdaDF` 内部重复计算 2D 噪声。必须把参数提到 Y 轴循环外。
4. **移动端大小核陷阱**：移动端 Worker 数量限制在 2~4 个，桌面端按 `hw - 2`。必须通过平台抽象接口获取线程数。
5. **着色器格式限制**：GLES 3.1 极大可能不支持 `glShaderBinary` (SPIR-V)，未来需在 RHI 层新增源码字符串（GLSL）的 `ShaderCreateInfo` 重载。

---

## 四、 当前进度与下一步

### ✅ 已完成（阶段 1 ~ 5）

- 架构改名重组、所有权梳理、性能排坑。
- RHI 彻底无状态化、RAII 延迟销毁队列。
- 视锥剔除收口、水体透明分离、Tectonic 风格地形节点树化。
- 跨平台绝对一致的 xorshift PRNG 种子洗牌。

### 🚧 当前任务（三选一）

1. **落地 `Platform` 抽象**：编写 `Platform::GetRecommendedWorkerThreads()`，剥离大小核调度。
2. **地形数据驱动**：编写 JSON 解析器，将 `TerrainGenerator` 构造函数里的节点拼装改为读取 JSON。
3. **图形后端准备**：修改 CMake 构建脚本，为 OpenGL ES 3.1（GLSL 源码）和 iOS Metal 准备分支。

---

## 五、 AI 协作约定

**你是这个项目的结对程序员。请严格遵守以下规则：**

1. **绝对禁止瞎猜**：在修改底层逻辑（特别是坐标运算、结构体定义、多线程同步）时，如果当前上下文没有包含相关文件的具体实现，**必须先主动向用户索要该文件的完整代码**。
2. **拒绝无意义缩写**：变量命名精确。
3. **输出代码必须完整**：修改核心函数时，必须给出完整的函数体。
4. **逐步实施**：分步骤确认，确保每次编译都能跑通。
5. **平台差异对齐**：所有 `#ifdef` 锁死在底层。写多线程时用 `RAIIThread`，写数据流时用 `UniqueFunction` 或 `std::move_only_function`。
