# Mycraft 引擎开发 — AI 上下文恢复与架构规范 (V4)

> **每次新对话开始，AI 必须完整阅读本文档以恢复项目状态。**

---

## 零、文档使用说明与冲突裁决

### 0.1 阅读顺序

1. **第一章～第三章**：项目基线、铁律、开发者偏好 —— 最高优先级，必须遵守。
2. **第四章～第八章**：当前架构、已完成、正在处理、下一步 —— 当前事实。
3. **第九章**：排坑大全 —— 遇到问题先查这里。
4. **第十章**：AI 协作约定 —— 最高行为准则。

### 0.2 冲突裁决表（已确认）

| 项目 | 裁决 |
| --- | --- |
| C++ 标准 | **C++23** (暂不折腾 C++26 反射) |
| 桌面编译器 | **clang-cl 主力**；MSVC / Linux GCC 兼容；**MinGW 已移除** |
| 移动编译器 | **NDK Clang** (NDK 30.0.16248370) |
| 依赖链接 | **静态链接**（`/MT`，无 DLL 依赖） |
| 图形 API | **Vulkan 1.3** (主力) / **OpenGL 4.6** (兜底) |
| 目标平台 | **Windows + Android**；Linux 兼容 |
| 渲染架构 | **RHI 无状态 + SceneRenderer + MDI + Texture Array** |
| 模型/动画 | **Assimp 加载 + 纯 C++ ModelNode 层级刚体 + 关键帧插值** |

---

## 一、项目概况与平台基线

**Mycraft**：Minecraft 风格体素游戏 + 自制体素专用引擎。目标是先做一个“能玩”的体素游戏，用游戏驱动引擎迭代，最终把引擎剥离成独立的项目，用于开发**体素风格 FPS**。

### 1.1 技术栈

* **语言**：C++23
* **构建**：CMake + Ninja Multi-Config + vcpkg（静态链接 `/MT`）
* **图形**：SDL3、volk、Vulkan 1.3 (Dynamic Rendering, Sync2)、OpenGL 4.6 (DSA)、glm、ImGui (vendored v1.93.0)、assimp
* **当前状态**：v0.4.0 核心重构阶段。客户端与服务端已全面完成 ECS 化（实体、系统、区块），彻底剥离了实体管理中的上帝类逻辑。

---

## 二、🚨 核心铁律与架构原则（最高优先级）

### 铁律 1：现代 C++ 与系统交互

* **全面拥抱现代 C++**：数据传 `std::span`，字符串用 `std::string_view`，资源用 `std::unique_ptr`/`shared_ptr`。
* **并发编程**：禁止裸 `std::thread`，统一使用 `Eng::tool::RAIIThread`。
* **平台隔离**：所有 `<windows.h>` 锁死在 `Platform.cpp`；业务层禁止 `#ifdef _WIN32`。
* **API 选型**：`SDL3` > `标准库` > `系统原生 API`。

### 铁律 2：命名与所有权三定律

1. **Service（`*Service`）**：全局唯一，跟 `Engine` 同寿，被动调用（如 `AssetService`, `IRenderService`）。
2. **Manager（`*Manager`）**：业务子系统，跟 Game 同寿，主动运行（有 Update / 后台线程）。如 `MeshManager`, `LODManager`。
3. **Tool（无后缀）**：纯工具类，函数式调用，栈对象或静态单例（如 `FileTool`, `MessageWriter`, `RAIIThread`）。

### 铁律 3：RHI 纯粹去脑化与 ECS 渲染分层

* **RHI 必须无状态**：`IRenderService` 只负责下发指令，严禁包含 `Mesh`、`Material` 等业务语义。
* **渲染大脑归 SceneRenderer**：视锥剔除、MDI 组装、大 VBO 生命周期、阴影 Pass，全部在 `SceneRenderer` 完成。
* **渲染数据由 ECS 驱动**：`RenderSystem` 只负责从 ECS 遍历实体并组装 `RenderContext`，最终提交给 `SceneRenderer` 统一绘制。

