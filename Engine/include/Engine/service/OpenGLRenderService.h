#pragma once

#include "Engine/service/IRenderService.h"
#include "Engine/tool/Configs.h"
#include "Engine/tool/Log.h"
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <memory>
#include <span>
#include <cstddef>
#include <unordered_map>

namespace Eng::service {

class OpenGLRenderService final : public IRenderService {
public:
    OpenGLRenderService();
    ~OpenGLRenderService() override;

    // ===== 生命周期 =====
    void Initialize(int width, int height, tool::Window* window) override;
    void Shutdown() override;

    glm::mat4 MakePerspectiveMatrix(float fovYRadians, float aspect, float nearZ, float farZ) const override;

    // ===== 视口 / 清屏 =====
    void SetViewport(int x, int y, int w, int h) override;
    void Clear(rhi::ClearFlag flag = rhi::ClearFlag::All,
               const glm::vec4& color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
               float depth = 1.0f, uint8_t stencil = 0) override;

    // ===== 帧控制 =====
    void BeginFrame() override;
    void EndFrame() override;

    // ===== 资源创建与更新 =====
    BufferPtr      CreateBuffer(const rhi::BufferCreateInfo& info) override;
    TexturePtr     CreateTexture(const rhi::TextureCreateInfo& info) override;
    ShaderPtr      CreateShader(const rhi::ShaderCreateInfo& info) override;
    FramebufferPtr CreateFramebuffer(const rhi::FramebufferCreateInfo& info) override;
    void           UpdateBuffer(const BufferPtr& buffer, size_t offset, std::span<const std::byte> data) override;

    // ===== 帧缓冲绑定 =====
    void BindFramebuffer(const FramebufferPtr& fb) override;
    void UnbindFramebuffer() override;

    // ===== 无状态绑定 =====
    void BindShader(const ShaderPtr& shader) override;
    void BindVertexBuffer(const BufferPtr& buffer, uint32_t binding = 0) override;
    void BindIndexBuffer(const BufferPtr& buffer) override;
    void BindTexture(const TexturePtr& texture, uint32_t slot) override;

    // ===== 常量缓冲 =====
    void BindUniformBuffer(const BufferPtr& buffer, uint32_t set, uint32_t binding) override;
    void PushConstants(const ShaderPtr& shader, std::span<const std::byte> data) override;

    // ===== 绘制 =====
    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0) override;
    void DrawIndexedIndirect(const BufferPtr& indirectBuffer, uint32_t drawCount, uint32_t stride) override;

    // ===== ImGui =====
    bool InitImGuiBackend() override;
    void ShutdownImGuiBackend() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderDrawData() override;

    // ===== 杂项 =====
    DeviceInfo GetDeviceInfo() const override;
    void ApplySettings(const tool::WindowConfig& win, const tool::RenderConfig& render) override;
    void WaitIdle() override;
    static Eng::service::BackendAvailability Probe();

    static constexpr int api_major = 4;
    static constexpr int api_minor = 6;

private:
    bool m_initialized = false;
    bool m_imguiInitialized = false;
    SDL_GLContext m_glContext = nullptr;

    TextureHandle     m_nextTextureHandle     = 1;
    ShaderHandle      m_nextShaderHandle      = 1;
    FramebufferHandle m_nextFramebufferHandle = 1;
    BufferHandle      m_nextBufferHandle      = 1;
    
    struct TextureDataInternal {
        GLuint textureID = 0;
        int    width     = 0;
        int    height    = 0;
        GLenum format    = GL_RGBA;
    };
    struct ShaderDataInternal {
        GLuint program = 0;
        GLuint perObjectUBO = 0;
        uint32_t pushConstantSize = 0;
        rhi::GraphicsPipelineState pipelineState = {};
    };
    struct FramebufferInternal {
        GLuint fbo      = 0;
        GLuint colorTex = 0;
        GLuint depthBuf = 0;
        int    width    = 0;
        int    height   = 0;
    };
    struct BufferDataInternal {
        GLuint          buffer = 0;
        size_t          size   = 0;
        rhi::BufferUsage usage;
    };

    std::unordered_map<TextureHandle,     TextureDataInternal> m_textures;
    std::unordered_map<ShaderHandle,      ShaderDataInternal>  m_shaders;
    std::unordered_map<FramebufferHandle, FramebufferInternal> m_framebuffers;
    std::unordered_map<FramebufferHandle, TextureHandle>       m_fbColorHandles;
    std::unordered_map<BufferHandle,      BufferDataInternal>  m_buffers;

    GLuint m_drawVAO  = 0;
    bool   m_vaoDirty = false;

    GLuint m_currentVBO       = 0;
    GLuint m_currentIBO       = 0;
    GLuint m_currentProgram   = 0;
    GLuint m_currentTextureID = 0;

    TexturePtr m_defaultTexturePtr = nullptr;

    std::unique_ptr<tool::Log> m_logger = std::make_unique<tool::Log>();
    DeviceInfo                 m_deviceInfo;
    tool::RenderConfig         m_renderConfig;

    tool::Window* m_window        = nullptr;
    int           m_viewportX     = 0;
    int           m_viewportY     = 0;
    int           m_viewportWidth = 0;
    int           m_viewportHeight = 0;

    // ===== 内部实现辅助 =====
    TextureHandle     CreateTextureInternal(const uint8_t* rgba, int w, int h, int requestedMips = 0);
    ShaderHandle      CreateShaderInternal(const rhi::ShaderCreateInfo& info);
    FramebufferHandle CreateFramebufferInternal(const rhi::FramebufferCreateInfo& info);
    BufferHandle      CreateBufferInternal(size_t size, rhi::BufferUsage usage);

    void DestroyTextureInternal(TextureHandle handle);
    void DestroyShaderInternal(ShaderHandle handle);
    void DestroyFramebufferInternal(FramebufferHandle handle);
    void DestroyBufferInternal(BufferHandle handle);

    void ApplyStateIfNeeded();
    void ApplyPipelineState(const rhi::GraphicsPipelineState& state);

    GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader);
    GLuint ResolveTexture(const TexturePtr& tex) const {
        if (tex) {
            auto it = m_textures.find(tex->GetHandle());
            if (it != m_textures.end() && it->second.textureID != 0) {
                return it->second.textureID;
            }
        }
        if (m_defaultTexturePtr) {
            auto it = m_textures.find(m_defaultTexturePtr->GetHandle());
            if (it != m_textures.end()) {
                return it->second.textureID;
            }
        }
        return 0;
    }
};

} // namespace Eng::service