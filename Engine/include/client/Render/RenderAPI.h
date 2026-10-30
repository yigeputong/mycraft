#pragma once

#include "core/Configs.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace Eng::client {

class Window;

// ==================== 渲染状态与格式枚举 ====================
/**
 * \brief 渲染硬件状态定义集合。
 * 
 * 将相关的枚举统一收拢在 rhi (Render Hardware Interface) 命名空间下，
 * 避免全局命名空间污染，同时方便底层统一进行枚举映射。
 */
namespace rhi {

/** \brief 纹理与帧缓冲的像素格式 */
enum class TextureFormat : uint8_t {
    RGBA8_UNORM,    /**< 常规 8 位颜色（常规屏幕渲染） */
    RGBA16_SFLOAT,  /**< HDR 浮点颜色（泛光、PBR 高光） */
    R8_UNORM,       /**< 单通道 8 位（阴影图、灰度图） */
    D32_SFLOAT      /**< 深度图 */
};

/** \brief 混合模式 */
enum class BlendMode : uint8_t {
    None,           /**< 不混合（实体方块） */
    AlphaBlend,     /**< 常规透明（水、玻璃） */
    Additive        /**< 发光叠加（粒子、光效） */
};

/** \brief 面剔除模式 */
enum class CullMode : uint8_t {
    Back,           /**< 背面剔除（默认） */
    Front,          /**< 正面剔除 */
    None            /**< 不剔除（树叶、植物、UI） */
};

/** \brief 深度测试模式 */
enum class DepthMode : uint8_t {
    Normal,         /**< 开启测试并写入深度 */
    TestOnly,       /**< 开启测试不写深度（半透明） */
    Disabled        /**< 彻底关闭深度（UI、天空盒） */
};

/** \brief 清屏标志位。 */
enum class ClearFlag : std::uint8_t {
    None    = 0,
    Color   = 1 << 0,
    Depth   = 1 << 1,
    Stencil = 1 << 2,
    All     = Color | Depth | Stencil
};

inline ClearFlag operator|(ClearFlag a, ClearFlag b) {
    // 内部计算时允许整型提升，最后强转回 uint8_t
    return static_cast<ClearFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

} // namespace rhi


// ==================== 底层不透明句柄（业务层不可见） ====================
/**
 * \brief 底层内部资源句柄。
 * 
 * \note 仅供引擎底层（OpenGL/Vulkan）进行资源池索引使用。
 * 业务层代码**绝不**应该接触这些类型，请全程使用下方的智能指针封装。
 */
using MeshHandle        = uint32_t;
using TextureHandle     = uint32_t;
using ShaderHandle      = uint32_t;
using FramebufferHandle = uint32_t;

// ==================== 资源智能指针前置声明 ====================
class Mesh;
class Texture;
class Shader;
class Framebuffer;

/** \brief 带自动生命周期管理的网格智能指针。 */
using MeshPtr        = std::shared_ptr<Mesh>;
/** \brief 带自动生命周期管理的纹理智能指针。 */
using TexturePtr     = std::shared_ptr<Texture>;
/** \brief 带自动生命周期管理的着色器智能指针。 */
using ShaderPtr      = std::shared_ptr<Shader>;
/** \brief 带自动生命周期管理的帧缓冲智能指针。 */
using FramebufferPtr = std::shared_ptr<Framebuffer>;


// ==================== 资源封装对象 ====================
/**
 * \brief 网格资源封装类。
 * 
 * 当 `MeshPtr` 的引用计数归零时，底层引擎会自动调用销毁函数（如 `glDeleteBuffers` 或 `vkDestroyBuffer`）。
 * 业务层只需持有此指针，无需手动释放。
 */
class Mesh {
public:
    explicit Mesh(MeshHandle h) : m_handle(h) {}
    ~Mesh() = default;
    
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    [[nodiscard]] MeshHandle GetHandle() const { return m_handle; }
private:
    MeshHandle m_handle;
};

/**
 * \brief 纹理资源封装类。
 */
class Texture {
public:
    explicit Texture(TextureHandle h) : m_handle(h) {}
    ~Texture() = default;
    
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    [[nodiscard]] TextureHandle GetHandle() const { return m_handle; }
private:
    TextureHandle m_handle;
};

/**
 * \brief 着色器资源封装类。
 */
class Shader {
public:
    explicit Shader(ShaderHandle h) : m_handle(h) {}
    ~Shader() = default;
    
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    [[nodiscard]] ShaderHandle GetHandle() const { return m_handle; }
private:
    ShaderHandle m_handle;
};

/**
 * \brief 帧缓冲资源封装类。
 */
class Framebuffer {
public:
    explicit Framebuffer(FramebufferHandle h, TexturePtr colorTex, int width, int height)
        : m_handle(h), m_colorTexture(std::move(colorTex)), m_width(width), m_height(height) {}
    ~Framebuffer() = default;
    
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    /** \brief 获取底层的颜色附件纹理（通常用于后处理全屏绘制）。 */
    [[nodiscard]] TexturePtr GetColorTexture() const { return m_colorTexture; }
    [[nodiscard]] int GetWidth()  const { return m_width; }
    [[nodiscard]] int GetHeight() const { return m_height; }
    [[nodiscard]] FramebufferHandle GetHandle() const { return m_handle; }
private:
    FramebufferHandle m_handle;
    TexturePtr        m_colorTexture;
    int               m_width;
    int               m_height;
};


// ==================== 数据结构 ====================
/**
 * \brief 引擎标准的顶点数据格式。
 * \warning 修改此结构体后，必须同步更新底层的顶点输入布局（Vertex Input Layout）。
 */
struct Vertex {
    glm::vec3 position;   /**< 位置 */
    glm::vec3 normal;     /**< 法线 */
    glm::vec2 uv;         /**< 纹理坐标 */
    float     ao = 1.0f;  /**< 环境光遮蔽 */
    glm::vec4 light;      /**< 体素光照 (R: 天空光, GBA: 方块RGB光) */
};

/** \brief 网格数据，用于提交给 GPU 生成资源。 */
struct MeshData {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
};

/** \brief 渲染材质。 */
struct Material {
    TexturePtr diffuse = nullptr;   /**< 漫反射贴图 */
    TexturePtr specular = nullptr;  /**< 高光贴图 */
    float shininess = 32.0f;        /**< 高光强度 */
};

/** \brief 模型子网格。 */
struct SubMesh {
    MeshPtr    mesh = nullptr;
    TexturePtr diffuseTexture = nullptr;
    glm::mat4  localTransform = glm::mat4(1.0f);
};

/** \brief 完整模型（包含多个子网格）。 */
struct Model {
    std::vector<SubMesh> subMeshes;
    bool IsValid() const { return !subMeshes.empty(); }
};

/** \brief 描述图形后端的可用性探测结果。 */
struct BackendAvailability {
    bool available = false;  /**< 是否可用 */
    std::string reason;      /**< 不可用的原因（如缺少动态库、版本太低等） */
};

/** \brief 图形硬件的设备信息快照。 */
struct DeviceInfo {
    std::string backend = "Unknown";          /**< "OpenGL" / "Vulkan" */
    std::string deviceName = "Unknown";       /**< 显卡名称 */
    std::string vendor = "Unknown";           /**< 硬件厂商 */
    std::string apiVersion = "Unknown";       /**< API 版本号 */
    std::string driverVersion = "Unknown";    /**< 驱动版本号 */
    std::string shadingLanguage = "Unknown";  /**< 着色器语言版本 */
    std::string extra = "Unknown";            /**< 后备信息串 */
};

/** \brief 全局渲染状态（通过 UBO 一次性提交给 GPU）。 */
struct GlobalState {
    glm::mat4 view = glm::mat4(1.0f);                   /**< 视图矩阵 */
    glm::mat4 projection = glm::mat4(1.0f);             /**< 投影矩阵 */
    glm::vec3 viewPos = glm::vec3(0.0f);                /**< 相机世界坐标 */
    float     aoStrength = 1.0f;                        /**< AO 强度 */
    glm::vec3 lightDir = glm::vec3(0.5f, -1.0f, 0.3f);  /**< 平行光方向 */
    float     lightIntensity = 1.0f;                    /**< 平行光强度 */
    glm::vec3 lightColor = glm::vec3(1.0f);             /**< 平行光颜色 */
    float     timeOfDay = 8.0f;                         /**< 时间（用于昼夜交替） */
    glm::vec3 lightAmbient = glm::vec3(0.3f);           /**< 环境光颜色 */
    float     _pad2 = 0.0f;                             /**< 补齐 16 字节对齐（UBO 必须） */
};

/** \brief 着色器创建配置。支持预编译宏与渲染状态定制。 */
struct ShaderCreateInfo {
    std::string vertPath;
    std::string fragPath;
    std::string vertEntry = "main";          /**< SPIR-V 入口点 */
    std::string fragEntry = "main";          /**< SPIR-V 入口点 */
    
