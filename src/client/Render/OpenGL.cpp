#include "client/Render/OpenGLAPI.h"
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

#include "client/Window.h"

namespace Eng::client {

// ---------- 构造函数 / 析构函数 ----------
OpenGLAPI::OpenGLAPI() = default;

OpenGLAPI::~OpenGLAPI() {
    Shutdown();
}

// ---------- 初始化 ----------
bool OpenGLAPI::Initialize(int width, int height, Window* window) {
    if (m_initialized) return true;
    m_window = window;

    // 启用深度测试
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // 启用背面剔除
    // glEnable(GL_CULL_FACE);
    // glCullFace(GL_BACK);

    SetViewport(0, 0, width, height);

    m_viewportWidth = width;
    m_viewportHeight = height;
    m_initialized = true;

    std::string version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    m_logger->log(LogLevel::INFO, "[OpenGLAPI] Initialized (OpenGL " + version + ")");

    return true;
}

void OpenGLAPI::Shutdown() {
    if (!m_initialized) return;

    // 释放所有资源
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
    m_logger->log(LogLevel::INFO, "[OpenGLAPI] Shutdown");
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

// ---------- 资源创建 ----------
MeshHandle OpenGLAPI::CreateMesh(const MeshData& data) {
    if (!m_initialized) return 0;

    MeshDataInternal internal;
    glGenVertexArrays(1, &internal.vao);
    glBindVertexArray(internal.vao);

    // 创建 VBO
    glGenBuffers(1, &internal.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, internal.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(Vertex), data.vertices.data(), GL_STATIC_DRAW);

    // 顶点属性
    // position (location = 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    // normal (location = 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);
    // uv (location = 2)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);

    // EBO（索引缓冲）
    if (!data.indices.empty()) {
        glGenBuffers(1, &internal.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, internal.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(uint32_t), data.indices.data(), GL_STATIC_DRAW);
        internal.indexCount = data.indices.size();
        internal.indexType = GL_UNSIGNED_INT;
    } else {
        internal.ebo = 0;
        internal.indexCount = data.vertices.size();
        internal.indexType = GL_NONE;
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    MeshHandle handle = m_nextMeshHandle++;
    m_meshes[handle] = std::move(internal);
    return handle;
}

TextureHandle OpenGLAPI::CreateTexture(const std::string& path) {
    if (!m_initialized) return 0;

    SDL_Surface* surf = IMG_Load(path.c_str());
    if (!surf) {
        logError(m_logger, "[Texture] IMG_Load failed: " << path << " - " << SDL_GetError());
        return 0;
    }

    logInfo(m_logger, "[Texture] Loaded: " << path << ", w=" << std::to_string(surf->w) 
        << ", h=" << std::to_string(surf->h) << ", format=" << std::to_string(surf->format));

    // 2. 检查尺寸是否有效
    if (surf->w <= 0 || surf->h <= 0) {
        logError(m_logger, "[Texture] Invalid surface size: " << surf->w << "x" << surf->h);
        SDL_DestroySurface(surf);
        return 0;
    }

    // 3. 强制转换为 RGBA8888（保证兼容性）
    SDL_Surface* converted = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ABGR8888);
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
    logInfo(m_logger, "[Texture] Bytes per pixel: " << bpp);

    // 5. 确定 OpenGL 格式
    GLenum internalFormat = GL_RGBA;
    GLenum format = GL_RGBA;
    if (bpp == 4) {
        internalFormat = GL_RGBA;
        format = GL_RGBA;
    } else if (bpp == 3) {
        internalFormat = GL_RGB;
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, surf->w, surf->h, 0,
                 format, GL_UNSIGNED_BYTE, surf->pixels);

    glGenerateMipmap(GL_TEXTURE_2D);

    SDL_DestroySurface(surf);

    // 7. 保存并返回句柄
    TextureHandle handle = m_nextTextureHandle++;
    TextureDataInternal data;
    data.textureID = textureID;
    data.width = surf->w;
    data.height = surf->h;
    data.format = format;
    m_textures[handle] = data;

    logInfo(m_logger, "[Texture] OpenGL texture created: " << textureID);
    return handle;
}

TextureHandle OpenGLAPI::CreateSkybox(const std::vector<std::string>& path) {
    if (!m_initialized) return 0;

    if (!m_skyboxVAO) {
         float vertices[] = {
            -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,
            1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
            1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,
            1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,
            1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
            1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f
        };

        glGenVertexArrays(1, &m_skyboxVAO);
        glGenBuffers(1, &m_skyboxVBO);
        glBindVertexArray(m_skyboxVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    if (!m_skyboxShader) {
        m_skyboxShader = CreateShader("./assets/shaders/Engine/OpenGL/skybox/skybox.vert", "./assets/shaders/Engine/OpenGL/skybox/skybox.frag");
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int w = 0;
    int h = 0;

    for (size_t i = 0; i < path.size(); ++i) {
        SDL_Surface* surf = IMG_Load(path[i].c_str());
        if (!surf) {
            logError(m_logger, "Failed to load cube face: " << path[i]);
            return 0;
        }
        SDL_Surface* converted = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ABGR8888);
        SDL_DestroySurface(surf);
        if (!converted) {
            logError(m_logger, "Conversion failed for: " << path[i]);
            return 0;
        }
        w = converted->w;
        h = converted->h;
        glTexImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i),
                     0, GL_RGBA, converted->w, converted->h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, converted->pixels);
        SDL_DestroySurface(converted);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    TextureHandle handle = m_nextTextureHandle++;
    TextureDataInternal data;
    data.textureID = textureID;
    data.width = w;
    data.height = h;
    data.format = GL_RGBA;
    m_textures[handle] = data;

    logInfo(m_logger, "[Texture] OpenGL CubeMap texture created: " << textureID);
    return handle;
}

ShaderHandle OpenGLAPI::CreateShader(const std::string& vertPath, const std::string& fragPath) {
    if (!m_initialized) return 0;

    // 读取源码
    std::string vertSource = ReadFile(vertPath);
    std::string fragSource = ReadFile(fragPath);
    if (vertSource.empty() || fragSource.empty()) {
        logError(m_logger, "[OpenGLAPI] Failed to read shader files.");
        return 0;
    }

    // 编译
    GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertSource);
    GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragSource);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return 0;
    }

