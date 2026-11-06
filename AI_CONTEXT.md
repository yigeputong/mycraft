# Mycraft 引擎开发 — AI 上下文恢复与架构规范 (V3)

> **本文档是 AI 开发协作的上下文恢复入口。**
> 每次开始新对话时，AI 必须完整阅读本文档以恢复项目状态。

---

## 一、 项目概况与技术基线

**Mycraft**：Minecraft 风格体素游戏 + 自制体素专用引擎。
**技术栈**：C++23 + CMake + Ninja + vcpkg。桌面端为 `clang-cl`，移动端为 `NDK Clang` (NDK 30.0.16248370)。

### 🎯 平台与 API 基线

* **桌面端**：Windows 10+，Vulkan 1.3（主力，Dynamic Rendering, Synchronization2），OpenGL 4.6（兼容兜底）。
* **移动端**：Android 8 (API 26)+，Vulkan 1.3，OpenGL ES 3.1（未来兜底）。
* **UI 框架**：Dear ImGui `v1.93.0`（已适配动态字体、新 DrawList、以及去除了默认 +0.5f 偏移）。

---

## 二、 🚨 核心铁律与架构原则（最高优先级）

1. **平台绝对隔离**：业务层和引擎层严禁出现 `#ifdef __ANDROID__` / `_WIN32`。所有差异必须下沉到 `Platform` 或 `FileTool`。
2. **RHI 纯粹去脑化**：`IRenderService` 只做无状态指令下发，严禁懂 Mesh/Material 等业务概念。
3. **依赖显式注入 (DI)**：业务层需要什么，就显式调 `engine.CreateXXXService()`。
4. **彻底 RAII**：严禁裸 `new/delete` 和裸 `std::thread`。资源生命周期绑定到 `std::shared_ptr`。
5. **拒绝魔法数字**：尺寸、阈值全用 `constexpr` 具名常量。

---

## 三、 ✅ 近期重大里程碑（已完成）

1. **渲染架构蜕变**：Texture Array 彻底替代 Atlas。支持 `arrayLayers`，根除远景渗色。
2. **输入系统重构 (`InputService`)**：
   * 废除单例，改为 `Engine` 持有，通过构造注入 `WindowService`。
   * 引入 Action 注册表（`ActionHash`），支持多键绑定与映射。
   * 支持 `InjectActionState` 和 `InjectMoveDir` 接口，用于虚拟触控。
3. **移动端构建踩坑记录（血泪史）**：
   * `vcpkg` 交叉编译 Android 依赖时极易因 GitHub 网络被墙导致 `curl 56` 错误或死锁。
   * **对策**：若卡在某个包下载失败，使用 `curl.exe -L -o "C:\Dev\vcpkg\downloads\<文件名>.tar.gz" "<代理链接>"` 手动下载并放入缓存目录。
4. **移动端虚拟 UI 基础**：
   * 实现了数据驱动的拖拽布局（`m_virtualButtons` + `m_joystickBasePos`）。
   * 通过 `ImGui::InvisibleButton` 接管拖拽。

---

## 四、 🚧 当前正在处理：移动端真机触控 Bug 修复

在真机（Android）上测试时，遇到了两个核心 Bug，需优先排查：

### 🐛 Bug 1：全屏乱触发“挖掘”动画

* **现象**：无论点击屏幕的哪里（包括摇杆区域、空白区域），手部都会出现挖掘（Dig）动作，但实际未必挖到了方块。
* **可能原因**：
    1. 虚拟按键 `m_virtualButtons` 中“挖”的判定范围异常（半径过大）。
    2. `ProcessTouchInputAndDrawOverlay` 里判断 `consumedTouches` 有误，导致手指没落在按钮上时，误触发了 `InjectActionState(Dig, true)`。
    3. 触摸点坐标计算（从归一化转像素）存在偏差。

### 🐛 Bug 2：飞行模式下无法下降

* **现象**：手机端似乎没有“下降”的按钮（FlyDown），或者绑定失效。人物在飞行模式下无法下降。
* **可能原因**：
    1. 移动端 UI 布局中没有给 `FlyDown` 配置虚拟按键。
    2. `m_flyMode` 下 `IsActionDown(A_FlyDown)` 逻辑未触发。

---

## 五、 📋 未来计划

* **v0.4.0：轻量级 ECS 架构**
  * 放弃传统的“上帝类”，引入 Sparse Set 架构。
  * 组件设计：`Transform`, `Velocity`, `AABB`, `Health`, `PigAI`, `Renderable`。
  * **测试目标**：在世界上生成几头“猪”，拥有漫游 AI、受重力掉落、腿部摆动动画（Minecraft 风格的拆解 Mesh 拼装）。
* **v0.5.0：国际化 (i18n)**
  * 配置 `lang/en_us.json`、`zh_cn.json`。
  * 所有的 UI 文本、虚拟按键标签全部转为 `i18n::Get("key")`。
* **v0.6.0：深度架构重构**
  * 构建系统解耦：写一个纯 CMake 脚本直接编译 Android `.so`，剥离 Gradle 的 `externalNativeBuild` 监控。
  * RHI 去多态化探索（模板/宏替换纯虚函数）。
  * 光追与 PBR 管线准备（全面转向 Compute Shader 驱动光照）。

---

## 六、 ⚠️ 排坑指南（血泪史补充）

* **ImGui 1.93.0 升级警告**：新版移除了 `AddLine` 内部默认的 `+0.5f` 偏移。手绘 UI 需注意检查边框和准星是否模糊。
* **Android 资源剥离**：`AssetService::ReadFile` 中需利用 `IsMobilePlatform()` 剥离 `assets/` 前缀。
* **Vulkan 验证层**：Android 端无 Validation Layer，编译期需用 `#if !defined(__ANDROID__)` 屏蔽。
* **纹理数组的 UI 限制**：ImGui 后端不支持 `sampler2DArray`，目前 HUD 的方块图标暂时用纯色等距方块渲染。
* **VMA 配置**：`VMA_DYNAMIC_VULKAN_FUNCTIONS=1`，初始化时只需传 `vkGetInstanceProcAddr` 和 `vkGetDeviceProcAddr`。

---

## 七、 🤝 AI 协作约定（最高优先级）

1. **绝对禁止瞎猜**：修改底层逻辑前，必须先索要相关代码和日志。
2. **要完整代码，不要片段**：修改核心函数必须给出完整实现，绝不用 `// 省略` 敷衍。
3. **逐步实施**：每次只解决一个架构痛点，跑通后再继续下一步。
4. **命名精确**：拒绝无意义缩写（用 `currentWindowWidth` 不用 `w`）。
