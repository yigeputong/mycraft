#include "client/Render/OpenGLAPI.h"
#include "client/Window.h"
#include "core/Log.h"

#include <SDL3/SDL_pixels.h>
#include <iostream>
#include <filesystem>
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

#define CHECK_GL(tag) do { \
    GLenum e; \
    while ((e = glGetError()) != GL_NO_ERROR) \
        logError(m_logger, "[GL] " << tag << " error=" << e); \
} while(0)

namespace Eng::client {

// ---------- 构造函数 / 析构函数 ----------
OpenGLAPI::OpenGLAPI() = default;

OpenGLAPI::~OpenGLAPI() {
    try { Shutdown(); }
    catch (const std::exception& e) {
        std::fprintf(stderr, "[OpenGLAPI] Shutdown threw: %s\n", e.what());
    }
    catch (...) {
        std::fputs("[OpenGLAPI] Shutdown threw unknown\n", stderr);
    }
}

// ---------- 初始化 ----------
void OpenGLAPI::Initialize(int width, int height, Window* window) {
    if (m_initialized) return;;
    m_window = window;

    // 启用深度测试
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // 启用背面剔除
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    SetViewport(0, 0, width, height);

    m_viewportWidth = width;
    m_viewportHeight = height;
    m_initialized = true;

    // 默认纹理
    m_defaultTexture = CreateTexture("./assets/textures/missing_texture.png");
    m_defaultTexID = ResolveTexture(m_defaultTexture);

    // ★ Global UBO
    glGenBuffers(1, &m_globalUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_globalUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Eng::client::GlobalUBOData),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_globalUBO);

    // ★ Material UBO
    glGenBuffers(1, &m_materialUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_materialUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Eng::client::MaterialUBOData),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_materialUBO);

    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void OpenGLAPI::Shutdown() {
    if (!m_initialized) return;

    if (m_globalUBO)   { glDeleteBuffers(1, &m_globalUBO);   m_globalUBO = 0; }
    if (m_materialUBO) { glDeleteBuffers(1, &m_materialUBO); m_materialUBO = 0; }
    if (m_fullscreenVAO) { glDeleteVertexArrays(1, &m_fullscreenVAO); m_fullscreenVAO = 0; }
    if (m_fullscreenVBO) { glDeleteBuffers(1, &m_fullscreenVBO);      m_fullscreenVBO = 0; }
    if (m_skyCubeMesh)   { DestroyMesh(m_skyCubeMesh);                m_skyCubeMesh = 0; }

    for (auto& [handle, mesh] : m_meshes) {
        if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
        if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.ebo) glDeleteBuffers(1, &mesh.ebo);
    }
    m_meshes.clear();

    for (auto& [handle, tex] : m_textures) {
        if (tex.textureID) glDeleteTextures(1, &tex.textureID);
    }
    m_textures.clear();

    for (auto& [handle, shader] : m_shaders) {
        if (shader.program) glDeleteProgram(shader.program);
    }
    m_shaders.clear();

    m_initialized = false;
    logInfo(m_logger, "[RenderAPI] Shutdown");
}

// ---------- 视口与清屏 ----------
void OpenGLAPI::SetViewport(int x, int y, int w, int h) {
    m_viewportX = x;
    m_viewportY = y;
    m_viewportWidth = w;
    m_viewportHeight = h;
    glViewport(x, y, w, h);
}

void OpenGLAPI::SetClearColor(float r, float g, float b, float a) {
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
    glClearColor(r, g, b, a);
}

void OpenGLAPI::Clear() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// ---------- 矩阵设置 ----------
void OpenGLAPI::SetViewMatrix(const glm::mat4& view) {
    m_viewMatrix = view;
}

void OpenGLAPI::SetProjectionMatrix(const glm::mat4& proj) {
    m_projectionMatrix = proj;
}

void OpenGLAPI::SetModelMatrix(const glm::mat4& model) {
    m_modelMatrix = model;
}

void OpenGLAPI::SetLightPosition(const glm::vec3& pos) {
    m_lightDir = pos;
}

void OpenGLAPI::SetLightColor(const glm::vec3& color, const float intensity) {
    m_lightColor = color;
    m_lightIntensity = intensity;
}

void OpenGLAPI::SetLightAmbient(const glm::vec3& amb) {
    m_lightAmbient = amb;
}

void OpenGLAPI::SetViewPosition(const glm::vec3& pos) {
    m_viewPos = pos;
}

