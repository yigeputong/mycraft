#include "client/Render/OpenGLAPI.h"
#include "client/Window.h"
#include "core/Log.h"

#include <SDL3/SDL_pixels.h>
#include <cstddef>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>

#include <SDL3/SDL_video.h>
#include <SDL3_image/SDL_image.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <glad/glad.h>

#define CHECK_GL(tag) do { \
    GLenum e; \
    while ((e = glGetError()) != GL_NO_ERROR) \
        logError(m_logger, "[GL] " << tag << " error=" << e); \
} while(0)

namespace {
    std::vector<char> ReadBinaryFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return {};
        auto size = f.tellg();
        f.seekg(0);
        std::vector<char> buf(static_cast<size_t>(size));
        f.read(buf.data(), size);
        return buf;
    }

    GLuint CompileShaderSPIRV(GLenum stage, const std::vector<char>& spv) {
        GLuint shader = glCreateShader(stage);
        glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V,
                    spv.data(), static_cast<GLsizei>(spv.size()));
        glSpecializeShader(shader, "main", 0, nullptr, nullptr);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char logBuf[2048];
            glGetShaderInfoLog(shader, sizeof(logBuf), nullptr, logBuf);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    std::vector<uint8_t> GenerateMissingTexturePixels() {
        constexpr int kSize = 16;
        std::vector<uint8_t> pixels(static_cast<size_t>(kSize * kSize) * 4);
        const uint8_t magenta[4] = {248, 0, 248, 255};
        const uint8_t black[4]   = {0, 0, 0, 255};
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                int idx = (y * kSize + x) * 4;
                bool isMagenta = ((x / 8) + (y / 8)) % 2 == 0;
                const uint8_t* color = isMagenta ? magenta : black;
                pixels[idx + 0] = color[0];
                pixels[idx + 1] = color[1];
                pixels[idx + 2] = color[2];
                pixels[idx + 3] = color[3];
            }
        }
        return pixels;
    }
}

namespace Eng::client {

OpenGLAPI::OpenGLAPI() = default;

OpenGLAPI::~OpenGLAPI() {
    try { Shutdown(); }
    catch (const std::exception& e) { std::fprintf(stderr, "[OpenGLAPI] Shutdown threw: %s\n", e.what()); }
    catch (...) { std::fputs("[OpenGLAPI] Shutdown threw unknown\n", stderr); }
}

void OpenGLAPI::Initialize(int width, int height, Window* window) {
    if (m_initialized) return;
    m_window = window;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    SetViewport(0, 0, width, height);
    m_initialized = true;

    auto missingPixels = GenerateMissingTexturePixels();
    m_defaultTexturePtr = CreateTexture(missingPixels.data(), 16, 16);
    if (!m_defaultTexturePtr) {
        throw std::runtime_error("Failed to create default missing texture");
    }

    glGenBuffers(1, &m_globalUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_globalUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Eng::client::GlobalState), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_globalUBO);

    constexpr GLsizeiptr kMaterialUBOSize = sizeof(float) * 4;
    glGenBuffers(1, &m_materialUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_materialUBO);
    glBufferData(GL_UNIFORM_BUFFER, kMaterialUBOSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_materialUBO);
}

void OpenGLAPI::Shutdown() {
    if (!m_initialized) return;

    if (m_globalUBO)   { glDeleteBuffers(1, &m_globalUBO); m_globalUBO = 0; }
    if (m_materialUBO) { glDeleteBuffers(1, &m_materialUBO); m_materialUBO = 0; }
    if (m_fullscreenVAO) { glDeleteVertexArrays(1, &m_fullscreenVAO); m_fullscreenVAO = 0; }
    if (m_fullscreenVBO) { glDeleteBuffers(1, &m_fullscreenVBO); m_fullscreenVBO = 0; }

    m_skyCubeMesh.reset();

    for (auto& [handle, mesh] : m_meshes) {
        if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
        if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.ebo) glDeleteBuffers(1, &mesh.ebo);
        if (mesh.instanceVBO) glDeleteBuffers(1, &mesh.instanceVBO);
    }
    m_meshes.clear();

    for (auto& [handle, tex] : m_textures) {
        if (tex.textureID) glDeleteTextures(1, &tex.textureID);
    }
    m_textures.clear();
    m_defaultTexturePtr.reset();

    for (auto& [handle, shader] : m_shaders) {
        if (shader.program) glDeleteProgram(shader.program);
    }
    m_shaders.clear();

    m_initialized = false;
    logInfo(m_logger, "[RenderAPI] Shutdown");
}

