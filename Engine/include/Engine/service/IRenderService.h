#pragma once

#include "Engine/tool/Configs.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <span>

namespace Eng::tool {
    class Window;
}

namespace Eng::service {

// ==================== 硬件枚举 ====================
namespace rhi {

enum class TextureFormat : uint8_t {
    RGBA8_UNORM,
    RGBA16_SFLOAT,
    R8_UNORM,
    D32_SFLOAT
};

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

// 顶点布局
enum class VertexLayout : uint8_t {
    Standard,      // position + normal + uv + ao + light
    Skybox,        // 仅 position（未来可改名 PositionOnly）
};

enum class BufferUsage : uint8_t {
    Vertex   = 1 << 0,
    Index    = 1 << 1,
    Indirect = 1 << 2,
    Uniform  = 1 << 3,
};

struct DrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

// ============================================================
// 底层管线状态（对齐 Vulkan 语义，不含任何"业务渲染意图"）
// ============================================================
enum class CompareOp : uint8_t {
    Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always
};

enum class CullMode : uint8_t {
    None, Front, Back, FrontAndBack
};

enum class FrontFace : uint8_t {
    CounterClockwise, Clockwise
};

enum class BlendFactor : uint8_t {
    Zero, One, SrcAlpha, OneMinusSrcAlpha,
    DstAlpha, OneMinusDstAlpha,
    SrcColor, OneMinusSrcColor,
    DstColor, OneMinusDstColor,
};

enum class BlendOp : uint8_t {
    Add, Subtract, ReverseSubtract, Min, Max
};

struct RasterizerState {
    CullMode  cullMode          = CullMode::Back;
    FrontFace frontFace         = FrontFace::CounterClockwise;
    bool      depthBiasEnable   = false;
    float     depthBiasConstant = 0.0f;
    float     depthBiasSlope    = 0.0f;
    float     lineWidth         = 1.0f;
};

struct DepthStencilState {
    bool      depthTestEnable   = true;
    bool      depthWriteEnable  = true;
    CompareOp depthCompareOp    = CompareOp::LessOrEqual;
    bool      stencilTestEnable = false;
};

struct BlendState {
    bool        blendEnable          = false;
    BlendFactor srcColorBlendFactor  = BlendFactor::SrcAlpha;
    BlendFactor dstColorBlendFactor  = BlendFactor::OneMinusSrcAlpha;
    BlendOp     colorBlendOp         = BlendOp::Add;
    BlendFactor srcAlphaBlendFactor  = BlendFactor::One;
    BlendFactor dstAlphaBlendFactor  = BlendFactor::Zero;
    BlendOp     alphaBlendOp         = BlendOp::Add;
    uint8_t     colorWriteMask       = 0x0F;   // R|G|B|A
};

struct GraphicsPipelineState {
    RasterizerState   rasterizer;
    DepthStencilState depthStencil;
    BlendState        blend;

    // ===== 便捷预设（迁移期与快速原型使用）=====
    static GraphicsPipelineState PresetOpaque() {
        GraphicsPipelineState s;
        s.depthStencil = { true, true, CompareOp::LessOrEqual, false };
        s.rasterizer   = { CullMode::Back, FrontFace::CounterClockwise, false, 0.0f, 0.0f, 1.0f };
        s.blend        = {};
        return s;
    }
    static GraphicsPipelineState PresetTransparent() {
        GraphicsPipelineState s;
        s.depthStencil = { true, false, CompareOp::LessOrEqual, false };
        s.rasterizer   = { CullMode::Back, FrontFace::CounterClockwise, false, 0.0f, 0.0f, 1.0f };
        s.blend        = { true, BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, BlendOp::Add,
                          BlendFactor::One, BlendFactor::Zero, BlendOp::Add, 0x0F };
        return s;
    }
    static GraphicsPipelineState PresetSkybox() {
        GraphicsPipelineState s;
        s.depthStencil = { true, false, CompareOp::LessOrEqual, false };
        s.rasterizer   = { CullMode::None, FrontFace::CounterClockwise, false, 0.0f, 0.0f, 1.0f };
        s.blend        = {};
        return s;
    }
    static GraphicsPipelineState PresetHand() {
        GraphicsPipelineState s;
        s.depthStencil = { false, false, CompareOp::LessOrEqual, false };
        s.rasterizer   = { CullMode::Back, FrontFace::CounterClockwise, false, 0.0f, 0.0f, 1.0f };
        s.blend        = {};
        return s;
    }
    static GraphicsPipelineState PresetUI() {
        GraphicsPipelineState s;
        s.depthStencil = { false, false, CompareOp::LessOrEqual, false };
        s.rasterizer   = { CullMode::None, FrontFace::CounterClockwise, false, 0.0f, 0.0f, 1.0f };
        s.blend        = { true, BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, BlendOp::Add,
                          BlendFactor::One, BlendFactor::Zero, BlendOp::Add, 0x0F };
        return s;
    }
};

// =======================================================
// Shader 字节码包装：shared_ptr 共享，拷贝 = 引用计数递增
// =======================================================
class ShaderBytecode {
public:
    ShaderBytecode() = default;

