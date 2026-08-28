#pragma once

#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_vulkan.h>
#include <optional>
#include "client/Render/RenderAPI.h"

namespace Eng::client {

class VulkanAPI final : public IRenderAPI {
public:
    VulkanAPI() {}
    ~VulkanAPI() {}

    bool Initialize(int width, int height) override {return true;}
    void Shutdown() override {}

    void SetViewport(int x, int y, int w, int h) override {}
    void SetClearColor(float r, float g, float b, float a) override {}
    void Clear() override {}

    void SetViewMatrix(const glm::mat4& view) override {}
    void SetProjectionMatrix(const glm::mat4& proj) override {}
    void SetModelMatrix(const glm::mat4& model) override {}

    MeshHandle CreateMesh(const MeshData& data) override {return 0;}
    TextureHandle CreateTexture(const std::string& path) override {return 0;}
    ShaderHandle CreateShader(const std::string& vertPath, const std::string& fragPath) override {return 0;}

    void DestroyMesh(MeshHandle handle) override {}
    void DestroyTexture(TextureHandle handle) override {}
    void DestroyShader(ShaderHandle handle) override {}
    Model LoadModel(const std::string& path) override {return {0, 0};}

    void SetUniform(ShaderHandle shader, const std::string& name, const glm::mat4& value) override {}
    void SetUniform(ShaderHandle shader, const std::string& name, const glm::vec3& value) override {}
    void SetUniform(ShaderHandle shader, const std::string& name, float value) override {}
    void SetUniform(ShaderHandle shader, const std::string& name, int value) override {}

    void DrawMesh(MeshHandle mesh, ShaderHandle shader, const Material& material) override {}

    void DrawSkybox(TextureHandle cubemap, ShaderHandle shader, const glm::mat4& view) override {}


private:

    static constexpr int vulkan_api_major_version = 1;
    static constexpr int vulkan_api_minor_version = 4;

};

}