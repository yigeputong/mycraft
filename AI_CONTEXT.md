# Mycraft 引擎开发 — AI 上下文恢复与架构规范

> **本文档是 AI 开发协作的上下文恢复入口。**
> 每次开始新对话时，AI 必须完整阅读本文档以恢复项目状态。
> 文档包含：项目概况、铁律、当前架构、排坑指南、进度跟踪、AI 协作约定。

---

## 一、 项目概况与平台基线（⚠️ 必读）

**Mycraft**：Minecraft 风格体素游戏 + 自制体素专用引擎。
**技术栈**：C++23 + CMake + Ninja + vcpkg。桌面端主力编译器为 `clang-cl`，移动端为 `NDK Clang`。

### 🎯 平台与 API 基线

- **桌面端**：Windows 10+，OpenGL 4.6（DSA、MDI）、Vulkan 1.3（Dynamic Rendering、Synchronization2）。
- **移动端**：Android 8 (API 26)+，OpenGL ES 3.1（Indirect Draw、SSBO），支持 **Vulkan 1.3**。

### 🚨 铁律 1：现代 C++ 与系统交互规范

- **内存与数据**：严禁裸指针（`new/delete`），优先 `std::unique_ptr` / `std::shared_ptr`。数据传输用 `std::span`，字符串用 `std::string_view`。
- **并发**：禁止裸 `std::thread`，统一用 `Eng::tool::RAIIThread`。禁止手动 lock/unlock，一律 RAII 锁。
- **平台隔离**：`<windows.h>` 锁死在 `Platform.cpp`，业务层绝不写 `#ifdef _WIN32`。所有平台差异必须在 `Platform.cpp` 或 RHI 内部消化。
- **跨平台一致性**：严禁使用 `std::mt19937` / `std::shuffle` 作为世界种子，必须使用手搓的 `xorshift32`。
- **文件 I/O**：全面禁止 `std::fstream` 和 `std::filesystem`。统一使用 `FileTool` 提供的跨平台 SDL IO 接口。

### 🚨 铁律 2：命名与所有权三定律

1. **Service 层**：跟 `Engine` 同寿，被动调用。
2. **Manager 层**：跟 Game 状态同寿，主动运行（如 `MeshManager`, `LODManager`）。
3. **Tool 层**：静态单例或栈对象，函数式调用（如 `Log`, `Input`, `*Transport`, `RAIIThread`）。

### 🚨 铁律 3：渲染架构的“去脑化”

- **RHI 必须纯粹**：`IRenderService` 只负责无状态指令下发。严禁出现业务语义。
- **渲染大脑归 SceneRenderer**：视锥体计算、大 Buffer 生命周期、MDI 组装，全在 `SceneRenderer` 内部完成。

### 🚨 铁律 4：贯彻 RAII 与多帧延迟销毁

- **资源生命周期自治**：业务层用完 `BufferPtr` 直接销毁，**绝不允许**手动 `WaitIdle`。
- **底层延迟销毁**：RHI 后端拦截资源析构，推入多帧延迟队列（`DeferredDeleter`）。

---

## 二、 目录结构

```text
mycraft/                        # 项目根（CMAKE_SOURCE_DIR / PROJECT_SOURCE_DIR）
├── AI_CONTEXT.md            # 本文档
├── CMakeLists.txt           # 全局架构基线、依赖查找与选项
├── Engine/                  # 引擎层（核心）
│   ├── CMakeLists.txt
│   ├── include/Engine/
│   │   ├── client/        # 渲染大脑（SceneRenderer, Frustum）
│   │   ├── service/       # RHI 接口（IRenderService, AssetService等）
│   │   └── tool/          # 基础工具（RAIIThread, FileTool, Platform, Log）
│   ├── lib/                # 第三方源码（imgui）
│   └── src/                # 对应实现
├── mycraft/                 # 游戏业务层（CMAKE_CURRENT_SOURCE_DIR of mycraft/CMakeLists.txt）
│   ├── CMakeLists.txt
│   ├── assets/             # 只读资源挂载点（自动打包到 Android APK）
│   │   ├── block/         # 方块 JSON 定义
│   │   ├── shaders/       # 着色器源码与编译产物(.spv)
│   │   │   ├── src/      # GLSL 源码
│   │   │   └── build/    # glslc 编译产物（按 shader 名分子目录）
│   │   ├── textures/      # 纹理
│   │   └── fonts/         # 字体
│   ├── include/game/
│   │   ├── client/        # 客户端逻辑（LODManager, MeshManager）
│   │   ├── core/          # 核心数据（World, BlockRegistry）
│   │   └── server/        # 服务端逻辑（WorldGenerator, DensityFunction）
│   └── src/                # 对应实现
└── mycraft-android/         # Android 独立工程壳（Gradle + JNI）
    ├── app/
    │   ├── build.gradle    # 包含 vcpkg 及 CMake 配置
    │   └── src/main/       # Java 壳与 SDLActivity
    ├── build.gradle
    ├── gradle.properties    # 包含 JVM 内存与代理配置
    ├── local.properties     # sdk.dir=C:/Dev/AndroidSDK (正斜杠)
    └── settings.gradle
```

---

## 三、 当前架构现状（技术实现）