### 铁律 4：RAII 与延迟销毁（补充死锁规范）

* **资源自治**：业务层用完 `BufferPtr` 直接销毁，绝不允许手动 `WaitIdle`。
* **锁粒度纪律**：绝对禁止在持有 `std::mutex` 的状态下进行网络发包（如 `m_server->SendTo`）、读写磁盘、或者调用可能再次加锁的公共函数，否则必然导致 `_RESOURCE_DEADLOCK_WOULD_OCCUR` 死锁。

### 铁律 5：数据与确定性

* **跨平台一致**：世界生成严禁使用 `std::mt19937`，必须使用 `xorshift32`，保证 PC 与手机同种子同结果。

### 铁律 6：文件 I/O 与日志

* **文件**：全面禁止 `std::fstream` / `std::filesystem`。统一使用 `FileTool` (基于 `SDL_IOStream`)。
* **日志**：流式宏 `logInfo(logger, "[Tag] Value: " << val)`，禁用 `std::print`，统一走 `SDL_Log`。

### 铁律 7：开发者个人偏好

* **拒绝魔法数字**：全部提取为 `constexpr`。
* **变量命名精确**：`cx` 必须写成 `playerChunkX`，UI 尺寸需基于 `ImGui::GetFontSize()` 动态计算。
* **纯 CLI 极客流**：VSCode + 终端完成一切，排斥 Android Studio 臃肿。
* **要完整代码**：绝不用 `// 省略` 敷衍，修改核心函数必须给出完整实现。

---

## 三、✅ 当前架构现状 (v0.4.0-dev)

### 3.1 ECS 架构（引擎容器 + 业务实体）

* **引擎层 `Engine/ecs`**：只认识"实体存储"与"系统接口"，不认识任何组件。
  * `EntityId` / `INVALID_ENTITY` / `MakeEntityId(index, generation)`。
  * `EntityStore<EntityT>`：`std::deque` + generation + 空闲槽 LIFO 复用；`DestroyEntity` 立即调 `ResetComponents()` 释放大对象；新槽 generation 从 1 起（0 保留给 `INVALID_ENTITY`）。
  * `ISystemT<WorldT, TickContextT>`：只有 `Update`，**没有 Render**（渲染有独立上下文与时机）。
  * `SystemScheduler<WorldT, TickContextT>`：按注册顺序线性调度，不做依赖图。
* **业务层双实体**：`ClientEntity`（player / renderable / chunkCoord / chunkData）与 `ServerEntity`（serverPlayer）共同继承 `EntityCommon`（transform / velocity / aabb / pigAI / item / netInterp），各自实现 `ResetComponents` / `IsPristine`。服务端那份**物理上没有渲染字段**，裁剪靠实体定义而不是宏。
* **系统**：`ItemSystem` / `PigAISystem` / `PhysicsSystem` 为 header-only 模板（`template <typename WorldT>`），两端共用；`AnimationSystem` 与 `RenderSystem` 只在客户端注册，服务端不编译。
* **服务端同构**：`GameServer` 基于 `ServerEntityManager` 运行，`ServerPlayerComponent` 统一管理网络 ID、背包、权威位置。

### 3.2 区块 (Chunk) 全面 ECS 化

* **组件化存储**：`ChunkCoordComponent` 与 `ChunkComponent` 正式接管区块。区块不再散落在各个 Map 中，成为 ECS 实体。
* **生命周期驱动**：视距外区块卸载、脏标记（`isMeshDirty`）重构 Mesh，全部在主循环通过 ECS 遍历直接驱动。
* **世界查询**：`GetBlockAt` 改为通过 `m_chunkToEcsMap` 直接定位 ECS 实体并读取 `chunkData->data`。

### 3.3 地形生成 (深度对齐原版)

* **地形形状**：`WorldGenerator` 和 `DensityFunction` 实现了原版的 `sloped_cheese` 密度树、`TerrainProvider` 样条及 `PeaksAndValleys` 折返算法。
* **生物群系**：移植 `OverworldBiomeBuilder`，基于 6 维气候参数（温度/湿度/侵蚀/怪异/大陆/深度）的查表映射。
* **地表规则**：引入了基岩、深板岩、砂岩等分层，表层方块动态变化（不再生硬的一刀切）。