// ---------- 资源创建 ----------
MeshHandle OpenGLAPI::CreateMesh(const MeshData& data) {
    return CreateMeshInternal(data, false);
}

MeshHandle OpenGLAPI::CreateMeshInstance(const MeshData& data) {
    return CreateMeshInternal(data, true);
}

MeshHandle OpenGLAPI::CreateMeshInternal(const MeshData& data, bool instanced) {
    if (!m_initialized) return 0;

    MeshDataInternal internal;
    glGenVertexArrays(1, &internal.vao);
    glBindVertexArray(internal.vao);

    // ============ 顶点 VBO ============
    glGenBuffers(1, &internal.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, internal.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 data.vertices.size() * sizeof(Vertex),
                 data.vertices.data(),
                 GL_STATIC_DRAW);

    // 顶点属性
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, ao));
    glEnableVertexAttribArray(3);

    // ============ 实例 VBO（仅实例化时）============
    internal.instanceVBO = 0;
    if (instanced) {
        glGenBuffers(1, &internal.instanceVBO);
        glBindBuffer(GL_ARRAY_BUFFER, internal.instanceVBO);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

        for (int i = 0; i < 4; ++i) {
            glEnableVertexAttribArray(3 + i);
            glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                                  (void*)(sizeof(glm::vec4) * i));
            glVertexAttribDivisor(3 + i, 1);
        }
    }

    // ============ EBO ============
    if (!data.indices.empty()) {
        glGenBuffers(1, &internal.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, internal.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     data.indices.size() * sizeof(uint32_t),
                     data.indices.data(),
                     GL_STATIC_DRAW);
        internal.indexCount = data.indices.size();
        internal.indexType = GL_UNSIGNED_INT;
    } else {
        internal.ebo = 0;
        internal.indexCount = data.vertices.size();
        internal.indexType = GL_NONE;
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    MeshHandle handle = m_nextMeshHandle++;
    m_meshes[handle] = internal;

    return handle;
}

TextureHandle OpenGLAPI::CreateTexture(const std::string& path) {
    if (!m_initialized) return 0;

    auto it = m_textureCache.find(path);
    if (it != m_textureCache.end()) {
        logDebug(m_logger, "[Texture] Cache hit: " << path);
        return it->second;
    }

    SDL_Surface* surf = IMG_Load(path.c_str());
    if (!surf) {
        logError(m_logger, "[Texture] IMG_Load failed: " << path << " - " << SDL_GetError());
        return 0;
    }

    // 2. 检查尺寸是否有效
    if (surf->w <= 0 || surf->h <= 0) {
        logError(m_logger, "[Texture] Invalid surface size: " << surf->w << "x" << surf->h);
        SDL_DestroySurface(surf);
        return 0;
    }

    // 3. 强制转换为 RGBA8888（保证兼容性）
    SDL_Surface* converted = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    if (!converted) {
        logError(m_logger, "[Texture] SDL_ConvertSurface failed: " << SDL_GetError());
        SDL_DestroySurface(surf);
        return 0;
    }
    SDL_DestroySurface(surf);
    surf = converted;

    // 4. 获取像素格式细节
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(surf->format);
    if (!details) {
        logError(m_logger, "[Texture] Unknown pixel format: " << surf->format);
        SDL_DestroySurface(surf);
        return 0;
    }

    int bpp = details->bytes_per_pixel;

    // 5. 确定 OpenGL 格式（统一 UNORM，和 atlas / 管线一致）
    GLenum internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    if (bpp == 4) {
        internalFormat = GL_RGBA8;
        format = GL_RGBA;
    } else if (bpp == 3) {
        internalFormat = GL_RGB8;
        format = GL_RGB;
    } else {
        logError(m_logger, "[Texture] Unsupported BPP: " << bpp);
        SDL_DestroySurface(surf);
        return 0;
    }

    // 6. 生成 OpenGL 纹理
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, surf->w, surf->h, 0,
                 format, GL_UNSIGNED_BYTE, surf->pixels);

    glGenerateMipmap(GL_TEXTURE_2D);

    int w = surf->w, h = surf->h;
    SDL_DestroySurface(surf);

    // 7. 保存并返回句柄
    TextureHandle handle = m_nextTextureHandle++;
    TextureDataInternal data;
    data.textureID = textureID;
    data.width = w;
    data.height = h;
    data.format = format;
    m_textures[handle] = data;

    m_textureCache[path] = handle;
    logInfo(m_logger, "[Texture] Loaded: " << path << " (" << w << "x" << h << ")");
    return handle;
}