### 1. 基础工具层 (Tool)

- **Platform**：全面接管 SDL3 的 CPU 核数探测、运行时分发（AVX2/NEON）以及 `GetRecommendedWorkerThreads()` 调度。
- **FileTool**：统一管理跨平台路径（iOS/Android 沙盒），屏蔽 SDL_IOStream 底层细节。
- **Log**：剥离 `<fstream>`，使用 `SDL_IOStream` 与 `SDL_Log`，提供实例宏与静态宏。
- **线程池**：基于 `RAIIThread` + `std::stop_token`，彻底告别裸 `thread`。

### 2. 渲染层 (RHI & SceneRenderer)

- **图形 API**：纯无状态 RHI。引入 `GraphicsPipelineState` 和 `TextureCreateInfo`（支持指定 Sampler 采样器、Mipmap 策略）。
- **LOD 远景系统**：
  - 脱离 `unordered_map`，改为 `std::vector` 连续内存存储。
  - 螺旋方环生成 + 距离优先排序。
  - 全局 Mega Buffer 增量上传，主线程不卡顿。
  - MDI 超级合批渲染。
  - 强行拉长裙边（Skirt）防跨级漏光。

### 3. 游戏与服务端 (Game & Server)

- **服务端流水线**：四阶段任务扁平化（密度 -> 地形 -> 特征 -> 光照），配合 `MAX_CONCURRENT_GENERATING` 背压控制，防止 OOM。
- **地形生成**：彻底节点化、JSON 数据驱动，基于 `xorshift32` 的跨平台绝对一致噪声生成。

---

## 四、 核心教训与排坑指南（血泪史）

1. **Android NDK 编译参数污染**：
   - 必须使用 `CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"` 来区分 `clang-cl` 和 `clang`，否则纯 Clang 会收到 `/utf-8` 等非法参数。
2. **Android 符号链接缺失**：
   - SDL3 默认找 `iconv`，Android 符号名为 `libiconv`，必须在 CMake 中定义 `SDL_ICONV=1` 强制使用内置实现。
   - ImGui 默认使用桌面 OpenGL 函数（如 `glGenSamplers`），在 Android 上必须定义 `IMGUI_IMPL_OPENGL_ES3`。
3. **反斜杠路径灾难**：
   - NDK/CMake 对反斜杠极度敏感。`local.properties` 和环境变量中的路径必须全部使用正斜杠 `/`。
4. **AGP 与 CMake 版本兼容**：
   - 老版 AGP (8.7.x) 遇到 CMake 4.x 会报 `sysroot` 未初始化。必须同步升级 AGP 至 **8.13.0** 和 Gradle 至 **8.13**。
5. **显存/内存溢出防范**：
   - 渲染大 Buffer 重建时，采用 `mutable std::mutex` 保护后台线程的读取，**切勿深拷贝快照**。
   - 限制每帧的 GPU 上传任务数与 Mesh 构建任务数。
6. **Android JNI 入口失踪**：
   - NDK 默认隐藏所有符号，如果 C++ 找不到 `SDL_main`，必须在 `main.cpp` 中用 `extern "C" __attribute__((visibility("default")))` 强制导出。
7. **Vulkan 验证层导致闪退**：
   - 安卓模拟器默认不带 `VK_LAYER_KHRONOS_validation`，C++ 初始化 Vulkan 时如果强开验证层会报 `ErrorLayerNotPresent`。必须加上 `!defined(__ANDROID__)` 屏蔽。
8. **Android 资源路径**：
   - SDL_IOFromFile 在 Android 下直接读取 APK 内 assets 根目录，不能带 `assets/` 前缀。必须在 `AssetService::ReadFile` 内部做剥离兼容。
9. **ImGui 事件保护**：
    - 渲染器初始化失败时，`WindowService` 里绝不能直接调用 `ImGui_ImplSDL3_ProcessEvent`，必须 `if (ImGui::GetCurrentContext() != nullptr)`，否则空指针崩溃。

---

## 五、 当前进度与下一步

### ✅ 已完成

- 跨平台文件、平台、日志系统重构。
- C++23 / SIMD 编译优化。
- LOD 连续内存重构与 MDI 渲染。
- **Android (arm64-v8a) 交叉编译与打包流程全线打通**。

### 🚧 当前正在处理

- 解决链接期最后的符号缺失（SDL iconv、ImGui OpenGL ES），跑通 Android 真机部署。

### 📋 未来计划

1. 落地完整的 `Platform` 抽象。
2. 补全 LOD 数据驱动架构。
3. 引入移动端触摸/手势适配。
4. 探索为 GLES 3.1 新增 GLSL 源码编译路径。

---

## 六、 AI 协作约定

**你是这个项目的结对程序员。请严格遵守以下规则：**

1. **绝对禁止瞎猜**：在修改底层逻辑时，必须先主动索要相关代码，严禁脑补。
2. **拒绝无意义缩写**：变量命名精确。
3. **输出代码必须完整**：修改核心函数，必须给出完整的函数体。
4. **逐步实施**：每次编译跑通后再继续下一步。
5. **平台差异对齐**：平台差异锁死底层，并发统一用 `RAIIThread` 与 `stop_token`。