void OpenGLAPI::SetViewport(int x, int y, int w, int h) {
    m_viewportX = x; m_viewportY = y;
    m_viewportWidth = w; m_viewportHeight = h;
    glViewport(x, y, w, h);
}

void OpenGLAPI::SetClearColor(float r, float g, float b, float a) {
    m_clearColor[0] = r; m_clearColor[1] = g;
    m_clearColor[2] = b; m_clearColor[3] = a;
    glClearColor(r, g, b, a);
}

void OpenGLAPI::Clear(rhi::ClearFlag flags) {
    GLbitfield mask = 0;
    if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(rhi::ClearFlag::Color))   mask |= GL_COLOR_BUFFER_BIT;
    if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(rhi::ClearFlag::Depth))   mask |= GL_DEPTH_BUFFER_BIT;
    if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(rhi::ClearFlag::Stencil)) mask |= GL_STENCIL_BUFFER_BIT;
    glClear(mask);
}

void OpenGLAPI::SetGlobalState(const GlobalState& state) {
    m_globalState = state;
}

// ==========================================
// 内部销毁逻辑
// ==========================================
void OpenGLAPI::DestroyMeshInternal(MeshHandle h) {
    auto it = m_meshes.find(h);
    if (it == m_meshes.end()) return;
    if (it->second.vao) glDeleteVertexArrays(1, &it->second.vao);
    if (it->second.vbo) glDeleteBuffers(1, &it->second.vbo);
    if (it->second.ebo) glDeleteBuffers(1, &it->second.ebo);
    if (it->second.instanceVBO) glDeleteBuffers(1, &it->second.instanceVBO);
    m_meshes.erase(it);
}

void OpenGLAPI::DestroyTextureInternal(TextureHandle h) {
    auto it = m_textures.find(h);
    if (it == m_textures.end()) return;
    if (it->second.textureID) glDeleteTextures(1, &it->second.textureID);
    m_textures.erase(it);
}

void OpenGLAPI::DestroyShaderInternal(ShaderHandle h) {
    auto it = m_shaders.find(h);
    if (it == m_shaders.end()) return;
    if (it->second.program) glDeleteProgram(it->second.program);
    m_shaders.erase(it);
}

void OpenGLAPI::DestroyFramebufferInternal(FramebufferHandle h) {
    auto it = m_framebuffers.find(h);
    if (it == m_framebuffers.end()) return;

    glDeleteFramebuffers(1, &it->second.fbo);
    if (it->second.depthBuf) glDeleteRenderbuffers(1, &it->second.depthBuf);

    auto thIt = m_fbColorHandles.find(h);
    if (thIt != m_fbColorHandles.end()) {
        m_fbColorHandles.erase(thIt);
    }
    m_framebuffers.erase(it);
}