TextureHandle OpenGLAPI::CreateTextureFromMemory(const aiTexture* embedded) {
    if (!embedded || !embedded->pcData) {
        logError(m_logger, "[Texture] Embedded texture is null");
        return 0;
    }

    SDL_Surface* surface = nullptr;

    if (embedded->mHeight == 0) {
        // ===== 情况 1：压缩数据（PNG/JPG） =====
        SDL_IOStream* io = SDL_IOFromMem(
            const_cast<void*>(static_cast<const void*>(embedded->pcData)),
            embedded->mWidth
        );
        if (!io) {
            logError(m_logger, "[Texture] SDL_IOFromMem failed: " << SDL_GetError());
            return 0;
        }

        surface = IMG_Load_IO(io, true);  // true = 自动关闭 io
        if (!surface) {
            logError(m_logger, "[Texture] IMG_Load_IO failed: " << SDL_GetError());
            return 0;
        }
    } else {
        // ===== 情况 2：未压缩 BGRA 像素 =====
        surface = SDL_CreateSurface(
            static_cast<int>(embedded->mWidth),
            static_cast<int>(embedded->mHeight),
            SDL_PIXELFORMAT_BGRA32
        );
        if (!surface) {
            logError(m_logger, "[Texture] SDL_CreateSurface failed: " << SDL_GetError());
            return 0;
        }
        std::memcpy(surface->pixels, embedded->pcData,
            static_cast<std::size_t>(embedded->mWidth) * embedded->mHeight * 4);
    }

    // ===== 转成 ABGR8888（匹配 OpenGL 的 GL_RGBA） =====
    SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!converted) {
        logError(m_logger, "[Texture] Conversion failed: " << SDL_GetError());
        return 0;
    }

    // ===== 上传到 OpenGL =====
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
             converted->w, converted->h, 0,
             GL_RGBA, GL_UNSIGNED_BYTE, converted->pixels);

    glGenerateMipmap(GL_TEXTURE_2D);

    // 各向异性过滤
    GLfloat maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
    if (maxAniso > 1.0f) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY,
                        std::min(16.0f, maxAniso));
    }

    int w = converted->w;
    int h = converted->h;
    SDL_DestroySurface(converted);

    // ===== 分配句柄 =====
    TextureHandle handle = m_nextTextureHandle++;
    TextureDataInternal data;
    data.textureID = textureID;
    data.width = w;
    data.height = h;
    data.format = GL_RGBA;
    m_textures[handle] = data;

    logInfo(m_logger, "[Texture] Embedded texture created: " << textureID
              << " (" << w << "x" << h << ")");

    return handle;
}

TextureHandle OpenGLAPI::CreateTextureFromPixels(const uint8_t* rgba, int w, int h) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    TextureHandle handle = m_nextTextureHandle++;
    TextureDataInternal entry;
    entry.textureID = tex;
    entry.width = w;
    entry.height = h;
    entry.format = GL_RGBA;
    m_textures[handle] = entry;

    logInfo(m_logger, "[Texture] from pixels: " << w << "x" << h);
    return handle;
}

namespace {
    // 读二进制文件
    std::vector<char> ReadBinaryFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return {};
        auto size = f.tellg();
        f.seekg(0);
        std::vector<char> buf(static_cast<size_t>(size));
        f.read(buf.data(), size);
        return buf;
    }

    // 从 SPIR-V 编译 shader
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
            // 通过参数把 log 传出去不太好，直接吞掉，外面只看 0
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }
}