    // 从 vector 移动构造：只发生一次 move，之后所有副本共享
    explicit ShaderBytecode(std::vector<std::byte> bytes)
        : m_storage(std::make_shared<const std::vector<std::byte>>(std::move(bytes))) {}

    // 从已有 shared_ptr 构造：多 CreateInfo 共享同一份
    explicit ShaderBytecode(std::shared_ptr<const std::vector<std::byte>> storage)
        : m_storage(std::move(storage)) {}

    std::span<const std::byte> Bytes() const noexcept {
        return m_storage ? std::span<const std::byte>(*m_storage)
                        : std::span<const std::byte>{};
    }

    bool   Empty()     const noexcept { return !m_storage || m_storage->empty(); }
    size_t SizeBytes() const noexcept { return m_storage ? m_storage->size() : 0; }

    const std::shared_ptr<const std::vector<std::byte>>& Storage() const noexcept {
        return m_storage;
    }

private:
    std::shared_ptr<const std::vector<std::byte>> m_storage;
};

// ==================== RenderCapabilities ====================
struct RenderCapabilities {
    std::string apiName = "Unknown";
    uint32_t apiMajor = 0;
    uint32_t apiMinor = 0;

    uint32_t maxTextureSize = 0;
    uint32_t maxComputeWorkGroupInvocations = 0;
    uint32_t maxVertexAttributes = 0;
    uint32_t maxVertexBufferBindings = 0;

    bool supportsGeometryShader = false;
    bool supportsTessellation = false;
    bool supportsDynamicRendering = false;
    bool supportsMeshShaders = false;
    bool supportsRayTracing = false;
    bool supportsCompute = false;
    bool supportsMultiDrawIndirect = false;

    bool supportsBindless = false;
    bool supportsBufferDeviceAddress = false;
    bool supportsTimelineSemaphore = false;
    bool supportsSparseBinding = false;

    std::vector<std::string> availableExtensions;
};

// ==================== CreateInfo 家族 ====================
struct BufferCreateInfo {
    size_t      size  = 0;
    BufferUsage usage = BufferUsage::Vertex;
};

// 纹理坐标包裹模式
enum class WrapMode : std::uint8_t {
    Repeat,
    ClampToEdge,
    MirroredRepeat
};

// 放大过滤模式（通常不需要 Mipmap）
enum class FilterMode : std::uint8_t {
    Nearest,    // 像素风首选
    Linear      // 平滑
};

// 缩小过滤模式（包含 Mipmap 策略）
enum class MinFilterMode : std::uint8_t {
    Nearest,
    Linear,
    Nearest_Mipmap_Nearest,
    Linear_Mipmap_Nearest,
    Nearest_Mipmap_Linear,  // 像素风远景神器
    Linear_Mipmap_Linear    // 三线性过滤
};

struct TextureCreateInfo {
    std::span<const std::byte> pixels;
    int                        width         = 0;
    int                        height        = 0;
    int                        arrayLayers   = 1;
    int                        requestedMips = 0;
    TextureFormat              format        = TextureFormat::RGBA8_UNORM;

    // 采样器配置 (赋予默认值，兼容旧代码)
    FilterMode                 magFilter     = FilterMode::Nearest;
    MinFilterMode              minFilter     = MinFilterMode::Nearest_Mipmap_Linear;
    WrapMode                   wrapU         = WrapMode::ClampToEdge;
    WrapMode                   wrapV         = WrapMode::ClampToEdge;
    bool                       enableAnisotropy = true;
};

struct ShaderCreateInfo {
    // ==================== SPIR-V 二进制（零拷贝共享） ====================
    ShaderBytecode vertSpirv;
    ShaderBytecode fragSpirv;

    // 调试名（缓存键用）
    std::string debugName;

    // ==================== 入口点 ====================
    std::string vertEntry = "main";
    std::string fragEntry = "main";

    // ==================== 硬件状态 ====================
    TextureFormat  colorFormat       = TextureFormat::RGBA8_UNORM;
    VertexLayout   vertexLayout      = VertexLayout::Standard;
    std::optional<GraphicsPipelineState> pipelineState = std::nullopt;
    uint32_t       pushConstantSize  = 0;