// ==========================================
// 资源创建
// ==========================================
MeshHandle OpenGLAPI::CreateMeshInternal(const MeshData& data) {
    MeshDataInternal internal;
    glCreateVertexArrays(1, &internal.vao);
    glCreateBuffers(1, &internal.vbo);
    glNamedBufferData(internal.vbo, data.vertices.size() * sizeof(Vertex), data.vertices.data(), GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(internal.vao, 0, internal.vbo, 0, sizeof(Vertex));

    glVertexArrayAttribFormat(internal.vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(internal.vao, 0, 0);
    glEnableVertexArrayAttrib(internal.vao, 0);
    glVertexArrayAttribFormat(internal.vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribBinding(internal.vao, 1, 0);
    glEnableVertexArrayAttrib(internal.vao, 1);
    glVertexArrayAttribFormat(internal.vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    glVertexArrayAttribBinding(internal.vao, 2, 0);
    glEnableVertexArrayAttrib(internal.vao, 2);
    glVertexArrayAttribFormat(internal.vao, 3, 1, GL_FLOAT, GL_FALSE, offsetof(Vertex, ao));
    glVertexArrayAttribBinding(internal.vao, 3, 0);
    glEnableVertexArrayAttrib(internal.vao, 3);

    glCreateBuffers(1, &internal.instanceVBO);
    glm::mat4 identity(1.0f);
    glNamedBufferData(internal.instanceVBO, sizeof(glm::mat4), &identity, GL_DYNAMIC_DRAW);
    glVertexArrayVertexBuffer(internal.vao, 1, internal.instanceVBO, 0, sizeof(glm::mat4));
    for (int i = 0; i < 4; ++i) {
        glVertexArrayAttribFormat(internal.vao, 4 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4) * i);
        glVertexArrayAttribBinding(internal.vao, 4 + i, 1);
        glEnableVertexArrayAttrib(internal.vao, 4 + i);
    }
    glVertexArrayBindingDivisor(internal.vao, 1, 1);

    glVertexArrayAttribFormat(internal.vao, 8, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, light));
    glVertexArrayAttribBinding(internal.vao, 8, 0);
    glEnableVertexArrayAttrib(internal.vao, 8);

    if (!data.indices.empty()) {
        glCreateBuffers(1, &internal.ebo);
        glNamedBufferData(internal.ebo, data.indices.size() * sizeof(uint32_t), data.indices.data(), GL_STATIC_DRAW);
        glVertexArrayElementBuffer(internal.vao, internal.ebo);
        internal.indexCount = data.indices.size();
        internal.indexType  = GL_UNSIGNED_INT;
    } else {
        internal.ebo        = 0;
        internal.indexCount = data.vertices.size();
        internal.indexType  = GL_NONE;
    }

    MeshHandle handle = m_nextMeshHandle++;
    m_meshes[handle] = internal;
    return handle;
}

MeshPtr OpenGLAPI::CreateMesh(const MeshData& data) {
    MeshHandle h = CreateMeshInternal(data);
    if (h == 0) return nullptr;
    return MeshPtr(new Mesh(h), [this](Mesh* m) {
        this->DestroyMeshInternal(m->GetHandle());
        delete m;
    });
}

TexturePtr OpenGLAPI::CreateTexture(const uint8_t* rgba, int w, int h) {
    TextureHandle th = CreateTextureInternal(rgba, w, h);
    if (th == 0) return nullptr;
    return TexturePtr(new Texture(th), [this](Texture* t) {
        this->DestroyTextureInternal(t->GetHandle());
        delete t;
    });
}

TextureHandle OpenGLAPI::CreateTextureInternal(const uint8_t* rgba, int w, int h) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    TextureHandle handle = m_nextTextureHandle++;
    m_textures[handle] = { tex, w, h, GL_RGBA };
    return handle;
}

ShaderHandle OpenGLAPI::CreateShaderInternal(const ShaderCreateInfo& info) {
    auto isSPV = [](const std::string& p) {
        return p.size() > 4 && p.compare(p.size() - 4, 4, ".spv") == 0;
    };

    GLuint vertex = 0, fragment = 0;
    if (isSPV(info.vertPath)) {
        auto spv = ReadBinaryFile(info.vertPath);
        vertex = CompileShaderSPIRV(GL_VERTEX_SHADER, spv);
    } else {
        vertex = CompileShader(GL_VERTEX_SHADER, ReadFile(info.vertPath));
    }
    if (!vertex) return 0;

    if (isSPV(info.fragPath)) {
        auto spv = ReadBinaryFile(info.fragPath);
        fragment = CompileShaderSPIRV(GL_FRAGMENT_SHADER, spv);
    } else {
        fragment = CompileShader(GL_FRAGMENT_SHADER, ReadFile(info.fragPath));
    }
    if (!fragment) { glDeleteShader(vertex); return 0; }

    GLuint program = LinkProgram(vertex, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!program) return 0;

    ShaderHandle handle = m_nextShaderHandle++;
    m_shaders[handle] = { program, info.cullMode, info.depthMode, info.blendMode }; // ★ 存入状态
    return handle;
}

ShaderPtr OpenGLAPI::CreateShader(const ShaderCreateInfo& info) {
    ShaderHandle h = CreateShaderInternal(info);
    if (h == 0) return nullptr;
    return ShaderPtr(new Shader(h), [this](Shader* s) {
        this->DestroyShaderInternal(s->GetHandle());
        delete s;
    });
}

FramebufferHandle OpenGLAPI::CreateFramebufferInternal(const FramebufferCreateInfo& info) {
    if (!m_fullscreenShader) {
        ShaderCreateInfo shaderInfo{};
        shaderInfo.vertPath = "./assets/shaders/build/post/vert.gl.spv";
        shaderInfo.fragPath = "./assets/shaders/build/post/frag.gl.spv";
        m_fullscreenShader = CreateShader(shaderInfo);
    }
    if (m_fullscreenVAO == 0) {
        static constexpr float vertices[] = {
            -1.0f,  1.0f,   0.0f, 1.0f,  -1.0f, -1.0f,   0.0f, 0.0f,
             1.0f, -1.0f,   1.0f, 0.0f,  -1.0f,  1.0f,   0.0f, 1.0f,
             1.0f, -1.0f,   1.0f, 0.0f,   1.0f,  1.0f,   1.0f, 1.0f
        };
        glCreateVertexArrays(1, &m_fullscreenVAO);
        glCreateBuffers(1, &m_fullscreenVBO);
        glNamedBufferData(m_fullscreenVBO, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glVertexArrayVertexBuffer(m_fullscreenVAO, 0, m_fullscreenVBO, 0, 4 * sizeof(float));
        glVertexArrayAttribFormat(m_fullscreenVAO, 0, 2, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(m_fullscreenVAO, 0, 0);
        glEnableVertexArrayAttrib(m_fullscreenVAO, 0);
        glVertexArrayAttribFormat(m_fullscreenVAO, 1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
        glVertexArrayAttribBinding(m_fullscreenVAO, 1, 0);
        glEnableVertexArrayAttrib(m_fullscreenVAO, 1);
    }

    GLuint fbo, colorTex;
    glCreateFramebuffers(1, &fbo);
    glCreateTextures(GL_TEXTURE_2D, 1, &colorTex);
    
    GLenum internalFormat = GL_RGBA8;
    if (info.colorFormat == rhi::TextureFormat::RGBA16_SFLOAT) internalFormat = GL_RGBA16F;
    if (info.colorFormat == rhi::TextureFormat::R8_UNORM)      internalFormat = GL_R8;

    glTextureStorage2D(colorTex, 1, internalFormat, info.width, info.height);
    
    glTextureParameteri(colorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(colorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(colorTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(colorTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, colorTex, 0);

    GLuint depthBuf = 0;
    if (info.hasDepth) {
        glCreateRenderbuffers(1, &depthBuf);
        glNamedRenderbufferStorage(depthBuf, GL_DEPTH24_STENCIL8, info.width, info.height);
        glNamedFramebufferRenderbuffer(fbo, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuf);
    }

    FramebufferHandle h = m_nextFramebufferHandle++;
    m_framebuffers[h] = { fbo, colorTex, depthBuf, info.width, info.height };
    
    TextureHandle texHandle = m_nextTextureHandle++;
    m_textures[texHandle] = { colorTex, info.width, info.height, internalFormat };
    m_fbColorHandles[h] = texHandle;
    return h;
}

FramebufferPtr OpenGLAPI::CreateFramebuffer(const FramebufferCreateInfo& info) {
    FramebufferHandle h = CreateFramebufferInternal(info);
    if (h == 0) return nullptr;

    TextureHandle texH = m_fbColorHandles[h];
    TexturePtr colorTex = TexturePtr(new Texture(texH), [this](Texture* t) {
        this->DestroyTextureInternal(t->GetHandle());
        delete t;
    });

    return FramebufferPtr(new Framebuffer(h, colorTex, info.width, info.height), [this](Framebuffer* f) {
        this->DestroyFramebufferInternal(f->GetHandle());
        delete f;
    });
}

void OpenGLAPI::BindFramebuffer(const FramebufferPtr& fb) {
    if (!fb) return;
    auto it = m_framebuffers.find(fb->GetHandle());
    if (it == m_framebuffers.end()) return;
    glBindFramebuffer(GL_FRAMEBUFFER, it->second.fbo);
    glViewport(0, 0, it->second.width, it->second.height);
}

void OpenGLAPI::UnbindFramebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (m_window) {
        int w, h;
        SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
        glViewport(0, 0, w, h);
    }
}

void OpenGLAPI::SetUniform(const ShaderPtr& shader, const std::string& name, const glm::mat4& value) {
    if (!shader) return;
    auto it = m_shaders.find(shader->GetHandle());
    if (it == m_shaders.end() || !it->second.program) return;
    glUseProgram(it->second.program);
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc != -1) glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}

void OpenGLAPI::SetUniform(const ShaderPtr& shader, const std::string& name, const glm::vec3& value) {
    if (!shader) return;
    auto it = m_shaders.find(shader->GetHandle());
    if (it == m_shaders.end() || !it->second.program) return;
    glUseProgram(it->second.program);
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc != -1) glUniform3fv(loc, 1, glm::value_ptr(value));
}

void OpenGLAPI::SetUniform(const ShaderPtr& shader, const std::string& name, float value) {
    if (!shader) return;
    auto it = m_shaders.find(shader->GetHandle());
    if (it == m_shaders.end() || !it->second.program) return;
    glUseProgram(it->second.program);
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc != -1) glUniform1f(loc, value);
}

void OpenGLAPI::SetUniform(const ShaderPtr& shader, const std::string& name, int value) {
    if (!shader) return;
    auto it = m_shaders.find(shader->GetHandle());
    if (it == m_shaders.end() || !it->second.program) return;
    glUseProgram(it->second.program);
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc != -1) glUniform1i(loc, value);
}

void OpenGLAPI::BeginFrame() {
    glDisable(GL_SCISSOR_TEST);
    if (m_window) {
        int w, h;
        SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
        glViewport(0, 0, w, h);
    }
    if (m_globalUBO) {
        glNamedBufferSubData(m_globalUBO, 0, sizeof(GlobalState), &m_globalState);
    }
}

void OpenGLAPI::ApplyState(const ShaderDataInternal& shaderData) {
    if (shaderData.depthMode == rhi::DepthMode::Disabled) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
        if (shaderData.depthMode == rhi::DepthMode::TestOnly) glDepthMask(GL_FALSE);
        else glDepthMask(GL_TRUE);
    }

    if (shaderData.cullMode == rhi::CullMode::None) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(shaderData.cullMode == rhi::CullMode::Back ? GL_BACK : GL_FRONT);
    }

    if (shaderData.blendMode == rhi::BlendMode::None) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        if (shaderData.blendMode == rhi::BlendMode::AlphaBlend) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else if (shaderData.blendMode == rhi::BlendMode::Additive) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        }
    }
}