    rhi::TextureFormat colorFormat = rhi::TextureFormat::RGBA8_UNORM; /**< 输出目标格式 */
    rhi::CullMode      cullMode    = rhi::CullMode::Back;             /**< 面剔除模式 */
    rhi::DepthMode     depthMode   = rhi::DepthMode::Normal;          /**< 深度测试模式 */
    rhi::BlendMode     blendMode   = rhi::BlendMode::None;            /**< 混合模式 */

    bool useInstancing = true;

    std::vector<std::pair<std::string, std::string>> defines; /**< 预编译宏 */
};

/** \brief 帧缓冲创建配置。 */
struct FramebufferCreateInfo {
    int  width = 0;
    int  height = 0;
    bool hasDepth = true;                                   /**< 是否创建深度附件 */
    rhi::TextureFormat colorFormat = rhi::TextureFormat::RGBA8_UNORM; /**< 颜色附件格式 */
};


// ==================== 核心 RHI 接口 ====================
/**
 * \brief 渲染硬件抽象接口 (Render Hardware Interface)。
 * 
 * 提供了对底层图形 API（OpenGL/Vulkan）的统一抽象。
 * 上层业务通过 `Create*` 系列接口获取带生命周期管理的智能指针，
 * 通过 `Draw*` 系列接口执行绘制。无需关心底层的句柄与销毁。
 * 
 * \note 设计约定：
 * 1. **坐标系**：上层业务统一使用 OpenGL 规范（Y 轴向上，NDC 范围 -1 到 1）。
 *    所有针对 Vulkan 的 Y 轴翻转和 Z 轴 0-1 的映射必须在 Vulkan 后端的内部处理。
 * 2. **线程安全**：整个 IRenderAPI 的绝大多数接口**非线程安全**，必须在主渲染线程调用。
 *    但资源销毁接口（如 `MeshPtr` 析构）可能会内部调用底层等待，允许在同步点调用。
 */
class IRenderAPI {
public:
    virtual ~IRenderAPI() = default;