### 3.4 光照引擎 (LightEngine)

* **光路分离**：天空光（Sky）与方块光（Block RGB）使用双队列独立扩散，彻底消除神秘光源。
* **垂直直射不衰减**：天光（SkyLight）在垂直向下传播时保持满值（15），修复洞穴死黑。
* **环境光保底**：`MeshManager` 组装顶点时加入 `AMBIENT_FLOOR = 0.1f` 防死黑。

### 3.5 ECS 已提升至引擎层（v0.4.0 完成）

* 纯容器与系统接口已搬入 `Engine/ecs`，业务层通过 `EntityStore<ClientEntity>` / `EntityStore<ServerEntity>` 实例化。
* 客户端与服务端各自持有实体注册表，共享同一套存储算法与调度器。
* 组件裁剪为编译期完成：服务端实体定义里不存在 renderable / chunkCoord / chunkData / chunkRender。
* 遗留问题（v0.6.0 收敛）：区块状态仍有两个记录者——`ClientWorld::m_outstanding` 记录"请求在途"，ECS 的 `chunkData` 记录"数据可读"。唯一事实来源必须是 ECS。

---

## 四、📋 路线图

### 4.1 v0.4.0：轻量级 ECS 与业务层覆盖

* ✅ 在 `Engine` 或 `game` 层实现 ECS 架构，组件抽取（Transform, Velocity, AABB, Player, Chunk 等）。
* ✅ 业务层迁移：猪、掉落物、玩家、区块统一纳入 ECS。
* ✅ 客户端与服务端的系统分离，构建 `SystemContext` 驱动。
* ✅ ECS 容器与系统接口提升至 `Engine/ecs`，客户端/服务端实体定义分离。

### 4.2 v0.5.0：i18n 与全面数据驱动

* **国际化**：配置 `lang/en_us.json`、`zh_cn.json`，UI 文本全面接入 i18n。
* **数据驱动**：将方块、物品、配方、实体的属性等彻底外置为 JSON 配置，实现“添加内容不需重编译代码”。

### 4.3 v0.6.0：深度重构（构建与渲染）

* **构建解耦**：抛弃 Gradle 的 `externalNativeBuild`，写纯 CMake 脚本直接编译 Android `.so`。
* **RHI 去多态化**：探索使用模板/宏替换纯虚函数，榨干性能。
* **代码清理**：彻底解决历史遗留的上帝类和耦合点。

### 4.4 v1.0.0+：高级图形特性探索

* 光线追踪（Ray Tracing）。
* PBR（基于物理的渲染）。
* DLSS / FSR 等超分辨率技术。

### 4.5 未来：剥离引擎（终极目标）

* 把 `Engine` 从 `Mycraft` 中完全独立出去，成为一个真正的通用体素引擎。
* 用它开发一款属于自己的 **体素风格 FPS**（强调枪械手感、射线判定、网络低延迟）。

---

## 五、⚠️ 排坑大全（极简硬核版）

### 图形与 RHI (Vulkan/OpenGL)

* **Vulkan 坐标**：必须翻转 Y 轴 (`proj[1][1] *= -1`) 且深度映射至 `[0,1]`。
* **VMA 内存**：分配的资源不要走 `vk::raii`，且写入后必须 `vmaFlushAllocation`。
* **ImGui RenderPass**：Vulkan 下必须单独开启无 Depth 的 RenderPass，否则报错。
* **OpenGL 状态残留**：画完水（透明）或天空盒后，必须强行重置 `glDepthMask` 防画面撕裂。
* **MDI 遍历顺序**：全局 Buffer 重建时，必须在同一帧内完成，防止偏移量错乱导致花屏。
* **纹理数组**：旧版 Texture Atlas 会导致远景渗色；ImGui 后端不支持 `sampler2DArray`，UI 需另建 2D 纹理。