void OpenGLAPI::DrawMesh(const MeshPtr& mesh, const ShaderPtr& shader, const Material& material, const glm::mat4& model) {
    if (!m_initialized || !mesh || !shader) return;
    auto meshIt = m_meshes.find(mesh->GetHandle());
    auto shaderIt = m_shaders.find(shader->GetHandle());
    if (meshIt == m_meshes.end() || shaderIt == m_shaders.end()) return;

    auto& meshData = meshIt->second;
    glUseProgram(shaderIt->second.program);
    ApplyState(shaderIt->second);

    float matData[4] = { material.shininess, 0.0f, 0.0f, 0.0f };
    glNamedBufferSubData(m_materialUBO, 0, sizeof(matData), matData);
    glNamedBufferSubData(meshData.instanceVBO, 0, sizeof(glm::mat4), &model);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ResolveTexture(material.diffuse));

    glBindVertexArray(meshData.vao);
    if (meshData.ebo != 0) glDrawElements(GL_TRIANGLES, (GLsizei)meshData.indexCount, meshData.indexType, nullptr);
    else glDrawArrays(GL_TRIANGLES, 0, (GLsizei)meshData.indexCount);
    glBindVertexArray(0);
}

void OpenGLAPI::DrawMeshInstanced(const MeshPtr& mesh, const ShaderPtr& shader, const Material& material, const std::vector<glm::mat4>& transforms) {
    if (transforms.empty() || !mesh || !shader) return;
    auto meshIt = m_meshes.find(mesh->GetHandle());
    auto shaderIt = m_shaders.find(shader->GetHandle());
    if (meshIt == m_meshes.end() || shaderIt == m_shaders.end()) return;

    auto& meshData = meshIt->second;
    glUseProgram(shaderIt->second.program);
    ApplyState(shaderIt->second);

    glNamedBufferData(meshData.instanceVBO, transforms.size() * sizeof(glm::mat4), transforms.data(), GL_DYNAMIC_DRAW);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ResolveTexture(material.diffuse));

    glBindVertexArray(meshData.vao);
    if (meshData.ebo != 0) glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)meshData.indexCount, meshData.indexType, nullptr, (GLsizei)transforms.size());
    else glDrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei)meshData.indexCount, (GLsizei)transforms.size());
    glBindVertexArray(0);
}

