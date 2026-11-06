# Mycraft 引擎开发 — AI 上下文恢复与架构规范 (V4)

> **本文档是 AI 开发协作的上下文恢复入口。**
> 每次开始新对话时，AI 必须完整阅读本文档以恢复项目状态。

---

## 一、 项目概况与技术基线

**Mycraft**：Minecraft 风格体素游戏 + 自制体素专用引擎。
**技术栈**：C++23 + CMake + Ninja + vcpkg。桌面端为 `clang-cl`，移动端为 `NDK Clang` (NDK 30.0.16248370)。

### 🎯 平台与 API 基线

* **桌面端**：Windows 10+，Vulkan 1.3（主力，Dynamic Rendering, Synchronization2），OpenGL 4.6（兼容兜底）。
* **移动端**：Android 8 (API 26)+，Vulkan 1.3（部分设备如骁龙662仅支持1.1需降级），OpenGL ES 3.1（未来兜底）。
* **UI 框架**：Dear ImGui `v1.93.0`（已适配动态字体、新 DrawList、去除了默认 `+0.5f` 偏移）。
* **工程规范**：已配置 `.clang-format` 与 `.clang-tidy`，严禁隐式类型转换，强制使用 C++23 特性，严格限制锁的作用域。

---

## 二、 🚨 核心铁律与架构原则（最高优先级）

1. **平台绝对隔离**：业务层和引擎层严禁出现 `#ifdef __ANDROID__` / `_WIN32`。所有差异必须下沉到 `Platform`、`FileTool` 或 `Window`。
2. **RHI 纯粹去脑化**：`IRenderService` 只做无状态指令下发，严禁懂 Mesh/Material 等业务概念。
3. **依赖显式注入 (DI)**：业务层需要什么，就显式调 `engine.CreateXXXService()`。
4. **彻底 RAII**：严禁裸 `new/delete` 和裸 `std::thread`。资源生命周期绑定到 `std::shared_ptr`。
5. **拒绝魔法数字**：尺寸、阈值全用 `constexpr` 具名常量（包括手部坐标、渲染顺序等）。
6. **锁的黄金定律**：**只在真正需要锁的地方加锁，绝不在锁内做网络 I/O 或复杂计算，严禁嵌套锁导致死锁。**

---

## 三、 ✅ 近期重大里程碑（已完成）

### 1. 移动端输入系统彻底重构

* **废除单例**：`InputService` 由 `Engine` 持有并注入。
* **Action 注册表**：统一 `Jump/FlyUp` 与 `Sneak/FlyDown` 语义，支持 `InjectActionState` 和 `InjectMoveDir`。
* **绕过 SDL3 Android 触摸 Bug**：关闭 `SDL_HINT_TOUCH_MOUSE_EVENTS`，在 `Window::onEvent` 中手动将 `SDL_EVENT_FINGER_*` 映射为 ImGui 的 `MousePos/Button` 事件，解决菜单点不动和坐标偏移。
* **修复“幽灵窗口”**：移除无 `ImGui::Begin` 包裹的 `InvisibleButton` 调用（如 HUD 快捷栏），改用原生的鼠标/触摸坐标手动判定点击。
* **修复全屏乱挖 Bug**：将 `ResetInjectedStates` 移入 `ClientUI::Draw` 最前端，每帧无条件清零，防止 `IsActionDown` 状态残留。

### 2. 渲染管线的底层修复

* **纹理统一**：强制 Vulkan 与 OpenGL 的 RHI 底层将所有单层纹理创建为 `e2DArray`，完美匹配 Shader 中的 `sampler2DArray`，解决玩家皮肤错位和触发 `discard` 导致手消失的问题。
* **渲染顺序重构**：`SceneRenderer::EndScene` 调整为：地形 -> 水（透明队列） -> 实体/天空盒/手。彻底解决第一人称视角下水覆盖手部的穿模问题。
* **LOD 边界剔除**：修复 `LODManager` 交集判定，只要与原生区块有交集即跳过，解决 Z-Fighting 与边缘重叠。
* **修复全屏切换**：补全 `SDL_WINDOW_FULLSCREEN` 标志，完善 Swapchain 重建触发。