ShaderHandle OpenGLAPI::CreateShader(const std::string& vertPath, const std::string& fragPath) {
    if (!m_initialized) return 0;

    auto isSPV = [](const std::string& p) {
        return p.size() > 4 && p.compare(p.size() - 4, 4, ".spv") == 0;
    };

    GLuint vertex = 0, fragment = 0;

    // ---- 顶点着色器 ----
    if (isSPV(vertPath)) {
        auto spv = ReadBinaryFile(vertPath);
        if (spv.empty()) {
            logError(m_logger, "[OpenGLAPI] Failed to read SPIR-V: " << vertPath);
            return 0;
        }
        vertex = CompileShaderSPIRV(GL_VERTEX_SHADER, spv);
    } else {
        std::string src = ReadFile(vertPath);
        if (src.empty()) {
            logError(m_logger, "[OpenGLAPI] Failed to read: " << vertPath);
            return 0;
        }
        vertex = CompileShader(GL_VERTEX_SHADER, src);
    }
    if (!vertex) return 0;

    // ---- 片元着色器 ----
    if (isSPV(fragPath)) {
        auto spv = ReadBinaryFile(fragPath);
        if (spv.empty()) {
            logError(m_logger, "[OpenGLAPI] Failed to read SPIR-V: " << fragPath);
            glDeleteShader(vertex);
            return 0;
        }
        fragment = CompileShaderSPIRV(GL_FRAGMENT_SHADER, spv);
    } else {
        std::string src = ReadFile(fragPath);
        if (src.empty()) {
            logError(m_logger, "[OpenGLAPI] Failed to read: " << fragPath);
            glDeleteShader(vertex);
            return 0;
        }
        fragment = CompileShader(GL_FRAGMENT_SHADER, src);
    }
    if (!fragment) {
        glDeleteShader(vertex);
        return 0;
    }

    // ---- 链接（下面保持你原有代码）----
    GLuint program = LinkProgram(vertex, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!program) return 0;

    logInfo(m_logger, "[RenderAPI] Shader created: " << vertPath);

    ShaderHandle handle = m_nextShaderHandle++;
    ShaderDataInternal internal;
    internal.program = program;
    m_shaders[handle] = internal;
    return handle;
}

ShaderHandle OpenGLAPI::CreateSkybox(const std::string& vertPath, const std::string& fragPath) {
    return CreateShader(vertPath, fragPath);
}

Model OpenGLAPI::LoadModel(const std::string& path, bool flipUV) {
    Model result;
    Assimp::Importer importer;

    unsigned int flags = aiProcess_Triangulate |
                         aiProcess_GenSmoothNormals |
                         aiProcess_JoinIdenticalVertices |
                         aiProcess_OptimizeMeshes |
                         aiProcess_PreTransformVertices;
    if (flipUV) flags |= aiProcess_FlipUVs;

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        logError(m_logger, "[LoadModel] Assimp error: " << importer.GetErrorString());
        return result;
    }

    logInfo(m_logger, "[LoadModel] Loaded: " << path);
    logDebug(m_logger, "[LoadModel] Meshes: " << scene->mNumMeshes);

    std::filesystem::path modelDir = std::filesystem::path(path).parent_path();

    // ★ Bug 1 修复：texCache 移到循环外
    std::unordered_map<std::string, TextureHandle> texCache;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        aiMesh* mesh = scene->mMeshes[m];

        // ---- 提取顶点 ----
        std::vector<Vertex> vertices;
        vertices.reserve(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            Vertex v;
            v.position = glm::vec3(mesh->mVertices[i].x,
                                   mesh->mVertices[i].y,
                                   mesh->mVertices[i].z);

            v.normal = mesh->mNormals
                ? glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z)
                : glm::vec3(0.0f, 1.0f, 0.0f);

            v.uv = mesh->mTextureCoords[0]
                ? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
                : glm::vec2(0.0f, 0.0f);

            vertices.push_back(v);
        }

        // ---- 提取索引 ----
        std::vector<uint32_t> indices;
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            aiFace face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                indices.push_back(face.mIndices[j]);
            }
        }

        if (vertices.empty() || indices.empty()) continue;

        // ---- 上传到 GPU ----
        MeshData meshData;
        meshData.vertices = vertices;
        meshData.indices = indices;
        MeshHandle meshHandle = CreateMesh(meshData);

        if (meshHandle == 0) {
            logError(m_logger, "[LoadModel] Failed to create mesh " << m);
            continue;
        }

        // ---- 加载该网格的材质纹理 ----
        TextureHandle diffuseTex = 0;   // ★ Bug 2 修复：只声明一次

        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            aiString texPath;

            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                const aiTexture* embedded = scene->GetEmbeddedTexture(texPath.C_Str());
                std::string texKey = texPath.C_Str();

                auto cacheIt = texCache.find(texKey);
                if (cacheIt != texCache.end()) {
                    diffuseTex = cacheIt->second;
                    logDebug(m_logger, "[LoadModel] Texture cache hit: " << texKey);
                } else {
                    if (embedded) {
                        diffuseTex = CreateTextureFromMemory(embedded);
                    } else {
                        std::string texStr = texPath.C_Str();
                        if (texStr.starts_with("//")) texStr = texStr.substr(2);

                        std::filesystem::path fullTexPath = modelDir / texStr;
                        if (std::filesystem::exists(fullTexPath)) {
                            diffuseTex = CreateTexture(fullTexPath.string());
                        } else {
                            logWarning(m_logger, "[LoadModel] Texture not found: "
                                       << fullTexPath.string());
                        }
                    }
                    texCache[texKey] = diffuseTex;
                }
            }
        }

        // ---- 加入 subMeshes ----
        SubMesh sub;
        sub.mesh = meshHandle;
        sub.diffuseTexture = diffuseTex;   // ★ 现在能拿到正确值了
        result.subMeshes.push_back(sub);

        logDebug(m_logger, "[LoadModel] SubMesh " << m << ": "
                 << vertices.size() << " verts, tex=" << diffuseTex);
    }

    if (result.subMeshes.empty()) {
        logError(m_logger, "[LoadModel] No valid submeshes!");
        return result;
    }

    logInfo(m_logger, "[LoadModel] Done! SubMeshes: " << result.subMeshes.size());
    return result;
}