    /**
     * \brief 初始化渲染器。
     * \param width 初始渲染宽度（通常为窗口像素宽）
     * \param height 初始渲染高度（通常为窗口像素高）
     * \param window 绑定的窗口对象，用于获取原生句柄（如 HWND 或 SDL_Window）
     * 
     * \warning 必须在 Window 创建完成后调用。
     */
    virtual void Initialize(int width, int height, Window* window) = 0;

    /** \brief 销毁渲染器并释放所有 GPU 资源。内部会调用 WaitIdle() 安全回收。 */
    virtual void Shutdown() = 0;

    // ==================== 视口与清屏 ====================
    /**
     * \brief 设置当前视口大小。
     * \note 如果绑定到了 FBO，通常需要将视口设置为 FBO 的尺寸。
     */
    virtual void SetViewport(int x, int y, int w, int h) = 0;

    /**
     * \brief 设置清屏颜色。
     */
    virtual void SetClearColor(float r, float g, float b, float a) = 0;

    /**
     * \brief 根据标志位清理当前绑定的帧缓冲。
     * \param flag 清理掩码，支持位组合（如 `ClearFlag::Color | ClearFlag::Depth`）。
     */
    virtual void Clear(rhi::ClearFlag flag = rhi::ClearFlag::All) = 0;