void OpenGLAPI::DrawMeshBatch(const std::vector<MeshPtr>& meshes, const ShaderPtr& shader, const Material& material) {
    if (meshes.empty() || !shader) return;
    auto shaderIt = m_shaders.find(shader->GetHandle());
    if (shaderIt == m_shaders.end()) return;

    glUseProgram(shaderIt->second.program);
    ApplyState(shaderIt->second);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ResolveTexture(material.diffuse));

    const glm::mat4 identity(1.0f);
    for (const auto& m : meshes) {
        if (!m) continue;
        auto meshIt = m_meshes.find(m->GetHandle());
        if (meshIt == m_meshes.end()) continue;
        auto& meshData = meshIt->second;
        glNamedBufferSubData(meshData.instanceVBO, 0, sizeof(glm::mat4), &identity);
        glBindVertexArray(meshData.vao);
        if (meshData.ebo != 0) glDrawElements(GL_TRIANGLES, (GLsizei)meshData.indexCount, meshData.indexType, nullptr);
        else glDrawArrays(GL_TRIANGLES, 0, (GLsizei)meshData.indexCount);
    }
    glBindVertexArray(0);
}

void OpenGLAPI::DrawSkybox(const ShaderPtr& shader) {
    if (!shader) return;
    auto it = m_shaders.find(shader->GetHandle());
    if (it == m_shaders.end()) return;

    if (!m_skyCubeMesh) {
        const float s = 1.0f;
        float verts[] = {
             s,-s,-s,  s,-s, s,  s, s, s,   s, s, s,  s, s,-s,  s,-s,-s,
            -s,-s, s, -s,-s,-s, -s, s,-s,  -s, s,-s, -s, s, s, -s,-s, s,
            -s, s,-s,  s, s,-s,  s, s, s,   s, s, s, -s, s, s, -s, s,-s,
            -s,-s, s,  s,-s, s,  s,-s,-s,   s,-s,-s, -s,-s,-s, -s,-s, s,
            -s,-s, s,  s,-s, s,  s, s, s,   s, s, s, -s, s, s, -s,-s, s,
             s,-s,-s, -s,-s,-s, -s, s,-s,  -s, s,-s,  s, s,-s,  s,-s,-s,
        };
        std::vector<Vertex> v(36);
        for (size_t i = 0; i < 36; ++i) {
            v[i].position = { verts[i*3], verts[i*3+1], verts[i*3+2] };
            v[i].normal = { 0, 0, 0 };
            v[i].uv = { 0, 0 };
        }
        MeshData md{ v, {} };
        m_skyCubeMesh = CreateMesh(md);
    }

    glUseProgram(it->second.program);
    ApplyState(it->second);

    auto meshit = m_meshes.find(m_skyCubeMesh->GetHandle());
    if (meshit != m_meshes.end()) {
        glBindVertexArray(meshit->second.vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }
}

void OpenGLAPI::DrawFullscreenQuad(const TexturePtr& texture) {
    if (!texture) return;
    
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if (m_window) {
        int w, h;
        SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
        glViewport(0, 0, w, h);
    }

    glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_fullscreenShader) return;
    auto shader = m_shaders.find(m_fullscreenShader->GetHandle())->second.program;

    glUseProgram(shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ResolveTexture(texture));
    glUniform1i(glGetUniformLocation(shader, "screenTexture"), 0);

    glBindVertexArray(m_fullscreenVAO); 
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void OpenGLAPI::EndFrame() {
    ImGui::Render();
    ImGuiRenderDrawData();
    SDL_GL_SwapWindow(m_window->GetSDLWindow());
}

std::string OpenGLAPI::ReadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint OpenGLAPI::CompileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint OpenGLAPI::LinkProgram(GLuint vertexShader, GLuint fragmentShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool OpenGLAPI::InitImGuiBackend() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!ImGui_ImplSDL3_InitForOpenGL(m_window->GetSDLWindow(), m_window->GetGLContext())) return false;
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) return false;
    return true;
}

