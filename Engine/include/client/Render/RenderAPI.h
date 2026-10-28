#pragma once

#include "core/Configs.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

struct aiTexture;

namespace Eng::client {

class Window;

/**
 * \brief 不透明的资源句柄。
 * 
 * 引擎底层通过句柄池管理 GPU 资源，上层业务无需关心底层是 OpenGL 的 GLuint
 * 还是 Vulkan 的 VkBuffer/VkImage。句柄为 0 时代表无效资源。
 */
using MeshHandle = uint32_t;
using TextureHandle = uint32_t;
using ShaderHandle = uint32_t;
using FramebufferHandle = uint32_t;

/**
 * \brief 描述渲染后端（API）的可用性状态。
 */
struct BackendAvailability {
    bool available = false;  /**< 是否可用 */
    std::string reason;      /**< 如果不可用，返回具体的错误原因（例如版本不支持、缺少动态库等） */
};

/**
 * \brief 引擎标准的顶点数据格式。
 * 
 * 这是一个强约束的 POD 结构体。底层 OpenGL 和 Vulkan 都必须按照此内存布局
 * 来解析顶点属性（通过 offsetof 映射到 layout location）。
 * 
 * \warning 修改此结构体后，必须同步更新所有顶点着色器以及后端的 Input Layout 配置！
 */
struct Vertex {
    glm::vec3 position;   /**< 顶点在世界/局部空间中的位置 */
    glm::vec3 normal;     /**< 法线，用于光照计算 */
    glm::vec2 uv;         /**< 纹理坐标 (U, V) */
    float     ao = 1.0f;  /**< 环境光遮蔽 (Ambient Occlusion)，范围 0.0 ~ 1.0 */
    
    /**
     * \brief 体素光照数据打包。
     * r 通道：天空光 (Sky Light) 0.0 ~ 1.0
     * g 通道：方块红光 (Block Red) 0.0 ~ 1.0
     * b 通道：方块绿光 (Block Green) 0.0 ~ 1.0
     * a 通道：方块蓝光 (Block Blue) 0.0 ~ 1.0
     */
    glm::vec4 light;      
};

/**
 * \brief 网格数据容器，用于在 CPU 内存中构建 Mesh 并提交给 GPU。
 */
struct MeshData {
    std::vector<Vertex> vertices;   /**< 顶点数组 */
    std::vector<uint32_t> indices;  /**< 索引数组。如果为空，则底层采用 DrawArrays 而非 DrawElements */
};

/**
 * \brief 材质描述。决定物体表面的贴图和高光属性。
 */
struct Material {
    TextureHandle diffuse = 0;   /**< 漫反射贴图句柄 */
    TextureHandle specular = 0;  /**< 高光贴图句柄（预留） */
    float shininess = 32.0f;     /**< 高光锐度，写入 Material UBO */
};

/**
 * \brief 模型中的子网格（用于由 Assimp 加载的复杂模型）。
 */
struct SubMesh {
    MeshHandle mesh = 0;                                /**< 网格句柄 */
    TextureHandle diffuseTexture = 0;                   /**< 该子网格绑定的默认漫反射贴图 */
    glm::mat4 localTransform = glm::mat4(1.0f);         /**< 局部变换矩阵（通常保留为 Identity） */
};

/**
 * \brief 完整的模型结构，包含多个 SubMesh。
 */
struct Model {
    std::vector<SubMesh> subMeshes;

    /** \brief 检查模型是否包含至少一个有效的网格。 */
    bool IsValid() const { return !subMeshes.empty(); }

    /** \brief 快捷获取第一个网格句柄（兼容早期只支持单网格的代码）。 */
    MeshHandle FirstMesh() const {
        return subMeshes.empty() ? 0 : subMeshes[0].mesh;
    }
};

/**
 * \brief 帧缓冲对象（FBO）句柄描述。
 * 
 * 用于离屏渲染（如后处理、第一人称手臂单独通道等）。
 */
struct Framebuffer {
    FramebufferHandle handle = 0; /**< 引擎内部的 FBO 句柄 */
    int  width  = 0;              /**< 宽度（像素） */
    int  height = 0;              /**< 高度（像素） */

