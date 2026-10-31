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
namespace rhi {

/** \brief 现代渲染管线类型（取代散装的状态机） */
enum class PipelineType : uint8_t {
    Opaque,       /**< 不透明实体（背面剔除，深度读写） */
    Transparent,  /**< 半透明（水/玻璃，开启混合，深度读不写） */
    Skybox,       /**< 天空盒（无剔除，深度读不写） */
    Hand,         /**< 第一人称手部（关闭深度测试，永远画在最前） */
    UI            /**< 2D 叠加层（无剔除，无深度） */
};

/** \brief 纹理与帧缓冲的像素格式 */
enum class TextureFormat : uint8_t {
    RGBA8_UNORM,    
    RGBA16_SFLOAT,  
    R8_UNORM,       
    D32_SFLOAT      
};

/** \brief 清屏标志位 */
enum class ClearFlag : std::uint8_t {
    None    = 0,
    Color   = 1 << 0,
    Depth   = 1 << 1,
    Stencil = 1 << 2,
    All     = Color | Depth | Stencil
};

inline ClearFlag operator|(ClearFlag a, ClearFlag b) {
    return static_cast<ClearFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

/**
 * \brief GPU 端的 MDI (Multi-Draw Indirect) 指令结构。
 * \warning 内存布局必须与 GPU 指令严格一致，不可随意增减字段！
 */
struct DrawIndexedIndirectCommand {
    uint32_t indexCount;    /**< 要画的索引数量 */
    uint32_t instanceCount; /**< 实例数量（地形默认为 1） */
    uint32_t firstIndex;    /**< 在当前全局 IBO 中的起始索引偏移 */
    int32_t  vertexOffset;  /**< 在当前全局 VBO 中的顶点偏移基数 */
    uint32_t firstInstance; /**< 基础实例 ID */
};

/** \brief 顶点输入布局类型 */
enum class VertexLayout : uint8_t {
    Standard,   /**< 地形/实体：包含位置、法线、UV、AO、光照 */
    Skybox      /**< 天空盒：仅包含位置 */
};

enum class PerObjectMode : uint8_t {
    None,     // 不使用 per-object 数据（地形 MDI、天空盒、后处理）
    Default,  // 使用 PerObjectData（entity、手部、掉落物）
};

/**
 * @brief 每个 object 独有的数据。通过 push constant（Vulkan）/ UBO（OpenGL）传递。
 * @warning 总大小必须 <= 128 字节（Vulkan 最小保证）。
 */
struct PerObjectData {
    glm::mat4 model      = glm::mat4(1.0f);    // 64 字节
    glm::vec4 tintColor  = glm::vec4(1.0f);    // 16 字节，未来：受伤泛红/附魔发光
    glm::vec4 uvOffsetScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);  // 16 字节，未来：物品栏图标
    // 已用 96 字节，余 32 字节
};

} // namespace rhi


// ==================== 底层不透明句柄 ====================
using MeshHandle        = uint32_t;
using TextureHandle     = uint32_t;
using ShaderHandle      = uint32_t;
using FramebufferHandle = uint32_t;

class Mesh;
class Texture;
class Shader;
class Framebuffer;

using MeshPtr        = std::shared_ptr<Mesh>;
using TexturePtr     = std::shared_ptr<Texture>;
using ShaderPtr      = std::shared_ptr<Shader>;
using FramebufferPtr = std::shared_ptr<Framebuffer>;

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

class Framebuffer {
public:
    explicit Framebuffer(FramebufferHandle h, TexturePtr colorTex, int width, int height)
        : m_handle(h), m_colorTexture(std::move(colorTex)), m_width(width), m_height(height) {}
    ~Framebuffer() = default;
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
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
struct Vertex {
    glm::vec3 position;   
    glm::vec3 normal;     
    glm::vec2 uv;         
    float     ao = 1.0f;  
    glm::vec4 light;      
};

struct MeshData {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
};

struct Material {
    TexturePtr diffuse = nullptr;
    TexturePtr specular = nullptr;
    float shininess = 32.0f;
};

struct Model {
    struct SubMesh {
        MeshPtr mesh;
        TexturePtr diffuse;
        glm::mat4 transform = glm::mat4(1.0f);
    };
    std::vector<SubMesh> subMeshes;
    bool IsValid() const { return !subMeshes.empty(); }
};

struct BackendAvailability {
    bool available = false;
    std::string reason;
};

struct DeviceInfo {
    std::string backend = "Unknown";          
    std::string deviceName = "Unknown";       
    std::string vendor = "Unknown";           
    std::string apiVersion = "Unknown";       
    std::string driverVersion = "Unknown";    
    std::string shadingLanguage = "Unknown";  
    std::string extra = "Unknown";            
};

struct GlobalState {
    glm::mat4 view = glm::mat4(1.0f);                   
    glm::mat4 projection = glm::mat4(1.0f);             
    glm::vec3 viewPos = glm::vec3(0.0f);                
    float     aoStrength = 1.0f;                        
    glm::vec3 lightDir = glm::vec3(0.5f, -1.0f, 0.3f);  
    float     lightIntensity = 1.0f;                    
    glm::vec3 lightColor = glm::vec3(1.0f);             
    float     timeOfDay = 8.0f;                         
    glm::vec3 lightAmbient = glm::vec3(0.3f);           
    float     _pad2 = 0.0f;                             
};

// 移除了散装的 Cull/Depth/Blend 状态，这些状态现在由 InitPipelines 决定
struct ShaderCreateInfo {
    std::string vertPath;
    std::string fragPath;
    std::string vertEntry = "main";
    std::string fragEntry = "main";
    rhi::TextureFormat colorFormat = rhi::TextureFormat::RGBA8_UNORM;
    std::vector<std::pair<std::string, std::string>> defines;
    // 显式指定顶点布局，默认 Standard
    rhi::VertexLayout vertexLayout = rhi::VertexLayout::Standard;
    // 指定管线状态（决定深度/剔除/混合），默认是不透明物体
    rhi::PipelineType pipelineType = rhi::PipelineType::Opaque; 