### 3. 服务端权威网络背包系统（史诗级）

* **架构合并**：废弃分离的 Hotbar/Inventory，合并为 36 格 `m_inventory`（0-8 为快捷栏）。
* **协议设计**：新增 `ClickContainer`（C->S 意图）与 `InventoryUpdate`（S->C 全量回推含手部物品）。
* **防作弊**：服务端维护权威 `heldItem`，执行严格的左键互换、右键一半、Q 键丢弃逻辑。StateId 校验防止时序错乱。
* **死锁修复**：`HandleClickContainer` 内部通过最小作用域锁只处理数据，释放锁后再发送网络包，根除死锁。

### 4. 存档与光照修复

* **存档锁（心跳机制）**：`WorldSave` 改为存储带毫秒时间戳的 JSON。服务端每秒 `RefreshLock`，启动时 `CleanupStaleLocks` 清理过期锁。彻底解决大退清后台导致的永久锁死。
* **加载存档方块全黑 Bug**：在 `DrainChunkResults` 应用存档方块改变后，立刻调用 `LightEngine::InitRegion` 进行跨区块光照重算。

---

## 四、 🚧 当前状态与下一步计划

### 当前状态

* 桌面端（Vulkan）运行流畅，网络背包、存档锁、LOD 渲染已全部打通。
* 手部第一人称独立通道（Viewmodel Pass）**暂时搁置**，目前的方案仍有裁剪问题。
* 正在移动端真机（华为平板 / 骁龙662手机）进行最终测试，准备发布 **v0.2.0**。

### 下一步计划

* **v0.4.0：轻量级 ECS 架构**（不仅是加猪，还要支持后续怪物、掉落物、工具实体等复杂玩法）。
  * 引入 Sparse Set 架构，摒弃传统上帝类。
  * 组件：`Transform`, `Velocity`, `AABB`, `Health`, `AI`, `Renderable`。
* **v0.5.0：国际化 (i18n)**。
* **v0.6.0：构建系统解耦**（纯 CMake 编译 Android `.so`），RHI 去多态化，光追与 Compute Shader 准备。

---

## 五、 ⚠️ 排坑指南（血泪史）

* **SDL3 触控黑科技**：绝对不要相信 SDL3 在 Android 上的触摸转鼠标事件，坐标会偏。必须在 `onEvent` 里手动把 `tfinger.x * io.DisplaySize.x` 喂给 ImGui。
* **Vulkan 坐标系与近平面**：Vulkan 的 Y 轴与 OpenGL 相反（需 `proj[1][1] *= -1`）。第一人称手部如果穿透 `zNear`（哪怕调得再小），会产生巨大的透视拉伸和撕裂。**如果不用独立通道，手必须退到安全距离（如 Z=-1.0）。**
* **ImGui 生命周期**：没有 `Begin()` 时，**绝对不要**用 `InvisibleButton` 等需要窗口上下文的控件。手动做 `AABB` 点击判定最稳。
* **锁的生命周期**：`std::mutex` 不可重入。在网络回调里，先加锁查数据，释放锁，再发包。
* **Android 杀后台**：华为等系统的清后台是 `SIGKILL`，不会有任何析构。任何防多开的“锁文件”都必须带**心跳时间戳**并在启动时清理。
* **Web 调参利器**：调整第一人称手部/UI 视角时，用 Three.js + lil-gui 写一个网页实时预览工具，能极大地提升调参效率（须注意 C++ 的 zNear 与 WebGL 不同，要手动锁定相机 near 值）。

---

## 六、 🤝 AI 协作约定（最高优先级）

1. **绝对禁止瞎猜**：修改底层逻辑前，必须先索要相关代码和日志。
2. **要完整代码，不要片段**：修改核心函数必须给出完整实现，绝不用 `// 省略` 敷衍。
3. **逐步实施**：每次只解决一个架构痛点，跑通后再继续下一步。
4. **命名精确**：拒绝无意义缩写（用 `currentWindowWidth` 不用 `w`）。
5. **架构优先**：出现 Bug 时，先思考是否是架构分层或生命周期问题，其次再查具体逻辑。