    // ==================== 全局状态 ====================
    /**
     * \brief 一次性提交全局状态（相机、光照等）。
     * \param state 包含所有全局信息的结构体。
     */
    virtual void SetGlobalState(const GlobalState& state) = 0;

    // ==================== 资源创建 ====================
    /** \brief 从 CPU 内存中创建网格。 */
    virtual MeshPtr CreateMesh(const MeshData& data) = 0;

    /** \brief 从原始 RGBA 像素数组创建纹理。 */
    virtual TexturePtr CreateTexture(const uint8_t* rgba, int w, int h) = 0;

    /** \brief 编译并链接着色器，同时根据结构体内的配置生成渲染管线。 */
    virtual ShaderPtr CreateShader(const ShaderCreateInfo& info) = 0;

    /** \brief 创建离屏渲染帧缓冲。 */
    virtual FramebufferPtr CreateFramebuffer(const FramebufferCreateInfo& info) = 0;

    // ==================== 帧渲染控制 ====================
    /** \brief 将后续的渲染指令输出到指定的帧缓冲中。 */
    virtual void BindFramebuffer(const FramebufferPtr& fb) = 0;

    /** \brief 解绑当前帧缓冲，将渲染目标切换回默认屏幕（Swapchain）。 */
    virtual void UnbindFramebuffer() = 0;

    /** \brief 准备开始新的一帧（内部处理 acquireNextImage 等）。 */
    virtual void BeginFrame() = 0;

    /** \brief 结束当前帧渲染，提交到 GPU 并交换缓冲。 */
    virtual void EndFrame() = 0;

    // ==================== 绘制接口 ====================
    /** 
     * \brief 绘制单个网格。 
     * \param model 模型矩阵（直接内嵌，无需状态机式 SetModelMatrix）。
     */
    virtual void DrawMesh(const MeshPtr& mesh, const ShaderPtr& shader, const Material& material, const glm::mat4& model = glm::mat4(1.0f)) = 0;

    /** \brief 实例化绘制（适合绘制大量形状相同但位置不同的物体，如掉落物、粒子）。 */
    virtual void DrawMeshInstanced(const MeshPtr& mesh, const ShaderPtr& shader, const Material& material, const std::vector<glm::mat4>& transforms) = 0;

    /** \brief 合批绘制（适合地形区块，能显著减少状态切换开销）。 */
    virtual void DrawMeshBatch(const std::vector<MeshPtr>& meshes, const ShaderPtr& shader, const Material& mat) = 0;

    /** \brief 绘制天空盒。通常在渲染流程的最后阶段调用。 */
    virtual void DrawSkybox(const ShaderPtr& shader) = 0;

    /** \brief 绘制全屏四边形（通常用于后处理，如将 FBO 贴图渲染到屏幕）。 */
    virtual void DrawFullscreenQuad(const TexturePtr& texture) = 0;

    // ==================== Uniform 数据传递 ====================
    virtual void SetUniform(const ShaderPtr& shader, const std::string& name, const glm::mat4& value) = 0;
    virtual void SetUniform(const ShaderPtr& shader, const std::string& name, const glm::vec3& value) = 0;
    virtual void SetUniform(const ShaderPtr& shader, const std::string& name, float value) = 0;
    virtual void SetUniform(const ShaderPtr& shader, const std::string& name, int value) = 0;

    // ==================== ImGui 后端 ====================
    virtual bool InitImGuiBackend() = 0;
    virtual void ShutdownImGuiBackend() = 0;
    virtual void ImGuiNewFrame() = 0;
    virtual void ImGuiRenderDrawData() = 0;

    // ==================== 其他 ====================
    /** \brief 获取当前图形后端的设备信息。 */
    [[nodiscard]] virtual DeviceInfo GetDeviceInfo() const = 0;

    /** \brief 应用窗口和渲染设置（如全屏、VSync 等）。 */
    virtual void ApplySettings(const WindowConfig& win, const RenderConfig& render) = 0;

    /** \brief 等待 GPU 空闲（销毁资源前必须调用）。 */
    virtual void WaitIdle() = 0;
};

} // namespace Eng::client