    // 链接
    GLuint program = LinkProgram(vertex, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    if (!program) return 0;

    // 存储
    ShaderHandle handle = m_nextShaderHandle++;
    ShaderDataInternal internal;
    internal.program = program;
    m_shaders[handle] = internal;

    return handle;
}

Model OpenGLAPI::LoadModel(const std::string& path) {
    Model result;
    Assimp::Importer importer;

    // ===== 1. 加载模型 =====
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |             // 所有面转三角形
        aiProcess_GenSmoothNormals |        // 生成法线
        // aiProcess_FlipUVs |              // 翻转纹理坐标
        aiProcess_JoinIdenticalVertices |   // 合并重复顶点
        aiProcess_OptimizeMeshes            // 优化网格
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        logError(m_logger, "[LoadModel] Assimp error: " << importer.GetErrorString());
        return result;
    }

    logInfo(m_logger, "[LoadModel] Loaded: " << path);
    logInfo(m_logger, "[LoadModel] Meshes: " << scene->mNumMeshes);

    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;
    uint32_t indexOffset = 0;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        aiMesh* mesh = scene->mMeshes[m];

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            Vertex v;
            v.position = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
            
            if (mesh->mNormals) {
                v.normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
            } else {
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            if (mesh->mTextureCoords[0]) {
                v.uv = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
            } else {
                v.uv = glm::vec2(0.0f, 0.0f);
            }
            allVertices.push_back(v);

            
        }
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            aiFace face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                allIndices.push_back(indexOffset + face.mIndices[j]);
            }
        }

        indexOffset += mesh->mNumVertices;
    }

    if (allVertices.empty() || allIndices.empty()) {
        logError(m_logger, "[LoadModel] No valid mesh data!");
        return result;
    }

    // ===== 3. 上传到 GPU =====
    MeshData meshData;
    meshData.vertices = allVertices;
    meshData.indices = allIndices;
    result.mesh = CreateMesh(meshData);

    if (result.mesh == 0) {
        logError(m_logger, "[LoadModel] Failed to create mesh!");
        return result;
    }

    logInfo(m_logger, "[LoadModel] Uploaded: " << allVertices.size() << " vertices, "
              << allIndices.size() << " indices");

    // ===== 4. 加载第一个材质的漫反射纹理 =====
    if (scene->mNumMaterials > 0) {
        aiMaterial* material = scene->mMaterials[0];
        aiString texPath;

        if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            // 4.1 处理纹理路径
            std::string texStr = texPath.C_Str();

            // 如果路径是相对路径，拼接模型所在目录
            std::filesystem::path modelPath(path);
            std::string fullTexPath = texStr;

            // 检查文件是否存在，如果不存在则尝试拼接目录
            if (!std::filesystem::exists(fullTexPath)) {
                fullTexPath = modelPath.parent_path().string() + "/" + texStr;
            }

            // 4.2 加载纹理
            result.diffuseTexture = CreateTexture(fullTexPath);

            if (result.diffuseTexture == 0) {
                logError(m_logger, "[LoadModel] Warning: Failed to load texture: " << fullTexPath);
            } else {
                logInfo(m_logger, "[LoadModel] Texture loaded!");
            }
        } else {
            logInfo(m_logger, "[LoadModel] No diffuse texture found.");
        }
    }

    logInfo(m_logger, "[LoadModel] Done!");
    return result;
}