    /** \brief 检查 FBO 是否创建成功。 */
    bool isValid() const { return handle != 0; }
};

/**
 * \brief 图形硬件的设备信息快照。
 * 
 * 通常在初始化完成后获取，用于 F3 调试面板展示。
 */
struct DeviceInfo {
    std::string backend = "Unknown";          /**< 图形 API 类型（"OpenGL" / "Vulkan"） */
    std::string deviceName = "Unknown";       /**< 显卡名称 */
    std::string vendor = "Unknown";           /**< 硬件厂商 */
    std::string apiVersion = "Unknown";       /**< API 版本号 */
    std::string driverVersion = "Unknown";    /**< 驱动版本号 */
    std::string shadingLanguage = "Unknown";  /**< 着色器语言版本（GLSL / SPIR-V） */
    std::string extra = "Unknown";            /**< 后备信息串 */
};

// ============ Global UBO (binding = 0) ============
/**
 * \brief 全局 Uniform Buffer 对象的数据布局。
 * 
 * \warning 必须严格遵守 std140 内存对齐规则！更改成员后，必须在下方 `static_assert` 中
 * 同步修改期望大小，否则会导致着色器读取越界或乱码。
 */
struct GlobalUBOData {
    glm::mat4 view;            /**< 视图矩阵 */
    glm::mat4 projection;      /**< 投影矩阵 */
    glm::vec3 viewPos;         /**< 相机世界坐标 */
    float     aoStrength;      /**< 环境光遮蔽强度开关 */
    glm::vec3 lightDir;        /**< 平行光（太阳）方向 */
    float     lightIntensity;  /**< 光照强度 */
    glm::vec3 lightColor;      /**< 光照颜色 */
    float     timeOfDay;       /**< 时间（用于天空盒/昼夜交替） */
    glm::vec3 lightAmbient;    /**< 环境光颜色 */
    float     _pad2;           /**< 补齐 16 字节对齐 */
};
static_assert(sizeof(GlobalUBOData) == 192, "GlobalUBO size mismatch");

// ============ Material UBO (binding = 1) ============
/**
 * \brief 材质 Uniform Buffer 对象的数据布局。
 */
struct MaterialUBOData {
    float shininess;          /**< 高光锐度 */
    float _pad[3];            /**< 补齐 16 字节对齐 */
};
static_assert(sizeof(MaterialUBOData) == 16, "MaterialUBO size mismatch");

/**
 * \brief 渲染清屏标志位。
 * 
 * 按位掩码设计，允许组合使用，例如：`ClearFlag::Color | ClearFlag::Depth`。
 */
enum class ClearFlag : std::uint32_t {
    None    = 0,               /**< 不清理任何内容 */
    Color   = 1 << 0,          /**< 仅清理颜色缓冲 */
    Depth   = 1 << 1,          /**< 仅清理深度缓冲 */
    Stencil = 1 << 2,          /**< 仅清理模板缓冲 */
    All     = Color | Depth | Stencil /**< 清理所有缓冲（默认） */
};

/**
 * \brief 重载位或运算符，以支持 ClearFlag 的组合。
 * \param a 标志 A
 * \param b 标志 B
 * \return 组合后的标志
 */
inline ClearFlag operator|(ClearFlag a, ClearFlag b) {
    return static_cast<ClearFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/**
 * \brief 渲染硬件抽象接口 (Render Hardware Interface, RHI)。
 * 
 * 这是引擎所有图形操作的核心抽象基类。上层业务（如 `MyGame`, `ClientUI`）通过此接口进行
 * 渲染，而**完全不需要关心底层是 OpenGL 还是 Vulkan**。
 * 
 * \note 设计约定：
 * 1. **坐标系**：上层业务统一使用 OpenGL 规范（Y 轴向上，NDC 范围 -1 到 1）。
 *    所有针对 Vulkan 的 Y 轴翻转和 Z 轴 0-1 的映射必须在 Vulkan 后端的 `SetProjectionMatrix` 等接口内部处理。
 * 2. **线程安全**：整个 IRenderAPI 的绝大多数接口**非线程安全**，必须在主渲染线程调用。
 *    但资源销毁接口（如 `DestroyMesh`）可能会内部调用底层等待，允许在同步点调用。
 */
class IRenderAPI {
public:
    virtual ~IRenderAPI() = default;

    /**
     * \brief 初始化渲染器并创建底层上下文/设备。
     * 
     * \param width 初始渲染宽度（通常为窗口像素宽）
     * \param height 初始渲染高度（通常为窗口像素高）
     * \param window 绑定的窗口对象，用于获取原生句柄（如 HWND 或 SDL_Window）
     * 
     * \warning 必须在 Window 创建完成后调用。
     */
    virtual void Initialize(int width, int height, Window* window) = 0;

    /**
     * \brief 销毁渲染器并释放所有 GPU 资源。
     * 
     * \note 内部通常会调用 `WaitIdle()`，等待显卡命令执行完毕，防止 UAF（释放后使用）错误。
     */
    virtual void Shutdown() = 0;

    // ==========================================
    // 视口与清屏
    // ==========================================

    /**
     * \brief 设置当前视口大小。
     * 
     * \param x 视口左下角 X 坐标
     * \param y 视口左下角 Y 坐标
     * \param w 视口宽度
     * \param h 视口高度
     * 
     * \note 如果绑定到了 FBO，通常需要将视口设置为 FBO 的尺寸。
     */
    virtual void SetViewport(int x, int y, int w, int h) = 0;

    /**
     * \brief 设置清屏颜色。
     * \param r 红色分量 0.0 ~ 1.0
     * \param g 绿色分量 0.0 ~ 1.0
     * \param b 蓝色分量 0.0 ~ 1.0
     * \param a 透明分量 0.0 ~ 1.0
     */
    virtual void SetClearColor(float r, float g, float b, float a) = 0;

    /**
     * \brief 根据标志位清理当前绑定的帧缓冲。
     * 
     * \param flag 清理掩码，支持位组合（如 `ClearFlag::Color | ClearFlag::Depth`）。
     * 
     * \note 如果在渲染中途调用此函数（如在渲染手部模型前清理深度），
     * Vulkan 后端会内部使用 `vkCmdClearAttachments` 进行处理，OpenGL 则直接调用 `glClear`。
     */
    virtual void Clear(ClearFlag flag = ClearFlag::All) = 0;

    // ==========================================
    // 矩阵与全局状态设置
    // ==========================================

    /** \brief 设置视图矩阵 (View Matrix) */
    virtual void SetViewMatrix(const glm::mat4& view) = 0;

    /** 
     * \brief 设置投影矩阵 (Projection Matrix)。
     * \warning 上层业务务必传入 OpenGL 规范的投影矩阵。Vulkan 后端会自动内部进行 Y 轴翻转和 Z 轴适配。
     */
    virtual void SetProjectionMatrix(const glm::mat4& proj) = 0;

    /** \brief 设置模型矩阵 (Model Matrix)，在调用 DrawMesh 时生效 */
    virtual void SetModelMatrix(const glm::mat4& model) = 0;

    /** \brief 设置平行光位置/方向 */
    virtual void SetLightPosition(const glm::vec3& pos) = 0;

    /** \brief 设置平行光颜色和强度 */
    virtual void SetLightColor(const glm::vec3& color, const float intensity) = 0;

    /** \brief 设置环境光颜色 */
    virtual void SetLightAmbient(const glm::vec3& amb) = 0;

    /** \brief 设置相机世界位置，用于着色器中的镜面反射计算 */
    virtual void SetViewPosition(const glm::vec3& pos) = 0;

    // ==========================================
    // 资源创建
    // ==========================================

    /** 
     * \brief 从 CPU 内存中创建网格。
     * \param data 包含顶点和索引的网格数据。
     * \return 返回网格句柄，失败返回 0。
     */
    virtual MeshHandle CreateMesh(const MeshData& data) = 0;

    /** 
     * \brief 创建支持实例化渲染的网格。
     * \note 在 Vulkan 下，实例矩阵通常作为一个独立的 VkBuffer（binding = 1）传入。
     */
    virtual MeshHandle CreateMeshInstance(const MeshData& data) = 0;

    /** 
     * \brief 从本地文件加载纹理。
     * \param path 图片文件路径（如 .png, .jpg）。
     * \return 纹理句柄，失败返回 0。
     */
    virtual TextureHandle CreateTexture(const std::string& path) = 0;

    /** 
     * \brief 从 Assimp 的内嵌纹理数据中创建纹理（用于加载嵌入了图片的 .gltf/.obj 模型）。
     * \param embedded Assimp 纹理数据指针。
     * \return 纹理句柄，失败返回 0。
     */
    virtual TextureHandle CreateTextureFromMemory(const aiTexture* embedded) = 0;

    /** 
     * \brief 从原始 RGBA 像素数组创建纹理。
     * \param rgba 像素指针。
     * \param w 图像宽度。
     * \param h 图像高度。
     * \return 纹理句柄，失败返回 0。
     */
    virtual TextureHandle CreateTextureFromPixels(const uint8_t* rgba, int w, int h) = 0;

    /**
     * \brief 编译并链接着色器。
     * \param vertPath 顶点着色器路径（支持 .glsl 文本或 .spv 二进制）。
     * \param fragPath 片元着色器路径。
     * \return 着色器句柄。
     */
    virtual ShaderHandle CreateShader(const std::string& vertPath, const std::string& fragPath) = 0;

    /**
     * \brief 创建天空盒专用着色器。
     * \note 内部会关闭面剔除（CullMode = None）并调整深度测试逻辑。
     */
    virtual ShaderHandle CreateSkybox(const std::string& vertPath, const std::string& fragPath) = 0;

    /**
     * \brief 使用 Assimp 加载模型文件。
     * \param path 模型路径。
     * \param flipUV 是否翻转 UV（针对部分 DirectX 导出的模型）。
     */
    virtual Model LoadModel(const std::string& path, bool flipUV = false) = 0;

    // ==========================================
    // 帧缓冲 (FBO) 管理
    // ==========================================

    /**
     * \brief 创建离屏渲染帧缓冲。
     * 
     * \param width 宽度（像素）
     * \param height 高度（像素）
     * \return 包含句柄的 Framebuffer 对象。
     * 
     * \note 创建的 FBO 会自动附带一张 Color 贴图（可通过 `GetFramebufferTexture` 获取）。
     */
    virtual Framebuffer CreateFramebuffer(int width, int height) = 0;

    /** 
     * \brief 销毁帧缓冲及其关联的附件。
     * \warning 销毁前必须确保 GPU 不再使用该 FBO。底层通常会内部调用 `WaitIdle()` 或 `vkDeviceWaitIdle()`。
     */
    virtual void DestroyFramebuffer(const Framebuffer& fb) = 0;

    /** 
     * \brief 将后续的渲染指令输出到指定的帧缓冲中。
     * \param fb 目标帧缓冲。
     */
    virtual void BindFramebuffer(const Framebuffer& fb) = 0;

    /** 
     * \brief 解绑当前帧缓冲，将渲染目标切换回默认屏幕（Swapchain）。
     */
    virtual void UnbindFramebuffer() = 0;

    /** 
     * \brief 获取 FBO 内部颜色附件的纹理句柄。
     * \param fb 目标帧缓冲。
     * \return 纹理句柄。常用于后处理 Pass 中将离屏画面贴到全屏四边形上。
     */
    virtual TextureHandle GetFramebufferTexture(const Framebuffer& fb) const = 0;

    // ==========================================
    // 资源销毁
    // ==========================================

    /** 
     * \brief 销毁网格资源。
     * \warning 底层 Vulkan 实现必须调用 `vkDeviceWaitIdle`，否则会触发 Validation 层的 UAF 报错。
     */
    virtual void DestroyMesh(MeshHandle handle) = 0;

    /** \brief 销毁纹理资源。 */
    virtual void DestroyTexture(TextureHandle handle) = 0;

    /** \brief 销毁着色器资源。 */
    virtual void DestroyShader(ShaderHandle handle) = 0;

    // ==========================================
    // Uniform 设置
    // ==========================================

    /** \brief 为指定 Shader 设置 mat4 类型的 Uniform。 */
    virtual void SetUniform(ShaderHandle shader, const std::string& name, const glm::mat4& value) = 0;
    
    /** \brief 为指定 Shader 设置 vec3 类型的 Uniform。 */
    virtual void SetUniform(ShaderHandle shader, const std::string& name, const glm::vec3& value) = 0;
    
    /** \brief 为指定 Shader 设置 float 类型的 Uniform。 */
    virtual void SetUniform(ShaderHandle shader, const std::string& name, float value) = 0;
    
    /** \brief 为指定 Shader 设置 int 类型的 Uniform（通常用于纹理槽位 sampler2D）。 */
    virtual void SetUniform(ShaderHandle shader, const std::string& name, int value) = 0;

    // ==========================================
    // 渲染绘制核心
    // ==========================================

    /**
     * \brief 准备开始新的一帧。
     * 
     * \note 内部会处理 Vulkan 的 acquireNextImage 以及 CommandBuffer 的 Begin。
     */
    virtual void BeginFrame() = 0;

    /** 
     * \brief 绘制单个网格。
     * \param mesh 网格句柄。
     * \param shader 使用的着色器。
     * \param material 材质（纹理绑定在这里生效）。
     */
    virtual void DrawMesh(MeshHandle mesh, ShaderHandle shader, const Material& material) = 0;

    /** 
     * \brief 实例化绘制（适合绘制大量形状相同但位置不同的物体，如掉落物）。
     * \param mesh 网格句柄。
     * \param shader 使用的着色器。
     * \param material 材质。
     * \param transforms 实例化矩阵列表。
     */
    virtual void DrawMeshInstanced(MeshHandle mesh,
                                    ShaderHandle shader,
                                    const Material& material,
                                    const std::vector<glm::mat4>& transforms) = 0;

    /** 
     * \brief 合批绘制（适合地形区块）。
     * 
     * \param meshes 网格句柄列表。
     * \param shader 使用的着色器。
     * \param mat 共享材质。
     * 
     * \note 相比循环调用 DrawMesh，合批绘制能显著减少状态切换的开销。
     */
    virtual void DrawMeshBatch(const std::vector<MeshHandle>& meshes, ShaderHandle shader, const Material& mat) = 0;

    /**
     * \brief 结束当前帧渲染，并提交到屏幕或 Swapchain。
     * 
     * \note 内部会处理 Vulkan 的 submit 和 present。
     */
    virtual void EndFrame() = 0;

    /**
     * \brief 绘制天空盒。
     * \param shader 天空盒专用着色器。
     * \note 通常在渲染流程的最后阶段调用，并关闭深度写入。
     */
    virtual void DrawSkybox(ShaderHandle shader) = 0;

    /**
     * \brief 绘制全屏四边形（通常用于后处理，如将 FBO 贴图渲染到屏幕）。
     * \param textureID 要贴到全屏的纹理。
     */
    virtual void DrawFullscreenQuad(TextureHandle textureID) = 0;

    // ==========================================
    // ImGui 后端接口
    // ==========================================

    /** 
     * \brief 初始化 ImGui 的渲染后端。
     * \note 调用方负责 ImGui::CreateContext() 和 ImGui_ImplSDL3_InitXXX()。此接口只负责渲染器相关初始化。
     */
    virtual bool InitImGuiBackend() = 0;

    /** 
     * \brief 关闭 ImGui 渲染后端。
     */
    virtual void ShutdownImGuiBackend() = 0;

    /** 
     * \brief ImGui 新帧准备。
     * \note 内部通常调用 ImGui_ImplXxx_NewFrame()。
     */
    virtual void ImGuiNewFrame() = 0;

    /** 
     * \brief ImGui 数据渲染。
     * \note 内部通常调用 ImGui_ImplXxx_RenderDrawData()。
     */
    virtual void ImGuiRenderDrawData() = 0;

    // ==========================================
    // 其他
    // ==========================================

    /** 
     * \brief 获取当前图形后端的设备信息。
     * \return 包含显卡型号、驱动版本等信息的结构体。
     */
    [[nodiscard]] virtual DeviceInfo GetDeviceInfo() const = 0;

    /** 
     * \brief 应用窗口和渲染设置（如全屏、VSync、AO 强度）。
     */
    virtual void ApplySettings(const WindowConfig& win, const RenderConfig& render) = 0;

    /** 
     * \brief 等待 GPU 完成所有排队的渲染命令。
     * 
     * \note 极其重要：在销毁任何正在被 GPU 使用的资源，或关闭窗口前，必须调用此函数。
     */
    virtual void WaitIdle() = 0;
};

}