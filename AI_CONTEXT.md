
# Mycraft 引擎开发 — AI 上下文恢复与架构规范 (V2)

> **本文档是 AI 开发协作的上下文恢复入口。**
> 每次开始新对话时，AI 必须完整阅读本文档以恢复项目状态。

---

## 一、 项目概况与技术基线

**Mycraft**：Minecraft 风格体素游戏 + 自制体素专用引擎。
**技术栈**：C++23 + CMake + Ninja + vcpkg。桌面端为 `clang-cl`，移动端为 `NDK Clang`。

### 🎯 平台与 API 基线

* **桌面端**：Windows 10+，Vulkan 1.3（主力，Dynamic Rendering, Synchronization2），OpenGL 4.6（兼容兜底）。
* **移动端**：Android 8 (API 26)+，Vulkan 1.3，OpenGL ES 3.1（未来兜底）。
* **UI 框架**：Dear ImGui `v1.93.0`（已升级至最新动态字体系统与 DrawList 重构版）。

---

## 二、 🚨 核心铁律与架构原则（最高优先级）

1. **平台绝对隔离**：业务层和引擎层严禁出现 `#ifdef __ANDROID__` / `_WIN32`。所有差异必须下沉到 `Platform` 或 `FileTool`。
2. **RHI 纯粹去脑化**：`IRenderService` 只做无状态指令下发，严禁懂 Mesh/Material 等业务概念。
3. **依赖显式注入 (DI)**：引擎不再充当“霸道总裁”全量初始化服务，业务层需要什么，就显式调 `engine.CreateXXXService()`。业务层只通过 `engine.GetXXX()` 拿实例。
4. **彻底 RAII**：严禁裸 `new/delete` 和裸 `std::thread`。资源生命周期绑定到 `std::shared_ptr`，底层利用 `DeferredDeleter` 延迟销毁。
5. **拒绝魔法数字**：尺寸、阈值全用 `constexpr` 具名常量。

---

## 三、 ✅ 近期重大里程碑（已完成）

### 1. 渲染架构史诗级蜕变：Texture Array

* **彻底废弃 Texture Atlas**：删除了所有恶心的 `GetTileUV` 和 `ATLAS_PADDING` 内缩逻辑。
* **RHI 全量支持 Array**：`IRenderService` 引入 `arrayLayers` 和 `Vertex::layer`。OpenGL 与 Vulkan 后端已完美支持 `GL_TEXTURE_2D_ARRAY` / `VK_IMAGE_VIEW_TYPE_2D_ARRAY`。
* **根除远景诅咒**：远景水面变暗、雪地黑色细网格线、Mipmap 渗色问题被彻底物理消灭。

### 2. 多线程与内存安全

* **网格防覆盖**：引入 `MeshTask.version` 与 `m_chunkMeshVersions` 机制，彻底解决多线程异步构建覆盖导致的黑洞。
* **显存自动清理**：重写了 `SceneRenderer` 的垃圾回收阈值（>50万或10%即触发），修复了 `m_deferredAppends` 溢出现象，杜绝显存泄漏。
* **VMA 修复**：修复了 Debug 模式下手动导入函数指针导致的栈溢出，改用动态加载入口。

### 3. 服务端与出生点稳固

* **出生点抢救**：实装 512 格同心圆寻陆算法，如果全海则强制生成 5x5 草方块平台。
* **光照修复**：出生点预生成 3x3 区块并注入完整的 `LightEngine::InitRegion` 计算。

---

## 四、 🚧 当前正在处理：引擎输入系统重构

**痛点**：目前 `Input` 是全局静态单例，无法依赖注入，且无法优雅支持多窗口与移动端多点触控。

**重构方案蓝图**：

1. **废除单例**：将 `Input` 改为 `Engine` 持有的实例（`std::unique_ptr<tool::Input> m_input;`）。
2. **事件分发改造**：`WindowService` 捕获的 SDL 事件，通过 `Engine` 转发给 `m_input->ProcessEvent(event)`。
3. **业务层适配**：`Game.cpp` 中的 36 处 `Input::` 调用全部改为通过 `engine.GetInput().` 调用。
4. **触控预埋**：在 `Input.h` 中新增 `TouchPoint` 结构（`fingerId`, `position`, `delta`, `active`），拦截 SDL3 的 `SDL_EVENT_FINGER_DOWN/MOTION/UP`。

---

## 五、 📋 未来计划：移动端触控体验

1. **虚拟摇杆**：动态原点（按在屏幕左半区何处，摇杆中心就在哪），计算归一化向量传给 `moveDir`。
2. **右屏视角滑动**：读取 `TouchPoint.delta`，转化为 `yaw/pitch` 增量，需与 ImGui 的 `WantCaptureMouse` 拦截配合。
3. **动作虚拟按键**：屏幕右下角加入跳跃、挖掘（长按）、放置（点击）的大热区按钮。

---

## 六、 ⚠️ 排坑指南（血泪史补充）

* **ImGui 1.93.0 升级警告**：新版移除了 `AddLine` 内部默认的 `+0.5f` 偏移，去掉了 Slider/Input 的实时编辑（LiveEdit）。手绘 UI 需注意检查边框和准星是否模糊。
* **Android 资源剥离**：`AssetService::ReadFile` 中需利用 `IsMobilePlatform()` 剥离 `assets/` 前缀。
* **Vulkan 验证层**：Android 端无 Validation Layer，编译期需用 `#if !defined(__ANDROID__)` 屏蔽。
* **纹理数组的 UI 限制**：ImGui 后端不支持 `sampler2DArray`，目前 HUD 的方块图标暂时用纯色等距方块渲染（若需高清贴图，需单独建 2D 纹理供 UI 用）。
* **VMA 配置**：`VMA_DYNAMIC_VULKAN_FUNCTIONS=1`，初始化时只需传 `vkGetInstanceProcAddr` 和 `vkGetDeviceProcAddr`。

---

## 七、 🤝 AI 协作约定（最高优先级）

1. **绝对禁止瞎猜**：修改底层逻辑前，必须先索要相关代码和日志。
2. **要完整代码，不要片段**：修改核心函数必须给出完整实现，绝不用 `// 省略` 敷衍。
3. **逐步实施**：每次只解决一个架构痛点，跑通后再继续下一步。
4. **命名精确**：拒绝无意义缩写（用 `currentWindowWidth` 不用 `w`）。