// ---------- 资源销毁 ----------
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
    if (!m_fullscreenShader)
        m_fullscreenShader = CreateShader("./assets/shaders/Engine/OpenGL/fb/fb.vert", "./assets/shaders/Engine/OpenGL/fb/fb.frag");

    static float vertices[] = {
        -1.0f,  1.0f,       0.0f, 1.0f,
        -1.0f, -1.0f,       0.0f, 0.0f,
         1.0f, -1.0f,       1.0f, 0.0f,
        -1.0f,  1.0f,       0.0f, 1.0f,
         1.0f, -1.0f,       1.0f, 0.0f,
         1.0f,  1.0f,       1.0f, 1.0f
    };

    glGenVertexArrays(1, &m_fullscreenVAO);
    glGenBuffers(1, &m_fullscreenVBO);
    glBindVertexArray(m_fullscreenVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_fullscreenVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    Framebuffer fb;
    fb.width = width;
    fb.height = height;

    // 1. 生成帧缓冲对象
    glGenFramebuffers(1, &fb.fboID);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fboID);

    // 2. 创建颜色纹理（用于最终采样）
    glGenTextures(1, &fb.colorTexture);
    glBindTexture(GL_TEXTURE_2D, fb.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.colorTexture, 0);

    glGenRenderbuffers(1, &fb.depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, fb.depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.depthBuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        fb.isValid = true;
        logInfo(m_logger, "[OpenGLAPI] Framebuffer created successfully");
    } else {
        logError(m_logger, "[OpenGLAPI] Framebuffer creation failed!");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    return fb;
}

void OpenGLAPI::BindFramebuffer(const Framebuffer& fb) {
    if (!fb.isValid) return;
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fboID);
    glViewport(0, 0, fb.width, fb.height);
}

void OpenGLAPI::UnbindFramebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (m_window) {
        glViewport(0, 0, m_window->GetConfigs().windowWidth, m_window->GetConfigs().windowHeight);
    }
}

uint32_t OpenGLAPI::GetFramebufferTexture(const Framebuffer& fb) const {
    return fb.colorTexture;
}

// ---------- Uniform 设置 ----------
void OpenGLAPI::SetUniform(ShaderHandle shader, const std::string& name, const glm::mat4& value) {
    auto it = m_shaders.find(shader);
    if (it == m_shaders.end() || !it->second.program) return;
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc == -1) return;
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}