void OpenGLAPI::ShutdownImGuiBackend() {
    if (ImGui::GetCurrentContext() == nullptr) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

void OpenGLAPI::ImGuiNewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

void OpenGLAPI::ImGuiRenderDrawData() {
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

[[nodiscard]] DeviceInfo OpenGLAPI::GetDeviceInfo() const {
    DeviceInfo info;
    info.backend = "OpenGL";
    auto getStr = [](GLenum e) -> std::string {
        auto p = glGetString(e);
        return p ? reinterpret_cast<const char*>(p) : "(null)";
    };
    info.deviceName = getStr(GL_RENDERER);
    info.vendor = getStr(GL_VENDOR);
    GLint major, minor;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    info.apiVersion = std::to_string(major) + "." + std::to_string(minor);
    info.driverVersion = getStr(GL_VERSION);
    info.shadingLanguage = "GLSL " + getStr(GL_SHADING_LANGUAGE_VERSION);
    return info;
}

void OpenGLAPI::ApplySettings(const WindowConfig& win, [[maybe_unused]] const RenderConfig& render) {
    SDL_GL_SetSwapInterval(win.vsync ? 1 : 0);
}

void OpenGLAPI::WaitIdle() {
    glFinish();
}

Eng::client::BackendAvailability OpenGLAPI::Probe() {
    Eng::client::BackendAvailability r;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, OpenGLAPI::api_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, OpenGLAPI::api_minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* w = SDL_CreateWindow("probe", 1, 1, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    if (!w) { r.reason = "Failed to create GL window"; return r; }
    SDL_GLContext ctx = SDL_GL_CreateContext(w);
    if (!ctx) { SDL_DestroyWindow(w); r.reason = "Failed to create GL context"; return r; }
    SDL_GL_MakeCurrent(w, ctx);
    if (gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress) == 0) {
        r.reason = "Failed to load GL pointers";
    } else {
        r.available = true;
    }
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(w);
    return r;
}

} // namespace Eng::client