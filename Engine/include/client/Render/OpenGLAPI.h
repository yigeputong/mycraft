#pragma once

#include "client/Render/RenderAPI.h"
#include "core/Log.h"
#include <glad/glad.h>
#include <string>
#include <memory>
#include <unordered_map>

namespace Eng::client {

class OpenGLAPI final : public IRenderAPI {
public:
    OpenGLAPI();
    ~OpenGLAPI() override;

    void Initialize(int width, int height, Window* window) override;
    void Shutdown() override;

    void InitPipelines() override;
    void BindPipeline(rhi::PipelineType type) override;

    void SetViewport(int x, int y, int w, int h) override;
    void SetClearColor(float r, float g, float b, float a) override;
    void Clear(rhi::ClearFlag flag = rhi::ClearFlag::All) override;
    void SetGlobalState(const GlobalState& state) override;

    // ★ MDI 接口
    void UploadGlobalGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                                size_t& outVboCapacity, size_t& outIboCapacity) override;
    void UpdateGlobalGeometry(uint32_t vertexOffset, const std::vector<Vertex>& vertices,
                            uint32_t indexOffset, const std::vector<uint32_t>& indices) override;
    void UploadIndirectCommands(const std::vector<rhi::DrawIndexedIndirectCommand>& commands) override;
    void DrawMDI(const ShaderPtr& shader, const Material& material, uint32_t commandCount) override;

    // 原有资源创建
    MeshPtr CreateMesh(const MeshData& data) override;
    TexturePtr CreateTexture(const uint8_t* rgba, int w, int h) override;
    ShaderPtr CreateShader(const ShaderCreateInfo& info) override;
    FramebufferPtr CreateFramebuffer(const FramebufferCreateInfo& info) override;

    void BindFramebuffer(const FramebufferPtr& fb) override;
    void UnbindFramebuffer() override;

    void BeginFrame() override;
    void EndFrame() override;

    void DrawMesh(const MeshPtr& mesh, const ShaderPtr& shader,
                  const Material& material,
                  const rhi::PerObjectData& data = {}) override;
    void DrawMeshInstanced(const MeshPtr& mesh, const ShaderPtr& shader,
                           const Material& material,
                           const std::vector<rhi::PerObjectData>& perObjects) override;
    void DrawSkybox(const ShaderPtr& shader) override;
    void BlitToScreen(const TexturePtr& texture) override;

    void SetUniform(const ShaderPtr& shader, const std::string& name, const glm::mat4& value) override;
    void SetUniform(const ShaderPtr& shader, const std::string& name, const glm::vec3& value) override;
    void SetUniform(const ShaderPtr& shader, const std::string& name, float value) override;
    void SetUniform(const ShaderPtr& shader, const std::string& name, int value) override;

    bool InitImGuiBackend() override;
    void ShutdownImGuiBackend() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderDrawData() override;

    [[nodiscard]] DeviceInfo GetDeviceInfo() const override;
    void ApplySettings(const WindowConfig& win, const RenderConfig& render) override;
    void WaitIdle() override;
    static Eng::client::BackendAvailability Probe();

    static constexpr int api_major = 4;
    static constexpr int api_minor = 6;

private:
    bool m_initialized = false;

    MeshHandle m_nextMeshHandle = 1;
    TextureHandle m_nextTextureHandle = 1;
    ShaderHandle m_nextShaderHandle = 1;
    FramebufferHandle m_nextFramebufferHandle = 1;

    struct MeshDataInternal {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLuint instanceVBO = 0;
        size_t indexCount = 0;
        GLenum indexType = GL_NONE;
    };

    struct TextureDataInternal {
        GLuint textureID = 0;
        int width = 0, height = 0;
        GLenum format = GL_RGBA;
    };

    struct ShaderDataInternal {
        GLuint program = 0;
        GLuint perObjectUBO = 0;                                       // 仅 Default 模式非零
        rhi::PerObjectMode perObjectMode = rhi::PerObjectMode::None;   // 新增
        rhi::PipelineType  pipelineType  = rhi::PipelineType::Opaque;  // 新增，DrawMesh 里用
    };

    struct FramebufferInternal {
        GLuint fbo      = 0;
        GLuint colorTex = 0;
        GLuint depthBuf = 0;
        int    width    = 0;
        int    height   = 0;
    };

    std::unordered_map<MeshHandle,        MeshDataInternal>    m_meshes;
    std::unordered_map<TextureHandle,     TextureDataInternal> m_textures;
    std::unordered_map<ShaderHandle,      ShaderDataInternal>  m_shaders;
    std::unordered_map<FramebufferHandle, FramebufferInternal> m_framebuffers;
    std::unordered_map<FramebufferHandle, TextureHandle>       m_fbColorHandles;

    TexturePtr m_defaultTexturePtr = nullptr;
    GlobalState m_globalState{};

    GLuint m_globalUBO   = 0;
    GLuint m_materialUBO = 0;

    // 核心：全局 DSA 句柄
    GLuint m_globalVAO = 0;
    GLuint m_globalVBO = 0;
    GLuint m_globalIBO = 0;
    GLuint m_indirectVBO = 0;

    // 新增：全局几何体的字节容量，用于底层安全校验
    size_t m_globalVboCapacity = 0;
    size_t m_globalIboCapacity = 0;

    MeshPtr m_skyCubeMesh = nullptr;

    GLuint m_fullscreenVAO = 0;
    GLuint m_fullscreenVBO = 0;
    ShaderPtr m_fullscreenShader = nullptr;

    std::unique_ptr<Log> m_logger = std::make_unique<Log>();
    DeviceInfo m_deviceInfo;

    Window* m_window;
    int m_viewportX = 0, m_viewportY = 0;
    int m_viewportWidth = 0, m_viewportHeight = 0;
    float m_clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    // 内部辅助
    MeshHandle CreateMeshInternal(const MeshData& data);
    TextureHandle CreateTextureInternal(const uint8_t* rgba, int w, int h);
    ShaderHandle CreateShaderInternal(const ShaderCreateInfo& info);
    FramebufferHandle CreateFramebufferInternal(const FramebufferCreateInfo& info);

    void DestroyMeshInternal(MeshHandle handle);
    void DestroyTextureInternal(TextureHandle handle);
    void DestroyShaderInternal(ShaderHandle handle);
    void DestroyFramebufferInternal(FramebufferHandle handle);

    void InitSkyCubeMesh();
    GLuint CompileShader(GLenum type, const std::string& source);
    GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader);
    std::string ReadFile(const std::string& path);
    
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

}