void OpenGLAPI::DestroyMesh(MeshHandle handle) {
    auto it = m_meshes.find(handle);
    if (it == m_meshes.end()) return;
    if (it->second.vao) glDeleteVertexArrays(1, &it->second.vao);
    if (it->second.vbo) glDeleteBuffers(1, &it->second.vbo);
    if (it->second.ebo) glDeleteBuffers(1, &it->second.ebo);
    m_meshes.erase(it);
}

void OpenGLAPI::DestroyTexture(TextureHandle handle) {
    auto it = m_textures.find(handle);
    if (it == m_textures.end()) return;
    if (it->second.textureID) glDeleteTextures(1, &it->second.textureID);
    m_textures.erase(it);
}

void OpenGLAPI::DestroyShader(ShaderHandle handle) {
    auto it = m_shaders.find(handle);
    if (it == m_shaders.end()) return;
    if (it->second.program) glDeleteProgram(it->second.program);
    m_shaders.erase(it);
}

Framebuffer OpenGLAPI::CreateFramebuffer(int width, int height) {
    // ---------- 全屏 quad 初始化（保持不变）----------
    if (!m_fullscreenShader)
    m_fullscreenShader = CreateShader(
        "./assets/shaders/build/post/vert.gl.spv",
        "./assets/shaders/build/post/frag.gl.spv");

    if (m_fullscreenVAO == 0) {
        static constexpr float vertices[] = {
            -1.0f,  1.0f,   0.0f, 1.0f,
            -1.0f, -1.0f,   0.0f, 0.0f,
            1.0f, -1.0f,   1.0f, 0.0f,
            -1.0f,  1.0f,   0.0f, 1.0f,
            1.0f, -1.0f,   1.0f, 0.0f,
            1.0f,  1.0f,   1.0f, 1.0f
        };

        glGenVertexArrays(1, &m_fullscreenVAO);
        glGenBuffers(1, &m_fullscreenVBO);
        glBindVertexArray(m_fullscreenVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_fullscreenVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    // ---------- FBO + attachments ----------
    GLuint fbo      = 0;
    GLuint colorTex = 0;
    GLuint depthBuf = 0;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Color texture
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, colorTex, 0);

    // Depth
    glGenRenderbuffers(1, &depthBuf);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuf);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_RENDERBUFFER, depthBuf);

    // 检查
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        logError(m_logger, "[OpenGLAPI] Framebuffer creation failed!");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteRenderbuffers(1, &depthBuf);
        return {};
    }
    logDebug(m_logger, "[RenderAPI] Framebuffer created: " << width << "x" << height);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // ---------- 存到内部表 ----------
    FramebufferHandle h = m_nextFramebufferHandle++;

    FramebufferInternal fbi;
    fbi.fbo      = fbo;
    fbi.colorTex = colorTex;
    fbi.depthBuf = depthBuf;
    fbi.width    = width;
    fbi.height   = height;
    m_framebuffers[h] = fbi;

    // 把 colorTex 也注册到 m_textures，这样 DrawFullscreenQuad 能直接用
    TextureHandle texHandle = m_nextTextureHandle++;
    m_textures[texHandle] = { colorTex, width, height, GL_RGBA };
    m_fbColorHandles[h] = texHandle;

    // ---------- 返回 handle ----------
    Framebuffer fb;
    fb.handle = h;
    fb.width  = width;
    fb.height = height;
    return fb;
}

