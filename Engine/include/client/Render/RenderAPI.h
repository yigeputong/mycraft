#pragma once

#include "core/Configs.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>
#include <memory>

namespace Eng::client {

class Window;

// ==================== 底层不透明句柄（业务层不可见） ====================
/**
 * \brief 底层内部资源句柄。
 * 
 * \note 仅供引擎底层（OpenGL/Vulkan）进行资源池索引使用。
 * 业务层代码**绝不**应该接触这些类型，请全程使用下方的智能指针封装。
 */
using MeshHandle      = uint32_t;
using TextureHandle   = uint32_t;
using ShaderHandle    = uint32_t;
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
};

/** \brief 清屏标志位。 */
enum class ClearFlag : std::uint32_t {
    None    = 0,
    Color   = 1 << 0,
    Depth   = 1 << 1,
    Stencil = 1 << 2,
    All     = Color | Depth | Stencil
};
inline ClearFlag operator|(ClearFlag a, ClearFlag b) {
    return static_cast<ClearFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/** \brief 着色器创建配置。支持预编译宏与渲染状态定制。 */
struct ShaderCreateInfo {
    std::string vertPath;
    std::string fragPath;
    std::string vertEntry = "main";          /**< SPIR-V 入口点 */
    std::string fragEntry = "main";          /**< SPIR-V 入口点 */
    bool isSky = false;                      /**< 是否为天空盒（关闭面剔除等） */
    bool depthTest = true;                   /**< 是否开启深度测试 */
    bool depthWrite = true;                  /**< 是否写入深度 */
    bool blendEnabled = true;                /**< 是否开启透明混合 */
    std::vector<std::pair<std::string, std::string>> defines; /**< 预编译宏 */
};

/** \brief 帧缓冲创建配置。 */
struct FramebufferCreateInfo {
    int  width = 0;
    int  height = 0;
    bool hasDepth = true;                    /**< 是否创建深度附件 */
    bool isHDR = false;                      /**< 是否使用高动态范围格式 */
};


// ==================== 核心 RHI 接口 ====================
/**
 * \brief 渲染硬件抽象接口 (Render Hardware Interface)。
 * 
 * 提供了对底层图形 API（OpenGL/Vulkan）的统一抽象。
 * 上层业务通过 `Create*` 系列接口获取带生命周期管理的智能指针，
 * 通过 `Draw*` 系列接口执行绘制。无需关心底层的句柄与销毁。
 */
class IRenderAPI {
public:
    virtual ~IRenderAPI() = default;

    /**
     * \brief 初始化渲染器。
     * \param width 宽度
     * \param height 高度
     * \param window 绑定的窗口对象
     */
    virtual void Initialize(int width, int height, Window* window) = 0;

    /** \brief 销毁渲染器。内部会调用 WaitIdle() 安全回收。 */
    virtual void Shutdown() = 0;

    // ==================== 视口与清屏 ====================
    virtual void SetViewport(int x, int y, int w, int h) = 0;
    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear(ClearFlag flag = ClearFlag::All) = 0;

    // ==================== 全局状态 ====================
    /**
     * \brief 一次性提交全局状态（相机、光照等）。
     * \param state 包含所有全局信息的结构体。
     */
    virtual void SetGlobalState(const GlobalState& state) = 0;

    // ==================== 资源创建 ====================
    
    /** \brief 创建网格。 */
    virtual MeshPtr CreateMesh(const MeshData& data) = 0;

    /** \brief 从本地文件加载纹理。 */
    virtual TexturePtr CreateTexture(const std::string& path) = 0;

    /** \brief 从内存像素创建纹理。 */
    virtual TexturePtr CreateTextureFromPixels(const uint8_t* rgba, int w, int h) = 0;

    /** \brief 创建着色器。 */
    virtual ShaderPtr CreateShader(const ShaderCreateInfo& info) = 0;

    /** \brief 创建帧缓冲。 */
    virtual FramebufferPtr CreateFramebuffer(const FramebufferCreateInfo& info) = 0;

    /** \brief 加载外部模型（内部会自动处理纹理内嵌与材质解析）。 */
    virtual Model LoadModel(const std::string& path, bool flipUV = false) = 0;

    // ==================== 帧渲染控制 ====================
    
    /** \brief 绑定离屏帧缓冲。 */
    virtual void BindFramebuffer(const FramebufferPtr& fb) = 0;

    /** \brief 解绑帧缓冲，恢复渲染到默认屏幕。 */
    virtual void UnbindFramebuffer() = 0;

    /** \brief 准备开始新的一帧。 */
    virtual void BeginFrame() = 0;

    /** \brief 结束当前帧渲染，提交到 GPU 并交换缓冲。 */
    virtual void EndFrame() = 0;

    // ==================== 绘制接口 ====================
    
    /** 
     * \brief 绘制单个网格。 
     * \param model 模型矩阵（直接内嵌，无需状态机式 SetModelMatrix）。
     */
    virtual void DrawMesh(const MeshPtr& mesh, const ShaderPtr& shader, const Material& material, const glm::mat4& model = glm::mat4(1.0f)) = 0;

    /** \brief 绘制多个实例（适合掉落物、粒子等）。 */
    virtual void DrawMeshInstanced(const MeshPtr& mesh, const ShaderPtr& shader, const Material& material, const std::vector<glm::mat4>& transforms) = 0;

    /** \brief 批量绘制（适合大量使用相同 Shader 的区块）。 */
    virtual void DrawMeshBatch(const std::vector<MeshPtr>& meshes, const ShaderPtr& shader, const Material& mat) = 0;

    /** \brief 绘制天空盒。 */
    virtual void DrawSkybox(const ShaderPtr& shader) = 0;

    /** \brief 绘制全屏四边形（后处理）。 */
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