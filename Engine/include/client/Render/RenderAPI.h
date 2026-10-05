#pragma once

#include "core/Configs.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

struct aiTexture;

namespace Eng::client {

class Window;

using MeshHandle = uint32_t;
using TextureHandle = uint32_t;
using ShaderHandle = uint32_t;
using FramebufferHandle = uint32_t;

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    float     ao = 1.0f;
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

struct SubMesh {
    MeshHandle mesh = 0;
    TextureHandle diffuseTexture = 0;
    glm::mat4 localTransform = glm::mat4(1.0f);  // 保留扩展空间
};

struct Model {
    std::vector<SubMesh> subMeshes;

    // 便利方法：是否有效
    bool IsValid() const { return !subMeshes.empty(); }

    // 兼容旧代码：返回第一个 mesh
    MeshHandle FirstMesh() const {
        return subMeshes.empty() ? 0 : subMeshes[0].mesh;
    }
};

struct Framebuffer {
    FramebufferHandle handle = 0;
    int  width  = 0;
    int  height = 0;

    bool isValid() const { return handle != 0; }
};

struct DeviceInfo {
    std::string backend = "Unknown";          // "OpenGL" / "Vulkan"
    std::string deviceName = "Unknown";       // "NVIDIA GeForce RTX 5060 Laptop GPU"
    std::string vendor = "Unknown";           // "NVIDIA" / "NVIDIA Corporation"
    std::string apiVersion = "Unknown";       // "4.6" / "1.4.351"
    std::string driverVersion = "Unknown";    // "617.14" / "4.6.0 NVIDIA 617.14"
    std::string shadingLanguage = "Unknown";  // "GLSL 4.60" / "SPIR-V 1.4"
    std::string extra = "Unknown";            // 后备：原始 version 串，供人肉对比
};

// ============ Global UBO (binding = 0) ============
struct GlobalUBOData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 viewPos;
    float     aoStrength;      // 原 _pad0
    glm::vec3 lightDir;
    float     lightIntensity;
    glm::vec3 lightColor;
    float     timeOfDay;       // 原 _pad1
    glm::vec3 lightAmbient;
    float     _pad2;
};
static_assert(sizeof(GlobalUBOData) == 192, "GlobalUBO size mismatch");

// ============ Material UBO (binding = 1) ============
struct MaterialUBOData {
    float shininess;          // 0 ~ 3
    float _pad[3];            // 4 ~ 15
};
static_assert(sizeof(MaterialUBOData) == 16, "MaterialUBO size mismatch");

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
    virtual void SetLightPosition(const glm::vec3& pos) = 0;
    virtual void SetLightColor(const glm::vec3& color, const float intensity) = 0;
    virtual void SetLightAmbient(const glm::vec3& amb) = 0;
    virtual void SetViewPosition(const glm::vec3& pos) = 0;

    // 资源创建
    virtual MeshHandle CreateMesh(const MeshData& data) = 0;
    virtual MeshHandle CreateMeshInstance(const MeshData& data) = 0;
    virtual TextureHandle CreateTexture(const std::string& path) = 0;
    virtual TextureHandle CreateTextureFromMemory(const aiTexture* embedded) = 0;
    virtual TextureHandle CreateTextureFromPixels(const uint8_t* rgba, int w, int h) = 0;
    virtual ShaderHandle CreateShader(const std::string& vertPath, const std::string& fragPath) = 0;
    virtual ShaderHandle CreateSkybox(const std::string& vertPath, const std::string& fragPath) = 0;
    virtual Model LoadModel(const std::string& path, bool flipUV = false) = 0;

    // 帧缓冲创建
    virtual Framebuffer CreateFramebuffer(int width, int height) = 0;
    virtual void DestroyFramebuffer(const Framebuffer& fb) = 0;
    virtual void BindFramebuffer(const Framebuffer& fb) = 0;
    virtual void UnbindFramebuffer() = 0;
    virtual TextureHandle GetFramebufferTexture(const Framebuffer& fb) const = 0;

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
    virtual void BeginFrame() = 0;
    virtual void DrawMesh(MeshHandle mesh, ShaderHandle shader, const Material& material) = 0;
    virtual void DrawMeshInstanced(MeshHandle mesh,
                                    ShaderHandle shader,
                                    const Material& material,
                                    const std::vector<glm::mat4>& transforms) = 0;
    virtual void EndFrame() = 0;

    // 特殊绘制（天空盒、UI等）
    virtual void DrawSkybox(ShaderHandle shader) = 0;
    virtual void DrawFullscreenQuad(TextureHandle textureID) = 0;

    // ==================== ImGui 后端 ====================
    // 调用方负责 ImGui::CreateContext() 和 ImGui_ImplSDL3_InitXXX
    // 后端负责渲染器相关的初始化和每帧绘制
    virtual bool InitImGuiBackend()   = 0;
    virtual void ShutdownImGuiBackend() = 0;
    virtual void ImGuiNewFrame()      = 0;   // 内部调 ImGui_ImplXxx_NewFrame
    virtual void ImGuiRenderDrawData() = 0;  // 内部调 ImGui_ImplXxx_RenderDrawData

    //其他
    virtual DeviceInfo GetDeviceInfo() const = 0;
    virtual void ApplySettings(const WindowConfig& win, const RenderConfig& render) = 0;
};

}