void OpenGLAPI::SetUniform(ShaderHandle shader, const std::string& name, const glm::vec3& value) {
    auto it = m_shaders.find(shader);
    if (it == m_shaders.end() || !it->second.program) return;
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc == -1) return;
    glUniform3fv(loc, 1, glm::value_ptr(value));
}

void OpenGLAPI::SetUniform(ShaderHandle shader, const std::string& name, float value) {
    auto it = m_shaders.find(shader);
    if (it == m_shaders.end() || !it->second.program) return;
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc == -1) return;
    glUniform1f(loc, value);
}

void OpenGLAPI::SetUniform(ShaderHandle shader, const std::string& name, int value) {
    auto it = m_shaders.find(shader);
    if (it == m_shaders.end() || !it->second.program) return;
    GLint loc = glGetUniformLocation(it->second.program, name.c_str());
    if (loc == -1) return;
    glUniform1i(loc, value);
}

// ---------- 绘制 ----------
void OpenGLAPI::DrawMesh(MeshHandle mesh, ShaderHandle shader, const Material& material) {
    if (!m_initialized) return;

    auto meshIt = m_meshes.find(mesh);
    auto shaderIt = m_shaders.find(shader);
    if (meshIt == m_meshes.end() || shaderIt == m_shaders.end()) return;

    const auto& meshData = meshIt->second;
    GLuint program = shaderIt->second.program;

    // 使用着色器
    glUseProgram(program);

    // 设置矩阵 uniform（如果着色器中有这些 uniform）
    SetUniform(shader, "uModel", m_modelMatrix);
    SetUniform(shader, "uView", m_viewMatrix);
    SetUniform(shader, "uProjection", m_projectionMatrix);

    // 设置材质纹理
    // 漫反射纹理
    if (material.diffuse != 0) {
        auto texIt = m_textures.find(material.diffuse);
        if (texIt != m_textures.end() && texIt->second.textureID) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texIt->second.textureID);
            SetUniform(shader, "uDiffuseTexture", 0);
        }
    }
    // 镜面纹理
    if (material.specular != 0) {
        auto texIt = m_textures.find(material.specular);
        if (texIt != m_textures.end() && texIt->second.textureID) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, texIt->second.textureID);
            SetUniform(shader, "uSpecularTexture", 1);
        }
    }
    // 高光强度
    SetUniform(shader, "uShininess", material.shininess);

    // 绑定 VAO 并绘制
    glBindVertexArray(meshData.vao);
    if (meshData.ebo) {
        glDrawElements(GL_TRIANGLES, (GLsizei)meshData.indexCount, meshData.indexType, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)meshData.indexCount);
    }
    glBindVertexArray(0);

    // 解绑纹理（可选）
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLAPI::DrawSkybox(TextureHandle cubemap, const glm::mat4& view) {
    if (!m_initialized) return;

    auto texIt = m_textures.find(cubemap);
    auto shaderIt = m_shaders.find(m_skyboxShader);
    if (texIt == m_textures.end() || shaderIt == m_shaders.end()) return;

    GLuint program = shaderIt->second.program;
    GLuint cubeMapID = texIt->second.textureID;

    GLboolean depthMaskWas = GL_FALSE;
    GLint depthFuncWas = GL_LESS;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFuncWas);

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    glUseProgram(program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapID);
    GLint loc = glGetUniformLocation(program, "uSkybox");
    if (loc != -1) glUniform1i(loc, 0);

    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
    GLint viewLoc = glGetUniformLocation(program, "uView");
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(skyboxView));

    GLint projLoc = glGetUniformLocation(program, "uProjection");
    if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(m_projectionMatrix));

    if (m_skyboxVAO) {
        glBindVertexArray(m_skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    glDepthMask(depthMaskWas);
    glDepthFunc(depthFuncWas);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void OpenGLAPI::DrawFullscreenQuad(TextureHandle textureID) {
    glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);

    auto shader = m_shaders.find(m_fullscreenShader)->second.program;

    glUseProgram(shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glUniform1i(glGetUniformLocation(shader, "screenTexture"), 0);

    glBindVertexArray(m_fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
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
    
} // namespace Eng
