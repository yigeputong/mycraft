#include "Engine/service/OpenGLRenderService.h"
#include "Engine/tool/Window.h"
#include "Engine/tool/Log.h"
#include "imgui.h"

#include <SDL3/SDL_pixels.h>
#include <cstddef>
#include <vector>
#include <array>
#include <cmath>

#include <SDL3/SDL_video.h>
#include <SDL3_image/SDL_image.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <glad/glad.h>

#define CHECK_GL(tag) do { \
    GLenum e; \
    while ((e = glGetError()) != GL_NO_ERROR) \
        logError(m_logger, "[GL] " << (tag) << " error=" << e); \
} while(0)

namespace {
    GLuint CompileShaderSPIRV(GLenum stage, std::span<const std::byte> spv) {
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

namespace Eng::service {

OpenGLRenderService::OpenGLRenderService() = default;

OpenGLRenderService::~OpenGLRenderService() {
    try { Shutdown(); }
    catch (const std::exception& e) { std::fprintf(stderr, "[OpenGLRenderService] Shutdown threw: %s\n", e.what()); }
    catch (...) { std::fputs("[OpenGLRenderService] Shutdown threw unknown\n", stderr); }
}

void OpenGLRenderService::Initialize(int width, int height, tool::Window* window) {
    if (m_initialized) return;
    m_window = window;

    m_glContext = SDL_GL_CreateContext(m_window->GetSDLWindow());
    if (!m_glContext) {
        throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
    }
    SDL_GL_MakeCurrent(m_window->GetSDLWindow(), m_glContext);
    SDL_GL_SetSwapInterval(m_window->GetConfigs().vsync ? 1 : 0);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        SDL_GL_DestroyContext(m_glContext);
        m_glContext = nullptr;
        throw std::runtime_error("gladLoadGLLoader failed");
    }

    SetViewport(0, 0, width, height);
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

    auto missingPixels = GenerateMissingTexturePixels();
    rhi::TextureCreateInfo tci;
    tci.pixels = std::as_bytes(std::span(missingPixels));
    tci.width = 16;
    tci.height = 16;
    m_defaultTexturePtr = CreateTexture(tci);

    glCreateVertexArrays(1, &m_drawVAO);
    glEnableVertexArrayAttrib(m_drawVAO, 0);
    glVertexArrayAttribFormat(m_drawVAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(m_drawVAO, 0, 0);
    glEnableVertexArrayAttrib(m_drawVAO, 1);
    glVertexArrayAttribFormat(m_drawVAO, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribBinding(m_drawVAO, 1, 0);
    glEnableVertexArrayAttrib(m_drawVAO, 2);
    glVertexArrayAttribFormat(m_drawVAO, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
    glVertexArrayAttribBinding(m_drawVAO, 2, 0);
    glEnableVertexArrayAttrib(m_drawVAO, 3);
    glVertexArrayAttribFormat(m_drawVAO, 3, 1, GL_FLOAT, GL_FALSE, offsetof(Vertex, layer));
    glVertexArrayAttribBinding(m_drawVAO, 3, 0);
    glEnableVertexArrayAttrib(m_drawVAO, 4);
    glVertexArrayAttribFormat(m_drawVAO, 4, 1, GL_FLOAT, GL_FALSE, offsetof(Vertex, ao));
    glVertexArrayAttribBinding(m_drawVAO, 4, 0);
    glEnableVertexArrayAttrib(m_drawVAO, 5);
    glVertexArrayAttribFormat(m_drawVAO, 5, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, light));
    glVertexArrayAttribBinding(m_drawVAO, 5, 0);

    m_initialized = true;
}

void OpenGLRenderService::Shutdown() {
    if (!m_initialized) return;

    m_deferDeleter.Flush(); // 强制清空残留队列

    if (m_drawVAO) glDeleteVertexArrays(1, &m_drawVAO);

    for (auto& [handle, tex] : m_textures) {
        if (tex.textureID) glDeleteTextures(1, &tex.textureID);
    }
    m_textures.clear();
    m_defaultTexturePtr.reset();

    for (auto& [handle, shader] : m_shaders) {
        if (shader.program) glDeleteProgram(shader.program);
    }
    m_shaders.clear();
    m_buffers.clear();

    if (m_glContext) {
        SDL_GL_DestroyContext(m_glContext);
        m_glContext = nullptr;
    }

    m_initialized = false;
    logInfo(m_logger, "[OpenGL] Shutdown complete");
}

glm::mat4 OpenGLRenderService::MakePerspectiveMatrix(
    float fovYRadians, float aspect, float nearZ, float farZ) const {
    return glm::perspective(fovYRadians, aspect, nearZ, farZ);
}

void OpenGLRenderService::SetViewport(int x, int y, int w, int h) {
    m_viewportX = x; m_viewportY = y;
    m_viewportWidth = w; m_viewportHeight = h;
    glViewport(x, y, w, h);
}

void OpenGLRenderService::Clear(rhi::ClearFlag flag, const glm::vec4& color, float depth, uint8_t stencil) {
    GLbitfield mask = 0;
    const uint32_t bits = static_cast<uint32_t>(flag);

    if (bits & static_cast<uint32_t>(rhi::ClearFlag::Color)) {
        glClearColor(color.r, color.g, color.b, color.a);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (bits & static_cast<uint32_t>(rhi::ClearFlag::Depth)) {
        glClearDepth(static_cast<double>(depth));
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (bits & static_cast<uint32_t>(rhi::ClearFlag::Stencil)) {
        glClearStencil(static_cast<GLint>(stencil));
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    if (mask) glClear(mask);
}

void OpenGLRenderService::BeginFrame() {
    glDisable(GL_SCISSOR_TEST);
    if (m_window) {
        int w, h;
        SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
        glViewport(0, 0, w, h);
    }

    m_frameCounter++;
    m_deferDeleter.Process(m_frameCounter); // 处理队列
}

void OpenGLRenderService::EndFrame() {
    ImGui::Render();
    ImGuiRenderDrawData();

    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    SDL_GL_SwapWindow(m_window->GetSDLWindow());
}

// ==========================================
// 资源创建
// ==========================================
BufferPtr OpenGLRenderService::CreateBuffer(const rhi::BufferCreateInfo& info) {
    if (info.size == 0) return nullptr;
    BufferHandle h = CreateBufferInternal(info.size, info.usage);
    if (h == 0) return nullptr;
    return BufferPtr(new Buffer(h), [this](Buffer* b) {
        this->DestroyBufferInternal(b->GetHandle());
        delete b;
    });
}

TexturePtr OpenGLRenderService::CreateTexture(const rhi::TextureCreateInfo& info) {
    if (info.format != rhi::TextureFormat::RGBA8_UNORM) {
        logWarning(m_logger, "[OpenGL] CreateTexture: only RGBA8_UNORM supported.");
        return nullptr;
    }
    if (!info.pixels.data() || info.width <= 0 || info.height <= 0) return nullptr;

    TextureHandle th = CreateTextureInternal(info);
    if (th == 0) return nullptr;
    return TexturePtr(new Texture(th), [this](Texture* t) {
        this->DestroyTextureInternal(t->GetHandle());
        delete t;
    });
}

ShaderPtr OpenGLRenderService::CreateShader(const rhi::ShaderCreateInfo& info) {
    ShaderHandle h = CreateShaderInternal(info);
    if (h == 0) return nullptr;
    return ShaderPtr(new Shader(h), [this](Shader* s) {
        this->DestroyShaderInternal(s->GetHandle());
        delete s;
    });
}

FramebufferPtr OpenGLRenderService::CreateFramebuffer(const rhi::FramebufferCreateInfo& info) {
    GLuint fbo, colorTex;
    glCreateFramebuffers(1, &fbo);
    glCreateTextures(GL_TEXTURE_2D, 1, &colorTex);

    GLenum internalFormat = GL_RGBA8;
    if (info.colorFormat == rhi::TextureFormat::RGBA16_SFLOAT) internalFormat = GL_RGBA16F;
    if (info.colorFormat == rhi::TextureFormat::R8_UNORM)      internalFormat = GL_R8;

    glTextureStorage2D(colorTex, 1, internalFormat, info.width, info.height);
    glTextureParameteri(colorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(colorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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

    TexturePtr colorTexturePtr = TexturePtr(new Texture(texHandle), [this](Texture* t) {
        this->DestroyTextureInternal(t->GetHandle());
        delete t;
    });

    return FramebufferPtr(new Framebuffer(h, colorTexturePtr, info.width, info.height),
        [this](Framebuffer* f) {
            auto it = m_framebuffers.find(f->GetHandle());
            if (it != m_framebuffers.end()) {
                glDeleteFramebuffers(1, &it->second.fbo);
                if (it->second.depthBuf) glDeleteRenderbuffers(1, &it->second.depthBuf);
                m_fbColorHandles.erase(f->GetHandle());
                m_framebuffers.erase(it);
            }
            delete f;
        });
}

void OpenGLRenderService::UpdateBuffer(const BufferPtr& buffer, size_t offset, std::span<const std::byte> data) {
    if (!buffer) return;
    auto it = m_buffers.find(buffer->GetHandle());
    if (it != m_buffers.end()) {
        glNamedBufferSubData(it->second.buffer, offset, data.size(), data.data());
    }
}

// ==========================================
// 帧缓冲绑定
// ==========================================
void OpenGLRenderService::BindFramebuffer(const FramebufferPtr& fb) {
    if (!fb) return;
    auto it = m_framebuffers.find(fb->GetHandle());
    if (it != m_framebuffers.end()) {
        glBindFramebuffer(GL_FRAMEBUFFER, it->second.fbo);
        glViewport(0, 0, it->second.width, it->second.height);
    }
}

void OpenGLRenderService::UnbindFramebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (m_window) {
        int w, h;
        SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
        glViewport(0, 0, w, h);
    }
}

// ==========================================
// 无状态绑定
// ==========================================
void OpenGLRenderService::BindShader(const ShaderPtr& shader) {
    if (!shader) return;
    auto it = m_shaders.find(shader->GetHandle());
    if (it == m_shaders.end()) return;

    glUseProgram(it->second.program);
    m_currentProgram = 0;
    m_vaoDirty       = true;
    ApplyPipelineState(it->second.pipelineState);
}

void OpenGLRenderService::BindVertexBuffer(const BufferPtr& buffer, uint32_t binding) {
    if (!buffer) return;
    if (binding != 0) return;
    auto it = m_buffers.find(buffer->GetHandle());
    if (it != m_buffers.end()) {
        m_currentVBO = it->second.buffer;
        m_vaoDirty   = true;
    }
}

void OpenGLRenderService::BindIndexBuffer(const BufferPtr& buffer) {
    if (!buffer) return;
    auto it = m_buffers.find(buffer->GetHandle());
    if (it != m_buffers.end()) {
        m_currentIBO = it->second.buffer;
        m_vaoDirty   = true;
    }
}

void OpenGLRenderService::BindTexture(const TexturePtr& texture, uint32_t slot) {
    if (!texture) return;
    auto it = m_textures.find(texture->GetHandle());
    if (it != m_textures.end() && slot == 0) {
        m_currentTextureID = it->second.textureID;
    }
}

void OpenGLRenderService::BindUniformBuffer(const BufferPtr& buffer, uint32_t set, uint32_t binding) {
    if (!buffer) return;
    auto it = m_buffers.find(buffer->GetHandle());
    if (it == m_buffers.end() || it->second.buffer == 0) return;

    const GLuint glBinding = set * 16 + binding;
    glBindBufferBase(GL_UNIFORM_BUFFER, glBinding, it->second.buffer);
}

void OpenGLRenderService::PushConstants(const ShaderPtr& shader, std::span<const std::byte> data) {
    if (!shader || data.empty()) return;
    auto it = m_shaders.find(shader->GetHandle());
    if (it == m_shaders.end()) return;
    if (it->second.perObjectUBO == 0) return;

    if (data.size_bytes() > sizeof(rhi::PerObjectData)) {
        return;
    }
    glNamedBufferSubData(it->second.perObjectUBO, 0, data.size_bytes(), data.data());
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, it->second.perObjectUBO);
}

// ==========================================
// 绘制
// ==========================================
void OpenGLRenderService::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex) {
    ApplyStateIfNeeded();
    if (instanceCount > 1) {
        glDrawArraysInstanced(GL_TRIANGLES, firstVertex, vertexCount, instanceCount);
    } else {
        glDrawArrays(GL_TRIANGLES, firstVertex, vertexCount);
    }
}

void OpenGLRenderService::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset) {
    ApplyStateIfNeeded();
    const void* offsetPtr = nullptr;
    if (firstIndex != 0) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        offsetPtr = reinterpret_cast<const void*>(static_cast<uintptr_t>(firstIndex) * sizeof(uint32_t));
    }

    if (instanceCount > 1) {
        glDrawElementsInstancedBaseVertex(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, offsetPtr, instanceCount, vertexOffset);
    } else {
        glDrawElementsBaseVertex(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, offsetPtr, vertexOffset);
    }
}

void OpenGLRenderService::DrawIndexedIndirect(const BufferPtr& indirectBuffer, uint32_t drawCount, uint32_t stride) {
    if (!indirectBuffer) return;
    auto it = m_buffers.find(indirectBuffer->GetHandle());
    if (it == m_buffers.end()) return;

    ApplyStateIfNeeded();
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, it->second.buffer);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, drawCount, stride);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

// ==========================================
// 内部实现辅助
// ==========================================
TextureHandle OpenGLRenderService::CreateTextureInternal(const rhi::TextureCreateInfo& info) {
    if (!info.pixels.data() || info.width <= 0 || info.height <= 0)
        return 0;

    bool isArray = info.arrayLayers > 1;
    GLenum target = isArray ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;

    GLuint tex;
    glCreateTextures(target, 1, &tex);

    int maxPossibleMips = static_cast<int>(std::floor(std::log2(std::max(info.width, info.height)))) + 1;
    int mipLevels = 1;
    if (info.requestedMips == 0) {
        if (m_renderConfig.mipmapLevels > 0) {
            mipLevels = std::min(maxPossibleMips, m_renderConfig.mipmapLevels + 1);
        }
    } else {
        mipLevels = std::min(maxPossibleMips, info.requestedMips);
    }

    // 根据 info.format 映射 GL 的 internalFormat, format, type
    GLenum glInternalFormat = GL_RGBA8;
    GLenum glFormat = GL_RGBA;
    GLenum glType = GL_UNSIGNED_BYTE;

    switch (info.format) {
        case rhi::TextureFormat::RGBA8_UNORM:
            glInternalFormat = GL_RGBA8; glFormat = GL_RGBA; glType = GL_UNSIGNED_BYTE; break;
        case rhi::TextureFormat::RGBA16_SFLOAT:
            glInternalFormat = GL_RGBA16F; glFormat = GL_RGBA; glType = GL_FLOAT; break;
        case rhi::TextureFormat::R8_UNORM:
            glInternalFormat = GL_R8; glFormat = GL_RED; glType = GL_UNSIGNED_BYTE; break;
        case rhi::TextureFormat::D32_SFLOAT:
            // 深度图一般不通过这里手动上传数据，如果是 D32 则忽略上传，仅分配
            glInternalFormat = GL_DEPTH_COMPONENT32F; glFormat = GL_DEPTH_COMPONENT; glType = GL_FLOAT; break;
    }

    // 区分 2D 和 2D_ARRAY
    if (isArray) {
        glTextureStorage3D(tex, mipLevels, glInternalFormat, info.width, info.height, info.arrayLayers);
        if (info.format != rhi::TextureFormat::D32_SFLOAT) {
            glTextureSubImage3D(tex, 0, 0, 0, 0, info.width, info.height, info.arrayLayers, glFormat, glType, info.pixels.data());
        }
    } else {
        glTextureStorage2D(tex, mipLevels, glInternalFormat, info.width, info.height);
        if (info.format != rhi::TextureFormat::D32_SFLOAT) {
            glTextureSubImage2D(tex, 0, 0, 0, info.width, info.height, glFormat, glType, info.pixels.data());
        }
    }
    
    if (mipLevels > 1) {
        glGenerateTextureMipmap(tex);
    }

    // MAG_FILTER
    GLint glMag = (info.magFilter == rhi::FilterMode::Linear) ? GL_LINEAR : GL_NEAREST;
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, glMag);

    // MIN_FILTER
    GLint glMin = GL_NEAREST;
    switch (info.minFilter) {
        case rhi::MinFilterMode::Nearest:                glMin = GL_NEAREST; break;
        case rhi::MinFilterMode::Linear:                 glMin = GL_LINEAR; break;
        case rhi::MinFilterMode::Nearest_Mipmap_Nearest: glMin = GL_NEAREST_MIPMAP_NEAREST; break;
        case rhi::MinFilterMode::Linear_Mipmap_Nearest:  glMin = GL_LINEAR_MIPMAP_NEAREST; break;
        case rhi::MinFilterMode::Nearest_Mipmap_Linear:  glMin = GL_NEAREST_MIPMAP_LINEAR; break;
        case rhi::MinFilterMode::Linear_Mipmap_Linear:   glMin = GL_LINEAR_MIPMAP_LINEAR; break;
        default:                                         glMin = GL_NEAREST; break;
    }
    
    if (mipLevels <= 1) {
        if (glMin == GL_NEAREST_MIPMAP_NEAREST || glMin == GL_NEAREST_MIPMAP_LINEAR) glMin = GL_NEAREST;
        else if (glMin == GL_LINEAR_MIPMAP_NEAREST || glMin == GL_LINEAR_MIPMAP_LINEAR) glMin = GL_LINEAR;
    }
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, glMin);

    // WRAP_MODE
    GLint glWrapU = (info.wrapU == rhi::WrapMode::Repeat) ? GL_REPEAT : 
                    (info.wrapU == rhi::WrapMode::MirroredRepeat ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE);
    GLint glWrapV = (info.wrapV == rhi::WrapMode::Repeat) ? GL_REPEAT : 
                    (info.wrapV == rhi::WrapMode::MirroredRepeat ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, glWrapU);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, glWrapV);

