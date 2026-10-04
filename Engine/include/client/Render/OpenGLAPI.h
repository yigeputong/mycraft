#pragma once

#include "client/Render/RenderAPI.h"
#include "core/Log.h"
#include <glad/glad.h>
#include <string>
#include <memory>

#define GL(func) func;OpenGLAPI::glCheckErr()

namespace Eng::client {

class OpenGLAPI final : public IRenderAPI {
public:
    OpenGLAPI();
    ~OpenGLAPI() override;

    // 初始化与销毁
    bool Initialize(int width, int height, Window* window) override;
    void Shutdown() override;

    // 视口与清屏
    void SetViewport(int x, int y, int w, int h) override;
    void SetClearColor(float r, float g, float b, float a) override;
    void Clear() override;

    // 矩阵设置
    void SetViewMatrix(const glm::mat4& view) override;
    void SetProjectionMatrix(const glm::mat4& proj) override;
    void SetModelMatrix(const glm::mat4& model) override;
    void SetLightPosition(const glm::vec3& pos) override;
    void SetLightColor(const glm::vec3& color, const float intensity) override;
    void SetLightAmbient(const glm::vec3& amb) override;
    void SetViewPosition(const glm::vec3& pos) override;

    // 资源创建（返回句柄）
    MeshHandle CreateMesh(const MeshData& data) override;
    MeshHandle CreateMeshInstance(const MeshData& data) override;
    TextureHandle CreateTexture(const std::string& path) override;
    TextureHandle CreateTextureFromMemory(const aiTexture* embedded) override;
    TextureHandle CreateTextureFromPixels(const uint8_t* rgba, int w, int h) override;
    ShaderHandle CreateShader(const std::string& vertPath, const std::string& fragPath) override;
    ShaderHandle CreateSkybox(const std::string& vertPath, const std::string& fragPath) override;
    Model LoadModel(const std::string& path, bool flipUV = false) override;

    // 资源销毁
    void DestroyMesh(MeshHandle handle) override;
    void DestroyTexture(TextureHandle handle) override;
    void DestroyShader(ShaderHandle handle) override;

    Framebuffer CreateFramebuffer(int width, int height) override;
    void DestroyFramebuffer(const Framebuffer& fb) override;
    void BindFramebuffer(const Framebuffer& fb) override;
    void UnbindFramebuffer() override;
    TextureHandle GetFramebufferTexture(const Framebuffer& fb) const override;

    // Uniform 设置
    void SetUniform(ShaderHandle shader, const std::string& name, const glm::mat4& value) override;
    void SetUniform(ShaderHandle shader, const std::string& name, const glm::vec3& value) override;
    void SetUniform(ShaderHandle shader, const std::string& name, float value) override;
    void SetUniform(ShaderHandle shader, const std::string& name, int value) override;

    // 绘制
    void BeginFrame() override;
    void DrawMesh(MeshHandle mesh, ShaderHandle shader, const Material& material) override;
    void DrawMeshInstanced(MeshHandle mesh,
                                   ShaderHandle shader,
                                   const Material& material,
                                   const std::vector<glm::mat4>& transforms) override;

    void DrawSkybox(ShaderHandle shader) override;
    void DrawFullscreenQuad(TextureHandle textureID) override;
    void EndFrame() override;

    bool InitImGuiBackend() override;
    void ShutdownImGuiBackend() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderDrawData() override;

    DeviceInfo GetDeviceInfo() const override;

    static constexpr int api_major = 4;
    static constexpr int api_minor = 6;
private:
    // 状态
    bool m_initialized = false;

    // 资源句柄生成器
    MeshHandle m_nextMeshHandle = 1;
    TextureHandle m_nextTextureHandle = 1;
    ShaderHandle m_nextShaderHandle = 1;
    FramebufferHandle m_nextFramebufferHandle = 1;

    // 内部数据结构
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
        // 可选的 uniform 缓存（为了性能）
        // std::unordered_map<std::string, GLint> uniformCache;
    };

    struct FramebufferInternal {
        GLuint fbo      = 0;
        GLuint colorTex = 0;
        GLuint depthBuf = 0;
        int    width    = 0;
        int    height   = 0;
    };

    // 资源表
    std::unordered_map<MeshHandle,        MeshDataInternal>    m_meshes;
    std::unordered_map<TextureHandle,     TextureDataInternal> m_textures;
    std::unordered_map<std::string,       TextureHandle>       m_textureCache;
    std::unordered_map<ShaderHandle,      ShaderDataInternal>  m_shaders;
    std::unordered_map<FramebufferHandle, FramebufferInternal> m_framebuffers;
    std::unordered_map<FramebufferHandle, TextureHandle>       m_fbColorHandles;

    // 默认纹理
    TextureHandle m_defaultTexture = 0;
    GLuint m_defaultTexID = 0;

    // 变换矩阵
    glm::mat4 m_viewMatrix       = glm::mat4(1.0f);
    glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
    glm::mat4 m_modelMatrix      = glm::mat4(1.0f);

    // 相机/光源参数
    glm::vec3 m_lightPos       = glm::vec3(1.0f, 2.0f, 3.0f);
    glm::vec3 m_viewPos        = glm::vec3(0.0f);
    glm::vec3 m_lightDir       = glm::vec3(0.5f, -1.0f, 0.3f);  // 斜向下照
    glm::vec3 m_lightColor     = glm::vec3(1.0f, 0.98f, 0.9f);    // 略暖的白
    glm::vec3 m_lightAmbient   = glm::vec3(0.3f);
    float     m_lightIntensity = 1.0f;
    float     m_aoStrength = 1.0f;
    float     m_timeOfDay  = 0.25f;

    // UBO
    GLuint m_globalUBO   = 0;   // binding = 0，存相机 + 光源
    GLuint m_materialUBO = 0;   // binding = 1，存材质

    // 天空盒
    MeshHandle m_skyCubeMesh = 0;

    // 全屏四边形
    GLuint m_fullscreenVAO = 0;
    GLuint m_fullscreenVBO = 0;
    ShaderHandle m_fullscreenShader = 0;

    // 记录日志
    std::unique_ptr<Log> m_logger = std::make_unique<Log>();
    DeviceInfo m_deviceInfo;

    // 视口和清屏
    Window* m_window;
    int m_viewportX = 0, m_viewportY = 0;
    int m_viewportWidth = 0, m_viewportHeight = 0;
    float m_clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    // 辅助函数
    MeshHandle GetSkyCubeMesh();
    void UpdateGlobalUBO();
    void UpdateMaterialUBO(float shininess);
    GLuint CompileShader(GLenum type, const std::string& source);
    GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader);
    std::string ReadFile(const std::string& path);
    MeshHandle CreateMeshInternal(const MeshData& data, bool instanced);
    GLuint ResolveTexture(TextureHandle h) const {
        if (h == 0) return m_defaultTexID;
        auto it = m_textures.find(h);
        if (it == m_textures.end() || it->second.textureID == 0)
            return m_defaultTexID;
        return it->second.textureID;
    }
};
    
}