    bool IsValid() const {
        return !vertSpirv.Empty() && !fragSpirv.Empty();
    }
};

struct FramebufferCreateInfo {
    int           width       = 0;
    int           height      = 0;
    bool          hasDepth    = true;
    TextureFormat colorFormat = TextureFormat::RGBA8_UNORM;
};

struct PerObjectData {
    glm::mat4 model         = glm::mat4(1.0f);
    glm::vec4 tintColor     = glm::vec4(1.0f);
    glm::vec4 uvOffsetScale = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
};

} // namespace rhi

// ==================== 底层句柄 ====================
using TextureHandle     = uint32_t;
using ShaderHandle      = uint32_t;
using FramebufferHandle = uint32_t;
using BufferHandle      = uint32_t;

class Texture;
class Shader;
class Framebuffer;
class Buffer;

using TexturePtr     = std::shared_ptr<Texture>;
using ShaderPtr      = std::shared_ptr<Shader>;
using FramebufferPtr = std::shared_ptr<Framebuffer>;
using BufferPtr      = std::shared_ptr<Buffer>;

class Texture {
public:
    explicit Texture(TextureHandle h) : m_handle(h) {}
    ~Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    TextureHandle GetHandle() const { return m_handle; }
private:
    TextureHandle m_handle;
};

class Shader {
public:
    explicit Shader(ShaderHandle h) : m_handle(h) {}
    ~Shader() = default;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    ShaderHandle GetHandle() const { return m_handle; }
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
    TexturePtr GetColorTexture() const { return m_colorTexture; }
    int GetWidth()  const { return m_width; }
    int GetHeight() const { return m_height; }
    FramebufferHandle GetHandle() const { return m_handle; }
private:
    FramebufferHandle m_handle;
    TexturePtr        m_colorTexture;
    int               m_width;
    int               m_height;
};

class Buffer {
public:
    explicit Buffer(BufferHandle h) : m_handle(h) {}
    ~Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    BufferHandle GetHandle() const { return m_handle; }
private:
    BufferHandle m_handle;
};

// ==================== RHI 通用数据 ====================
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    float     layer = 0.0f;
    float     ao = 1.0f;
    glm::vec4 light;
};

struct DeviceInfo {
    std::string backend         = "Unknown";
    std::string deviceName      = "Unknown";
    std::string vendor          = "Unknown";
    std::string apiVersion      = "Unknown";
    std::string driverVersion   = "Unknown";
    std::string shadingLanguage = "Unknown";
    std::string extra           = "Unknown";
};

struct BackendAvailability {
    bool available = false;
    rhi::RenderCapabilities caps;
    std::string reason;
};

// ==================== RHI 主接口 ====================
class IRenderService {
public:
    virtual ~IRenderService() = default;

    // ===== 生命周期 =====
    virtual void Initialize(int width, int height, tool::Window* window) = 0;
    virtual void Shutdown() = 0;
    virtual void WaitIdle() = 0;
    virtual DeviceInfo GetDeviceInfo() const = 0;
    virtual void ApplySettings(const tool::WindowConfig& win, const tool::RenderConfig& render) = 0;
    virtual glm::mat4 MakePerspectiveMatrix(float fovYRadians, float aspect, float nearZ, float farZ) const = 0;

    // ===== 帧控制 =====
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    // ===== 视口与清屏 =====
    virtual void SetViewport(int x, int y, int w, int h) = 0;

    virtual void Clear(rhi::ClearFlag flag = rhi::ClearFlag::All,
                       const glm::vec4& color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
                       float depth = 1.0f,
                       uint8_t stencil = 0) = 0;

    // ===== 资源创建（新接口）=====
    virtual BufferPtr      CreateBuffer(const rhi::BufferCreateInfo& info) = 0;
    virtual TexturePtr     CreateTexture(const rhi::TextureCreateInfo& info) = 0;
    virtual ShaderPtr      CreateShader(const rhi::ShaderCreateInfo& info) = 0;
    virtual FramebufferPtr CreateFramebuffer(const rhi::FramebufferCreateInfo& info) = 0;

    // ===== 资源更新 =====
    virtual void UpdateBuffer(const BufferPtr& buffer, size_t offset,
                              std::span<const std::byte> data) = 0;

    // ===== 帧缓冲绑定 =====
    virtual void BindFramebuffer(const FramebufferPtr& fb) = 0;
    virtual void UnbindFramebuffer() = 0;

    // ===== 无状态绑定 =====
    virtual void BindShader(const ShaderPtr& shader) = 0;
    virtual void BindVertexBuffer(const BufferPtr& buffer, uint32_t binding = 0) = 0;
    virtual void BindIndexBuffer(const BufferPtr& buffer) = 0;
    virtual void BindTexture(const TexturePtr& texture, uint32_t slot) = 0;

    // ===== 常量缓冲 =====
    virtual void BindUniformBuffer(const BufferPtr& buffer, uint32_t set, uint32_t binding) = 0;
    virtual void PushConstants(const ShaderPtr& shader, std::span<const std::byte> data) = 0;

    // ===== 绘制 =====
    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                      uint32_t firstVertex = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                             uint32_t firstIndex = 0, int32_t vertexOffset = 0) = 0;
    virtual void DrawIndexedIndirect(const BufferPtr& indirectBuffer,
                                     uint32_t drawCount, uint32_t stride) = 0;

    // ===== ImGui =====
    virtual bool InitImGuiBackend() = 0;
    virtual void ShutdownImGuiBackend() = 0;
    virtual void ImGuiNewFrame() = 0;
    virtual void ImGuiRenderDrawData() = 0;
};

} // namespace Eng::service