void OpenGLAPI::BindFramebuffer(const Framebuffer& fb) {
    auto it = m_framebuffers.find(fb.handle);
    if (it == m_framebuffers.end()) return;
    glBindFramebuffer(GL_FRAMEBUFFER, it->second.fbo);
    glViewport(0, 0, it->second.width, it->second.height);
}

void OpenGLAPI::UnbindFramebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (m_window) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
        glViewport(0, 0, w, h);
    }
}

TextureHandle OpenGLAPI::GetFramebufferTexture(const Framebuffer& fb) const {
    auto it = m_fbColorHandles.find(fb.handle);
    return it != m_fbColorHandles.end() ? it->second : 0;
}

void OpenGLAPI::DestroyFramebuffer(const Framebuffer& fb) {
    auto it = m_framebuffers.find(fb.handle);
    if (it == m_framebuffers.end()) return;

    glDeleteFramebuffers(1, &it->second.fbo);
    glDeleteTextures(1, &it->second.colorTex);
    if (it->second.depthBuf)
        glDeleteRenderbuffers(1, &it->second.depthBuf);

    // 从 m_textures 删掉注册的 colorTex
    auto thIt = m_fbColorHandles.find(fb.handle);
    if (thIt != m_fbColorHandles.end()) {
        m_textures.erase(thIt->second);
        m_fbColorHandles.erase(thIt);
    }

    m_framebuffers.erase(it);
}

// ---------- Uniform 设置 ----------
void OpenGLAPI::SetUniform(ShaderHandle shader, const std::string& name, const glm::mat4& value) {
    auto it = m_shaders.find(shader);
    if (it == m_shaders.end() || !it->second.program) return;
    glUseProgram(it->second.program);
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc == -1) return;
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
    CHECK_GL("SetUniform(mat4)");
}

void OpenGLAPI::SetUniform(ShaderHandle shader, const std::string& name, const glm::vec3& value) {
    auto it = m_shaders.find(shader);
    if (it == m_shaders.end() || !it->second.program) return;
    glUseProgram(it->second.program);
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc == -1) return;
    glUniform3fv(loc, 1, glm::value_ptr(value));
    CHECK_GL("SetUniform(vec3)");
}

void OpenGLAPI::SetUniform(ShaderHandle shader, const std::string& name, float value) {
    if (name == "uAOStrength") {
        m_aoStrength = value;
    } else if (name == "uTimeOfDay") {
        m_timeOfDay  = value;
    } else {
        auto it = m_shaders.find(shader);
        if (it == m_shaders.end() || !it->second.program) return;
        glUseProgram(it->second.program);
        GLint loc = glGetUniformLocation(it->second.program, name.c_str());
        if (loc == -1) return;
        glUniform1f(loc, value);
        CHECK_GL("SetUniform(float)");
    }
}

void OpenGLAPI::SetUniform(ShaderHandle shader, const std::string& name, int value) {
    auto it = m_shaders.find(shader);
    if (it == m_shaders.end() || !it->second.program) return;
    glUseProgram(it->second.program);
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc == -1) return;
    glUniform1i(loc, value);
    CHECK_GL("SetUniform(int)");
}

// ---------- 绘制 ----------
void OpenGLAPI::BeginFrame() {
    UpdateGlobalUBO();
}