    // 各向异性过滤
    if (mipLevels > 1 && info.enableAnisotropy && m_renderConfig.enableAnisotropic && GLAD_GL_EXT_texture_filter_anisotropic) {
        GLfloat maxAniso = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        float aniso = std::min(static_cast<float>(m_renderConfig.maxAnisotropy), maxAniso);
        glTextureParameterf(tex, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    }

    TextureHandle handle = m_nextTextureHandle++;
    m_textures[handle] = { tex, info.width, info.height, glInternalFormat };
    return handle;
}

ShaderHandle OpenGLRenderService::CreateShaderInternal(const rhi::ShaderCreateInfo& info) {
    if (!info.IsValid()) {
        logError(m_logger, "[OpenGL] CreateShaderInternal: empty SPIR-V.");
        return 0;
    }

    GLuint vertex = CompileShaderSPIRV(GL_VERTEX_SHADER, info.vertSpirv.Bytes());
    if (!vertex) return 0;

    GLuint fragment = CompileShaderSPIRV(GL_FRAGMENT_SHADER, info.fragSpirv.Bytes());
    if (!fragment) { glDeleteShader(vertex); return 0; }

    GLuint program = LinkProgram(vertex, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!program) return 0;

    ShaderDataInternal internal;
    internal.program          = program;
    internal.pushConstantSize = info.pushConstantSize;
    internal.pipelineState    = info.pipelineState.value_or(rhi::GraphicsPipelineState::PresetOpaque());

    if (info.pushConstantSize >= sizeof(rhi::PerObjectData)) {
        glCreateBuffers(1, &internal.perObjectUBO);
        glNamedBufferData(internal.perObjectUBO, sizeof(rhi::PerObjectData), nullptr, GL_DYNAMIC_DRAW);
    }

    ShaderHandle handle = m_nextShaderHandle++;
    m_shaders[handle] = internal;
    return handle;
}

BufferHandle OpenGLRenderService::CreateBufferInternal(size_t size, rhi::BufferUsage usage) {
    BufferDataInternal internal;
    glCreateBuffers(1, &internal.buffer);
    internal.size = size;
    internal.usage = usage;

    glNamedBufferData(internal.buffer, size, nullptr, GL_DYNAMIC_DRAW);

    BufferHandle handle = m_nextBufferHandle++;
    m_buffers[handle] = internal;
    return handle;
}

void OpenGLRenderService::DestroyTextureInternal(TextureHandle h) {
    auto it = m_textures.find(h);
    if (it == m_textures.end()) return;
    if (it->second.textureID) glDeleteTextures(1, &it->second.textureID);
    m_textures.erase(it);
}

void OpenGLRenderService::DestroyShaderInternal(ShaderHandle h) {
    auto it = m_shaders.find(h);
    if (it == m_shaders.end()) return;
    if (it->second.program) glDeleteProgram(it->second.program);
    if (it->second.perObjectUBO) glDeleteBuffers(1, &it->second.perObjectUBO);
    m_shaders.erase(it);
}

void OpenGLRenderService::DestroyFramebufferInternal(FramebufferHandle h) {
    auto it = m_framebuffers.find(h);
    if (it != m_framebuffers.end()) {
        glDeleteFramebuffers(1, &it->second.fbo);
        if (it->second.depthBuf) glDeleteRenderbuffers(1, &it->second.depthBuf);
        m_fbColorHandles.erase(h);
        m_framebuffers.erase(it);
    }
}

void OpenGLRenderService::DestroyBufferInternal(BufferHandle h) {
    auto it = m_buffers.find(h);
    if (it != m_buffers.end()) {
        if (it->second.buffer) {
            GLuint buf = it->second.buffer;
            // 交给通用延迟销毁器，延迟 2 帧
            m_deferDeleter.Defer([buf]() {
                glDeleteBuffers(1, &buf);
            }, m_frameCounter, 2);
        }
        m_buffers.erase(it);
    }
}

GLuint OpenGLRenderService::LinkProgram(GLuint vertexShader, GLuint fragmentShader) {
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

void OpenGLRenderService::ApplyStateIfNeeded() {
    if (!m_drawVAO) return;

    if (m_currentProgram) {
        glUseProgram(m_currentProgram);
    }

    if (m_currentVBO) {
        glVertexArrayVertexBuffer(m_drawVAO, 0, m_currentVBO, 0, sizeof(Vertex));
        m_currentVBO = 0;
    }

    if (m_currentIBO) {
        glVertexArrayElementBuffer(m_drawVAO, m_currentIBO);
        m_currentIBO = 0;
    }

    if (m_currentTextureID) {
        glBindTextureUnit(0, m_currentTextureID);
        m_currentTextureID = 0;
    }

    if (m_vaoDirty) {
        glBindVertexArray(m_drawVAO);
        m_vaoDirty = false;
    }
}

void OpenGLRenderService::ApplyPipelineState(const rhi::GraphicsPipelineState& state) {
    if (state.depthStencil.depthTestEnable) glEnable(GL_DEPTH_TEST);
    else                                    glDisable(GL_DEPTH_TEST);
    glDepthMask(state.depthStencil.depthWriteEnable ? GL_TRUE : GL_FALSE);

    switch (state.depthStencil.depthCompareOp) {
        case rhi::CompareOp::Never:          glDepthFunc(GL_NEVER); break;
        case rhi::CompareOp::Less:           glDepthFunc(GL_LESS); break;
        case rhi::CompareOp::Equal:          glDepthFunc(GL_EQUAL); break;
        case rhi::CompareOp::LessOrEqual:    glDepthFunc(GL_LEQUAL); break;
        case rhi::CompareOp::Greater:        glDepthFunc(GL_GREATER); break;
        case rhi::CompareOp::NotEqual:       glDepthFunc(GL_NOTEQUAL); break;
        case rhi::CompareOp::GreaterOrEqual: glDepthFunc(GL_GEQUAL); break;
        case rhi::CompareOp::Always:         glDepthFunc(GL_ALWAYS); break;
    }

    if (state.rasterizer.cullMode == rhi::CullMode::None) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        if (state.rasterizer.cullMode == rhi::CullMode::Front) glCullFace(GL_FRONT);
        else if (state.rasterizer.cullMode == rhi::CullMode::Back) glCullFace(GL_BACK);
        else glCullFace(GL_FRONT_AND_BACK);
    }
    glFrontFace(state.rasterizer.frontFace == rhi::FrontFace::Clockwise ? GL_CW : GL_CCW);

    if (state.blend.blendEnable) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

// ==========================================
// ImGui & 杂项
// ==========================================
bool OpenGLRenderService::InitImGuiBackend() {
    if (m_imguiInitialized) return true;

    if (!ImGui_ImplSDL3_InitForOpenGL(m_window->GetSDLWindow(), m_glContext)) {
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) {
        ImGui_ImplSDL3_Shutdown();
        return false;
    }
    m_imguiInitialized = true;
    return true;
}

void OpenGLRenderService::ShutdownImGuiBackend() {
    if (!m_imguiInitialized) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    m_imguiInitialized = false;
}

void OpenGLRenderService::ImGuiNewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void OpenGLRenderService::ImGuiRenderDrawData() {
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

DeviceInfo OpenGLRenderService::GetDeviceInfo() const {
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

void OpenGLRenderService::ApplySettings(const tool::WindowConfig& win, [[maybe_unused]] const tool::RenderConfig& render) {
    if (!m_glContext) return;
    SDL_GL_SetSwapInterval(win.vsync ? 1 : 0);
}

void OpenGLRenderService::WaitIdle() {
    glFinish();
}

Eng::service::BackendAvailability OpenGLRenderService::Probe() {
    Eng::service::BackendAvailability r;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, OpenGLRenderService::api_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, OpenGLRenderService::api_minor);
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
        r.caps.apiName = "OpenGL";
        GLint major, minor;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        r.caps.apiMajor = major;
        r.caps.apiMinor = minor;

        GLint maxTex, maxComputeWG, maxVertAttrs, maxVertBuffers;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxComputeWG);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertAttrs);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIB_BINDINGS, &maxVertBuffers);

