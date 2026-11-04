# Mycraft 引擎开发 — AI 上下文恢复与架构规范

> **本文档是 AI 开发协作的上下文恢复入口。**
> 每次开始新对话时，AI 必须完整阅读本文档以恢复项目状态。
> 文档包含：项目概况、铁律、开发习惯、当前架构、排坑指南、进度跟踪、AI 协作约定。

---

## 一、 项目概况与平台基线（⚠️ 必读）

**Mycraft**：Minecraft 风格体素游戏 + 自制体素专用引擎。
**技术栈**：C++23 + CMake + Ninja + vcpkg。桌面端主力编译器为 `clang-cl`，移动端为 `NDK Clang`。

### 🎯 平台与 API 基线

- **桌面端**：Windows 10+，OpenGL 4.6（DSA、MDI）、Vulkan 1.3（Dynamic Rendering、Synchronization2）。
- **移动端**：Android 8 (API 26)+，OpenGL ES 3.1（Indirect Draw、SSBO），支持 **Vulkan 1.3**。

### 🚨 铁律 1：现代 C++ 与系统交互规范

- **内存与数据**：严禁裸指针（`new/delete`），优先 `std::unique_ptr` / `std::shared_ptr`。数据传输用 `std::span`，字符串用 `std::string_view`。
- **并发**：禁止裸 `std::thread`，统一用 `Eng::tool::RAIIThread`。禁止手动 lock/unlock，一律 RAII 锁，配合 `std::stop_token`。
- **平台隔离（极度重要）**：业务层和引擎层（除极个别入口外）**绝对禁止出现 `#ifdef __ANDROID__` 或 `#ifdef _WIN32` 等平台宏**。所有平台差异（如沙盒路径、移动端触屏、安全区）必须抽象到 `Eng::tool::Platform` 或 `FileTool` 内部消化（使用 `Eng::tool::IsMobilePlatform()` 等接口）。
- **跨平台一致性**：严禁使用 `std::mt19937` / `std::shuffle` 作为世界种子，必须使用手搓的 `xorshift32`，保证 PC 与手机生成完全一致的世界。
- **文件 I/O**：全面禁止 `std::fstream` 和 `std::filesystem`。统一使用 `FileTool` 提供的跨平台 SDL IO 接口（`SDL_IOStream`）。

### 🚨 铁律 2：命名与所有权三定律

1. **Service 层**：跟 `Engine` 同寿，被动调用（如 `IRenderService`, `AssetService`, `WindowService`）。
2. **Manager 层**：跟 Game 状态同寿，主动运行（如 `MeshManager`, `LODManager`）。
3. **Tool 层**：静态单例或栈对象，函数式调用（如 `Log`, `Input`, `Platform`）。

### 🚨 铁律 3：渲染架构的“去脑化”

- **RHI 必须纯粹**：`IRenderService` 只负责无状态指令下发。严禁出现业务语义（如 Mesh、Material 等概念）。UI 上下文（如 ImGui 初始化）应由上层（`Game.cpp`）统一管理，RHI 只负责图形绑定。
- **渲染大脑归 SceneRenderer**：视锥体计算、大 Buffer 生命周期、MDI 组装，全在 `SceneRenderer` 内部完成。

### 🚨 铁律 4：贯彻 RAII 与多帧延迟销毁

- **资源生命周期自治**：业务层用完 `BufferPtr` 直接销毁，**绝不允许**手动 `WaitIdle`。
- **底层延迟销毁**：RHI 后端拦截资源析构，推入多帧延迟队列（`DeferredDeleter`）。

---

## 二、 🧑‍💻 开发者偏好与编程习惯（AI 必须遵守）

1. **拒绝魔法数字（Magic Numbers）**：代码中不允许出现无意义的固定数值（如 `0.5f`, `16.0f`, `100`）。必须提取为具名常量，如 `constexpr float PADDING_RATIO = 1.0f;`，并附带清晰注释。
2. **拥抱动态计算**：UI 布局和字体大小等，倾向于基于 `ImGui::GetFontSize()` 等基础变量进行动态比例计算，而不是写死像素值。
3. **纯 CLI 极客流**：主开发环境是 **VSCode + 命令行终端/插件**，不喜欢 Android Studio 和 JetBrains 的臃肿。一切构建（`assembleDebug`）和调试（`adb`）尽量在终端或插件完成。
4. **要完整代码，不要片段**：修改核心函数时，AI 必须输出**完整的函数体**，甚至是**完整的文件**。绝不允许用 `// 省略原有代码` 来敷衍。
5. **恨透了“瞎猜”**：遇到编译/链接报错，AI 绝不能“大概率是某某问题”就乱改。必须先索要相关代码、日志文件或 `CMakeCache.txt` 进行精准排查。
6. **优雅的工程化妥协**：如果非要做妥协（例如平台差异），必须将其锁死在最低层。如果有一种写法能同时兼顾架构优雅和跨平台，必须优先采用。

---

## 三、 目录结构