void OpenGLAPI::DrawMesh(MeshHandle mesh, ShaderHandle shader, const Material& material) {
    if (!m_initialized) return;

    auto meshIt = m_meshes.find(mesh);
    auto shaderIt = m_shaders.find(shader);
    if (meshIt == m_meshes.end() || shaderIt == m_shaders.end()) return;

    const auto& meshData = meshIt->second;
    GLuint program = shaderIt->second.program;
    if (program == 0) return;

    glUseProgram(program);

    UpdateMaterialUBO(material.shininess);

    SetUniform(shader, "uModel", m_modelMatrix);

    // ============ 3. 绑定纹理 ============
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ResolveTexture(material.diffuse));

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ResolveTexture(material.specular));

    // ============ 4. 绘制 ============

    glBindVertexArray(meshData.vao);
    if (meshData.ebo != 0) {
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(meshData.indexCount),
                       meshData.indexType,
                       nullptr);
    } else {
        glDrawArrays(GL_TRIANGLES, 0,
                     static_cast<GLsizei>(meshData.indexCount));
    }
    glBindVertexArray(0);

    // ============ 5. 解绑纹理 ============
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLAPI::DrawMeshInstanced(MeshHandle mesh, ShaderHandle shader, const Material& material, const std::vector<glm::mat4>& transforms) {
    if (transforms.empty()) return;

    auto meshIt = m_meshes.find(mesh);
    auto shaderIt = m_shaders.find(shader);
    if (meshIt == m_meshes.end() || shaderIt == m_shaders.end()) return;

    auto& meshData = meshIt->second;
    GLuint program = shaderIt->second.program;
    if (program == 0) return;

    glUseProgram(program);

    UpdateMaterialUBO(material.shininess); 

    // ============ 2. 上传实例矩阵 ============
    glBindBuffer(GL_ARRAY_BUFFER, meshData.instanceVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 transforms.size() * sizeof(glm::mat4),
                 transforms.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);   // 解绑，避免影响其他状态

    // ============ 3. 绑定纹理 ============
    // 漫反射
    glActiveTexture(GL_TEXTURE0);
    if (material.diffuse != 0) {
        auto texIt = m_textures.find(material.diffuse);
        if (texIt != m_textures.end() && texIt->second.textureID != 0) {
            glBindTexture(GL_TEXTURE_2D, texIt->second.textureID);
        } else {
            glBindTexture(GL_TEXTURE_2D, m_defaultTexture);
        }
    } else {
        glBindTexture(GL_TEXTURE_2D, m_defaultTexture);
    }

    // 镜面
    glActiveTexture(GL_TEXTURE1);
    if (material.specular != 0) {
        auto texIt = m_textures.find(material.specular);
        if (texIt != m_textures.end() && texIt->second.textureID != 0) {
            glBindTexture(GL_TEXTURE_2D, texIt->second.textureID);
        } else {
            glBindTexture(GL_TEXTURE_2D, m_defaultTexture);
        }
    } else {
        glBindTexture(GL_TEXTURE_2D, m_defaultTexture);
    }

    // ============ 4. 一次性绘制所有实例 ============
    glBindVertexArray(meshData.vao);
    if (meshData.ebo != 0) {
        glDrawElementsInstanced(GL_TRIANGLES,
                                static_cast<GLsizei>(meshData.indexCount),
                                meshData.indexType,
                                nullptr,
                                static_cast<GLsizei>(transforms.size()));
    } else {
        glDrawArraysInstanced(GL_TRIANGLES,
                              0,
                              static_cast<GLsizei>(meshData.indexCount),
                              static_cast<GLsizei>(transforms.size()));
    }
    glBindVertexArray(0);

    // ============ 5. 解绑纹理 ============
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLAPI::DrawSkybox(ShaderHandle shader) {
    auto it = m_shaders.find(shader);
    if (it == m_shaders.end()) return;
    GLuint program = it->second.program;
    if (program == 0) return;

    // 天空盒状态：不写深度、不剔面
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    GLint oldDepthFunc;
    glGetIntegerv(GL_DEPTH_FUNC, &oldDepthFunc);
    glDepthFunc(GL_LEQUAL);   // shader 里 gl_Position.xyww 会得到 z=1，LEQUAL 才能通过

    glUseProgram(program);

    // 天空盒不需要 model / material / 纹理
    auto meshit = m_meshes.find(GetSkyCubeMesh());
    if (meshit == m_meshes.end()) return;
    glBindVertexArray(meshit->second.vao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    // 恢复状态
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glDepthFunc(oldDepthFunc);
    glUseProgram(0);
}

void OpenGLAPI::DrawFullscreenQuad(TextureHandle textureID) {
    glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);

    auto shader = m_shaders.find(m_fullscreenShader)->second.program;

    glUseProgram(shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ResolveTexture(textureID));
    glUniform1i(glGetUniformLocation(shader, "screenTexture"), 0);

    glBindVertexArray(m_fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void OpenGLAPI::EndFrame() {
    ImGui::Render();
    ImGuiRenderDrawData();
    SDL_GL_SwapWindow(m_window->GetSDLWindow());
}

// ---------- 辅助函数 ----------
std::string OpenGLAPI::ReadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        logError(m_logger, "[OpenGLAPI] Could not open file: " << path);
        return "";
    }
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
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        logError(m_logger, "[OpenGLAPI] Shader compilation failed:\n" << infoLog);
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
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        logError(m_logger, "[OpenGLAPI] Program linking failed:\n" << infoLog);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void OpenGLAPI::UpdateGlobalUBO() {
    while (glGetError() != GL_NO_ERROR) {}

    if (m_globalUBO == 0) {
        logError(m_logger, "[OpenGLAPI] m_globalUBO == 0!");
        return;
    }

    Eng::client::GlobalUBOData data;
    data.view           = m_viewMatrix;
    data.projection     = m_projectionMatrix;
    data.viewPos        = m_viewPos;
    data.aoStrength     = m_aoStrength;       // 原 _pad0
    data.lightDir       = glm::normalize(m_lightDir);
    data.lightIntensity = m_lightIntensity;
    data.lightColor     = m_lightColor;
    data.timeOfDay      = m_timeOfDay;        // 原 _pad1
    data.lightAmbient   = m_lightAmbient;
    data._pad2          = 0.0f;

    glBindBuffer(GL_UNIFORM_BUFFER, m_globalUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        logError(m_logger, "[OpenGLAPI: UpdateGlobalUBO()] glBufferSubData error: " << err);
    }

    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void OpenGLAPI::UpdateMaterialUBO(float shininess) {
    Eng::client::MaterialUBOData data;
    data.shininess = shininess;
    data._pad[0] = data._pad[1] = data._pad[2] = 0.0f;

    glBindBuffer(GL_UNIFORM_BUFFER, m_materialUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

MeshHandle OpenGLAPI::GetSkyCubeMesh() {
    if (m_skyCubeMesh != 0) return m_skyCubeMesh;

    // 36 顶点（每面 6 个），NDC 空间的单位立方体
    const float s = 1.0f;
    float verts[] = {
        // +X
         s,-s,-s,  s,-s, s,  s, s, s,   s, s, s,  s, s,-s,  s,-s,-s,
        // -X
        -s,-s, s, -s,-s,-s, -s, s,-s,  -s, s,-s, -s, s, s, -s,-s, s,
        // +Y
        -s, s,-s,  s, s,-s,  s, s, s,   s, s, s, -s, s, s, -s, s,-s,
        // -Y
        -s,-s, s,  s,-s, s,  s,-s,-s,   s,-s,-s, -s,-s,-s, -s,-s, s,
        // +Z
        -s,-s, s,  s,-s, s,  s, s, s,   s, s, s, -s, s, s, -s,-s, s,
        // -Z
         s,-s,-s, -s,-s,-s, -s, s,-s,  -s, s,-s,  s, s,-s,  s,-s,-s,
    };

    std::vector<Eng::client::Vertex> v(36);
    for (size_t i = 0; i < 36; ++i) {
        v[i].position = {verts[i*3], verts[i*3+1], verts[i*3+2]};
        v[i].normal   = {0.0f, 0.0f, 0.0f};
        v[i].uv       = {0.0f, 0.0f};
    }

    Eng::client::MeshData md;
    md.vertices = std::move(v);
    // indices 留空 → glDrawArrays
    m_skyCubeMesh = CreateMesh(md);
    return m_skyCubeMesh;
}

// ---------- ImGui ----------
bool OpenGLAPI::InitImGuiBackend() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!ImGui_ImplSDL3_InitForOpenGL(m_window->GetSDLWindow(), m_window->GetGLContext()))
        return false;
    if (!ImGui_ImplOpenGL3_Init("#version 460 core"))
        return false;
    logInfo(m_logger, "[RenderAPI] ImGui backend initialized");
    return true;
}

void OpenGLAPI::ShutdownImGuiBackend() {
    if (ImGui::GetCurrentContext() == nullptr) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    logInfo(m_logger, "[RenderAPI] ImGui backend shutdown");
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
    info.vendor     = getStr(GL_VENDOR);

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    info.apiVersion = std::to_string(major) + "." + std::to_string(minor);

    std::string glVersion = getStr(GL_VERSION);
    info.extra        = glVersion;
    info.driverVersion = glVersion;                   // 不做厂商特化解析

    std::string glsl = getStr(GL_SHADING_LANGUAGE_VERSION);
    info.shadingLanguage = "GLSL " + glsl;

    return info;
}

void OpenGLAPI::ApplySettings(const WindowConfig& win, const RenderConfig& render) {
    // 全屏由 MyGame 直接调 SetFullscreen
    SDL_GL_SetSwapInterval(win.vsync ? 1 : 0);
    m_aoStrength = render.m_aoStrength;
}

void OpenGLAPI::WaitIdle() {
    glFinish();
}
    
} // namespace Eng