        r.caps.maxTextureSize = maxTex;
        r.caps.maxComputeWorkGroupInvocations = maxComputeWG;
        r.caps.maxVertexAttributes = maxVertAttrs;
        r.caps.maxVertexBufferBindings = maxVertBuffers;

        GLint numExtensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
        std::vector<std::string> exts(numExtensions);
        for (GLint i = 0; i < numExtensions; ++i) {
            const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, i);
            if (ext) {
                exts[i] = ext;
                r.caps.availableExtensions.push_back(ext);
            }
        }
        
        auto hasExtension = [&](const std::string& name) {
            return std::find(exts.begin(), exts.end(), name) != exts.end();
        };

        r.caps.supportsCompute = (major > 4) || (major == 4 && minor >= 3);
        r.caps.supportsGeometryShader = (major > 3) || (major == 3 && minor >= 2);
        r.caps.supportsTessellation = (major > 4) || (major == 4 && minor >= 0);
        r.caps.supportsMultiDrawIndirect = (major > 4) || (major == 4 && minor >= 3); 
        r.caps.supportsDynamicRendering = false; 
        r.caps.supportsMeshShaders = hasExtension("GL_EXT_mesh_shader");
        r.caps.supportsRayTracing = false; 
        r.caps.supportsBindless = hasExtension("GL_ARB_bindless_texture");
        r.caps.supportsBufferDeviceAddress = hasExtension("GL_EXT_buffer_device_address") || hasExtension("GL_NV_buffer_device_address");
    }
    
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(w);
    return r;
}

} // namespace Eng::service