```text
mycraft/                        # 项目根（CMAKE_SOURCE_DIR / PROJECT_SOURCE_DIR）
├── AI_CONTEXT.md               # 本文档
├── CMakeLists.txt              # 全局架构基线、依赖查找与选项
├── Engine/                     # 引擎层（核心）
│   ├── CMakeLists.txt
│   ├── include/Engine/
│   │   ├── client/             # 渲染大脑（SceneRenderer, Frustum）
│   │   ├── service/            # RHI 接口（IRenderService, AssetService等）
│   │   └── tool/               # 基础工具（RAIIThread, FileTool, Platform, Log）
│   ├── lib/                    # 第三方源码（imgui）
│   └── src/                    # 对应实现
├── mycraft/                    # 游戏业务层
│   ├── CMakeLists.txt
│   ├── assets/                 # 只读资源挂载点（自动打包到 Android APK）
│   │   ├── block/              # 方块 JSON 定义
│   │   ├── shaders/            # 着色器源码与编译产物(.spv)
│   │   │   ├── src/            # GLSL 源码
│   │   │   └── build/          # glslc 编译产物（按 shader 名分子目录）
│   │   ├── textures/           # 纹理
│   │   └── fonts/              # 字体
│   ├── include/game/
│   │   ├── client/             # 客户端逻辑（LODManager, MeshManager, ClientUI）
│   │   ├── core/               # 核心数据（World, BlockRegistry）
│   │   └── server/             # 服务端逻辑（WorldGenerator）
│   └── src/                    # 对应实现
└── mycraft-android/            # Android 独立工程壳（Gradle + JNI）
```

> **assets 挂载约定**：`assets/` 位于 `mycraft/` 业务层子目录下，而非项目根。CMake 中统一用 `${PROJECT_SOURCE_DIR}/mycraft/assets` 显式定位。

---

## 四、 核心技术规范与坑位指南（血泪史）

1. **多 ABI 编译的 Vcpkg Triplet 灾难**：
   - Gradle 的 `abiFilters` 同时配置 `arm64-v8a` 和 `x86_64` 时，**绝对不能在 `build.gradle` 里硬编码 `-DVCPKG_TARGET_TRIPLET`**，否则会导致所有架构都链接同一个架构的库。
   - **正确解法**：在根 `CMakeLists.txt` 的 `project()` 之前，使用 `unset(VCPKG_TARGET_TRIPLET CACHE)` 清空缓存，然后根据 `CMAKE_ANDROID_ARCH_ABI`（而不是 `ANDROID_ABI`）设定对应的 triplet，并使用 `CACHE STRING "" FORCE` 强制写入。同时，为每个 ABI 隔离 `CMAKE_BINARY_DIR`（如 `${CMAKE_BINARY_DIR}/${CMAKE_ANDROID_ARCH_ABI}`），杜绝缓存互串。
2. **Android 平台宏与 Touch 交互**：
   - 业务层严禁 `#ifdef __ANDROID__`。统一使用 `Eng::tool::IsMobilePlatform()`。
   - 在 `Game.cpp` 中集中初始化 ImGui 上下文。移动端需加上 `ImGuiConfigFlags_IsTouchScreen` 和 `NoMouseCursorChange`。
3. **Android 资源路径与安全区**：
   - `AssetService::ReadFile` 中，针对移动端需剥离 `assets/` 前缀（使用 `IsMobilePlatform()` 判断）。图片加载统一用 `SDL_IOFromConstMem` + `IMG_Load_IO`。
   - UI 需使用 `SAFE_TOP_RATIO` 等常量计算安全区，避开系统状态栏、导航条和圆角边缘。
4. **Vulkan 验证层**：
   - Android 模拟器/真机通常不带 `VK_LAYER_KHRONOS_validation`，强行加载会报 `ErrorLayerNotPresent`。必须在代码中使用 `#if !defined(__ANDROID__)` 在编译期屏蔽。
5. **NDK 编译参数污染**：
   - 必须使用 `CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"` 来区分 `clang-cl` 和纯 `clang`，否则纯 Clang 会收到 `/utf-8` 等非法参数。
6. **反斜杠路径灾难**：
   - NDK/CMake 对反斜杠极度敏感。`local.properties`、环境变量和 CMake 参数中的路径必须全部使用正斜杠 `/`。
7. **AGP 与 CMake 版本兼容**：
   - 老版 AGP (8.7.x) 遇到 CMake 4.x 会报 `sysroot` 未初始化。必须同步升级 AGP 至 **8.13.0** 和 Gradle 至 **8.13**。

---

## 五、 当前进度与下一步

### ✅ 已完成

- 跨平台文件、平台、日志系统重构。
- C++23 / SIMD 编译优化。
- LOD 连续内存重构与 MDI 渲染。
- **Android (arm64-v8a) 交叉编译与打包流程全线打通**。
- **UI 层史诗级重构**：主菜单/设置界面安全区适配、触屏滚动支持、消除平台宏、完美 DPI 响应式布局。

### 🚧 当前正在处理

- 解决多 ABI 编译（arm64-v8a 与 x86_64）时的 vcpkg 缓存冲突问题，确保双架构可以同时正确链接。

### 📋 未来计划

1. 在真机上测试核心玩法（物理碰撞、LOD 加载、方块交互）。
2. 落地完整的 `Platform` 抽象（线程池调度等）。
3. 补全 LOD 数据驱动架构。
4. 探索为 GLES 3.1 新增 GLSL 源码编译路径（针对不支持 Vulkan 的旧设备）。

---

## 六、 AI 协作约定（最高优先级）

**你是这个项目的结对程序员。请严格遵守以下规则：**

1. **绝对禁止瞎猜**：在修改底层逻辑时，必须先主动索要相关代码（如 `CMakeCache.txt`、报错日志、完整函数），严禁脑补或给出“大概率”的解决方案。
2. **拒绝无意义缩写**：变量命名精确，如 `currentWindowWidth` 而非 `w`。
3. **输出代码必须完整**：修改核心函数，**必须给出完整的函数体，甚至完整的文件**。绝不允许出现 `// 省略原有代码`。
4. **逐步实施**：每次只改一个环节，确认编译跑通后再继续下一步。
5. **平台差异对齐**：平台差异锁死底层，并发统一用 `RAIIThread` 与 `std::stop_token`。业务层禁止出现 `#ifdef __ANDROID__`。
6. **拒绝魔法数字**：代码中的尺寸、比例必须定义为常量或动态计算。