### 渲染逻辑与物理

* **面剔除过度**：树叶和水不能互相剔除，否则会掏空；火把必须排除在物理碰撞之外，否则玩家会被草卡住。
* **坐标原点**：玩家 `position` 是脚底，渲染需加 `PLAYER_EYE`；掉落物是中心，半高为 0.15。
* **区块截断**：曾因客户端与服务端 `CHUNK_SIZE_Y` 宏定义不一致，导致收到的区块数据缺失。

### 多线程与内存

* **栈溢出**：`Chunk` 约 320KB，多线程任务中必须用 `unique_ptr` 在堆上分配副本。
* **Mesh 覆盖**：多线程异步建 Mesh 极易发生旧数据覆盖新数据，必须引入 `version` 版本号校验。
* **线程安全**：`std::cout` 不会自动 flush，多线程必丢日志；`current_zone()` 首次调用非线程安全需静态缓存。

### SDL3 与 Android 移动端

* **触摸坐标**：SDL3 自带的触摸转鼠标事件在安卓上会坐标错位，必须拦截原生 `SDL_EVENT_FINGER_*` 手动喂给 ImGui。
* **路径灾难**：NDK 对反斜杠极其敏感，所有配置文件（如 `local.properties`）必须强制正斜杠 `/`。
* **符号缺失**：Android 必须强制 `SDL_ICONV=1` 且定义 `IMGUI_IMPL_OPENGL_ES3`。
* **API 变更**：SDL3 的 `SDL_net` 参数变多，全屏切换不能用 `SetWindowSize(0,0)`。
* **网络被墙**：vcpkg 交叉编译时从 GitHub 下载容易卡死，需手动 curl 下载放入缓存目录。

### 模型与 Blender

* **矩阵转置**：Assimp 是行主序，glm 是列主序，C++ 加载时必须 `glm::transpose`。
* **坐标系**：Blender 导出 glTF 必须勾选 `+Y Up`，否则模型躺着。
* **动画卡顿**：K 帧时插值必须选“线性”，平滑插值会导致循环首尾帧速度归零而卡顿。

### 光照与渲染逻辑

* **光照拆分**：天空光与方块光必须分列 BFS 队列，绝不能用 `(r==15&&g==15&&b==15)` 来判断是否为天空光，否则光强衰减后会退化为错误颜色。
* **区块 AABB**：`Chunk` 结构中没有 `aabbMin/Max`，必须在 `DrainMeshResults` 中根据 `cx, cz` 实时算出包围盒再提交给渲染器。

### ECS 与内存安全

* **指针失效**：`EntityStore` 底层是 `std::deque`，`CreateEntity` 不会让已有实体的引用失效，但**迭代器全部失效**，遍历过程中创建实体依然禁止。`GetEntity` 返回的指针在 `DestroyEntity` 后立即失效（槽位已被 `ResetComponents` 清空）。
* **死锁陷阱**：调用 `SendInventoryUpdate`、`SpawnItemDrop` 等涉及网络广播或跨系统的函数时，必须确保 `m_worldMutex` 没有在当前线程被重复上锁。

---

## 六、🤝 AI 协作约定（最高优先级）

1. **绝对禁止瞎猜**：修改底层逻辑前，必须先索要相关代码和日志。严禁“大概率是某某问题”的乱改。
2. **要完整代码，不要片段**：修改核心函数必须给出完整实现，绝不用 `// 省略` 敷衍。
3. **逐步实施**：每次只解决一个架构痛点，跑通后再继续下一步。
4. **命名精确**：拒绝无意义缩写（比如用 `currentWindowWidth` 不用 `w`）。
5. **架构优先**：出现 Bug 时，先思考是否是架构分层或生命周期问题，其次再查具体逻辑。

---

## 七、快速参考

### 7.1 一句话唤醒
>
> "继续 Mycraft：当前 v0.4.0-dev，核心架构已全面完成 ECS 化（玩家、掉落物、区块均已作为实体管理，系统逻辑完全独立）。下一步：v0.5.0 i18n + 数据驱动"
