#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

namespace Eng::client {

class Window;

using MeshHandle = uint32_t;
using TextureHandle = uint32_t;
using ShaderHandle = uint32_t;

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct Material {
    TextureHandle diffuse = 0;
    TextureHandle specular = 0;
    float shininess = 32.0f;
};

struct Model {
    MeshHandle mesh = 0;
    TextureHandle diffuseTexture = 0;
};

struct Framebuffer {
    uint32_t fboID = 0;
    uint32_t colorTexture = 0;
    uint32_t depthBuffer = 0;
    int width = 0, height = 0;
    bool isValid = false;
};

struct RenderConfig {
    // ===== 窗口 =====
    int windowWidth = 1920;
    int windowHeight = 1080;
    bool fullscreen = false;
    bool vsync = true;
    int refreshRate = 0; // 0 = 自动

    // ===== 渲染 =====
    int renderWidth = windowWidth;
    int renderHeight = windowHeight;
    float fov = 60.0f;                  // 垂直视野（度）
    float gamma = 2.2f;                 // Gamma 校正值
    float anisotropy = 0.0f;            // 各向异性过滤倍数（运行时查询）

    // ===== 后处理 =====
    bool enableBloom = false;           // 建议用 FeatureManager 管理
    bool enableSSAO = false;
    bool enableMotionBlur = false;

    int maxFPS = 0;                     // 0 = 不限帧率
    int shadowMapSize = 2048;           // 阴影贴图分辨率
    int maxLights = 16;                 // 最大动态光源数

    bool showFPS = true;
    bool showNormals = false;
    bool wireframeMode = false;

    void Reset();
};

class IRenderAPI {
public:
    virtual ~IRenderAPI() = default;

    // 初始化与销毁
    virtual bool Initialize(int width, int height, Window* window) = 0;
    virtual void Shutdown() = 0;

    // 视口与清屏
    virtual void SetViewport(int x, int y, int w, int h) = 0;
    virtual void SetClearColor(float r, float g, float b, float a) = 0;
    virtual void Clear() = 0;

    // 矩阵设置
    virtual void SetViewMatrix(const glm::mat4& view) = 0;
    virtual void SetProjectionMatrix(const glm::mat4& proj) = 0;
    virtual void SetModelMatrix(const glm::mat4& model) = 0;

    // 资源创建
    virtual MeshHandle CreateMesh(const MeshData& data) = 0;
    virtual TextureHandle CreateTexture(const std::string& path) = 0;
    //order: right, left, top, bottom, front, back
    virtual TextureHandle CreateSkybox(const std::vector<std::string>& path) = 0;
    virtual ShaderHandle CreateShader(const std::string& vertPath, const std::string& fragPath) = 0;
    virtual Model LoadModel(const std::string& path) = 0;

    // 帧缓冲创建
    virtual Framebuffer CreateFramebuffer(int width, int height) = 0;
    virtual void BindFramebuffer(const Framebuffer& fb) = 0;
    virtual void UnbindFramebuffer() = 0;
    virtual uint32_t GetFramebufferTexture(const Framebuffer& fb) const = 0;

    // 资源销毁
    virtual void DestroyMesh(MeshHandle handle) = 0;
    virtual void DestroyTexture(TextureHandle handle) = 0;
    virtual void DestroyShader(ShaderHandle handle) = 0;

    // 设置Uniform变量
    virtual void SetUniform(ShaderHandle shader, const std::string& name, const glm::mat4& value) = 0;
    virtual void SetUniform(ShaderHandle shader, const std::string& name, const glm::vec3& value) = 0;
    virtual void SetUniform(ShaderHandle shader, const std::string& name, float value) = 0;
    virtual void SetUniform(ShaderHandle shader, const std::string& name, int value) = 0;

    // 绘制核心
    virtual void DrawMesh(MeshHandle mesh, ShaderHandle shader, const Material& material) = 0;

    // 特殊绘制（天空盒、UI等）
    virtual void DrawSkybox(TextureHandle cubemap, const glm::mat4& view) = 0;
    virtual void DrawFullscreenQuad(TextureHandle textureID) = 0;
};

}