    rhi::PerObjectMode perObjectMode = rhi::PerObjectMode::None;
};

struct FramebufferCreateInfo {
    int  width = 0;
    int  height = 0;
    bool hasDepth = true;
    rhi::TextureFormat colorFormat = rhi::TextureFormat::RGBA8_UNORM;
};


// ==================== 核心 RHI 接口 ====================
class IRenderAPI {
public:
    virtual ~IRenderAPI() = default;

    virtual void Initialize(int width, int height, Window* window) = 0;
    virtual void Shutdown() = 0;

    // ==================== 现代管线状态机 ====================
    /** \brief 初始化预设的管线状态（OpenGL: VAO 映射，Vulkan: VkPipeline 创建） */
    virtual void InitPipelines() = 0;

    /** \brief 切换管线（取代所有散装状态） */
    virtual void BindPipeline(rhi::PipelineType type) = 0;

    // ==================== 视口与清屏 ====================
    virtual void SetViewport(int x, int y, int w, int h) = 0;
    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear(rhi::ClearFlag flag = rhi::ClearFlag::All) = 0;

    // ==================== 全局状态 ====================
    virtual void SetGlobalState(const GlobalState& state) = 0;

    // ==================== MDI (Multi-Draw Indirect) 支持 ====================
    /** \brief 上传全局的大 VBO 和 IBO（用于地形区块合批） */
    virtual void UploadGlobalGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) = 0;

    /** \brief 每帧上传需要渲染的 MDI 指令 */
    virtual void UploadIndirectCommands(const std::vector<rhi::DrawIndexedIndirectCommand>& commands) = 0;

    // ==================== 传统资源创建 ====================
    virtual MeshPtr CreateMesh(const MeshData& data) = 0;
    virtual TexturePtr CreateTexture(const uint8_t* rgba, int w, int h) = 0;
    virtual ShaderPtr CreateShader(const ShaderCreateInfo& info) = 0;
    virtual FramebufferPtr CreateFramebuffer(const FramebufferCreateInfo& info) = 0;

    // ==================== 帧渲染控制 ====================
    virtual void BindFramebuffer(const FramebufferPtr& fb) = 0;
    virtual void UnbindFramebuffer() = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    // ==================== 独立对象绘制 ====================
    virtual void DrawMesh(const MeshPtr& mesh, const ShaderPtr& shader,
                          const Material& material,
                          const rhi::PerObjectData& data = {}) = 0;
    virtual void DrawMeshInstanced(const MeshPtr& mesh, const ShaderPtr& shader,
                                   const Material& material,
                                   const std::vector<rhi::PerObjectData>& perObjects) = 0;
    virtual void DrawSkybox(const ShaderPtr& shader) = 0;
    /** \brief 执行 MDI 绘制（一次性画完整个地形） */
    virtual void DrawMDI(const ShaderPtr& shader, const Material& material, uint32_t commandCount) = 0;
    /** \brief 将指定纹理呈递到屏幕（后处理输出的最终一步） */
    virtual void BlitToScreen(const TexturePtr& texture) = 0;

    // ==================== Uniform 数据传递 ====================
    virtual void SetUniform(const ShaderPtr& shader, const std::string& name, const glm::mat4& value) = 0;
    virtual void SetUniform(const ShaderPtr& shader, const std::string& name, const glm::vec3& value) = 0;
    virtual void SetUniform(const ShaderPtr& shader, const std::string& name, float value) = 0;
    virtual void SetUniform(const ShaderPtr& shader, const std::string& name, int value) = 0;

    // ==================== 其他 ====================
    virtual bool InitImGuiBackend() = 0;
    virtual void ShutdownImGuiBackend() = 0;
    virtual void ImGuiNewFrame() = 0;
    virtual void ImGuiRenderDrawData() = 0;

    [[nodiscard]] virtual DeviceInfo GetDeviceInfo() const = 0;
    virtual void ApplySettings(const WindowConfig& win, const RenderConfig& render) = 0;
    virtual void WaitIdle() = 0;
};

} // namespace Eng::client