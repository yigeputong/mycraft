#include "client/AssetManager.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <SDL3_image/SDL_image.h>
#include <filesystem>

namespace Eng::client {

AssetManager::AssetManager(IRenderAPI* rhi) : m_rhi(rhi) {}

TexturePtr AssetManager::LoadTexture(const std::string& path) {
    if (!m_rhi) {
        logError(m_logger, "[AssetManager] RHI is null, cannot load texture.");
        return nullptr;
    }

    auto it = m_textureCache.find(path);
    if (it != m_textureCache.end()) {
        return it->second;
    }

    SDL_Surface* loaded = IMG_Load(path.c_str());
    if (!loaded) {
        logError(m_logger, "[AssetManager] IMG_Load failed: " << path << " - " << SDL_GetError());
        return nullptr;
    }

    SDL_Surface* surf = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!surf) {
        logError(m_logger, "[AssetManager] SDL_ConvertSurface failed: " << SDL_GetError());
        return nullptr;
    }

    TexturePtr tex = m_rhi->CreateTexture(static_cast<const uint8_t*>(surf->pixels), surf->w, surf->h);
    
    SDL_DestroySurface(surf);

    if (tex) {
        m_textureCache[path] = tex;
    } else {
        logError(m_logger, "[AssetManager] Failed to create texture from pixels: " << path);
    }
    return tex;
}

ShaderPtr AssetManager::LoadShader(const std::string& name, const ShaderCreateInfo& info) {
    if (!m_rhi) {
        logError(m_logger, "[AssetManager] RHI is null, cannot load shader.");
        return nullptr;
    }

    auto it = m_shaderCache.find(name);
    if (it != m_shaderCache.end()) {
        return it->second;
    }

    bool isVulkan = (m_rhi->GetDeviceInfo().backend == "Vulkan");
    std::string ext = isVulkan ? ".vk.spv" : ".gl.spv";
    std::string base = "assets/shaders/build/" + name + "/";

    ShaderCreateInfo finalInfo = info;
    if (finalInfo.vertPath.empty()) finalInfo.vertPath = base + "vert" + ext;
    if (finalInfo.fragPath.empty()) finalInfo.fragPath = base + "frag" + ext;

    auto shader = m_rhi->CreateShader(finalInfo);
    if (shader) {
        m_shaderCache[name] = shader;
    } else {
        logError(m_logger, "[AssetManager] Failed to load shader: " << name);
    }
    return shader;
}

Model AssetManager::LoadModel(const std::string& path, bool flipUV) {
    if (!m_rhi) {
        logError(m_logger, "[AssetManager] RHI is null, cannot load model.");
        return Model{};
    }

    std::string cacheKey = path + (flipUV ? "_flipped" : "");
    auto it = m_modelCache.find(cacheKey);
    if (it != m_modelCache.end()) {
        return it->second;
    }

    Model result;
    Assimp::Importer importer;
    unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                         aiProcess_JoinIdenticalVertices | aiProcess_OptimizeMeshes;
    if (flipUV) flags |= aiProcess_FlipUVs;

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        logError(m_logger, "[AssetManager] Assimp error: " << importer.GetErrorString());
        return result;
    }

    std::filesystem::path modelDir = std::filesystem::path(path).parent_path();
    std::unordered_map<std::string, TexturePtr> localTexCache;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        aiMesh* mesh = scene->mMeshes[m];
        std::vector<Vertex> vertices;
        
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            Vertex v;
            v.position = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
            v.normal = mesh->mNormals ? glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z) : glm::vec3(0, 1, 0);
            v.uv = mesh->mTextureCoords[0] ? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y) : glm::vec2(0);
            v.ao = 1.0f;
            v.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            vertices.push_back(v);
        }
        
        std::vector<uint32_t> indices;
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            for (unsigned int j = 0; j < mesh->mFaces[f].mNumIndices; ++j) {
                indices.push_back(mesh->mFaces[f].mIndices[j]);
            }
        }

        if (vertices.empty() || indices.empty()) continue;

        MeshData md{ std::move(vertices), std::move(indices) };
        MeshPtr meshPtr = m_rhi->CreateMesh(md);
        if (!meshPtr) continue;

        TexturePtr diffuseTex = nullptr;
        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            aiString texPath;
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                std::string texKey = texPath.C_Str();
                auto cacheIt = localTexCache.find(texKey);
                
                if (cacheIt != localTexCache.end()) {
                    diffuseTex = cacheIt->second;
                } else {
                    const aiTexture* embedded = scene->GetEmbeddedTexture(texKey.c_str());
                    if (embedded && embedded->mHeight > 0) {
                        diffuseTex = m_rhi->CreateTexture(reinterpret_cast<const uint8_t*>(embedded->pcData), embedded->mWidth, embedded->mHeight);
                    } else if (embedded) {
                        SDL_IOStream* io = SDL_IOFromMem(const_cast<void*>(reinterpret_cast<const void*>(embedded->pcData)), embedded->mWidth);
                        if (io) {
                            SDL_Surface* surf = IMG_Load_IO(io, true);
                            if (surf) {
                                SDL_Surface* converted = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
                                if (converted) {
                                    diffuseTex = m_rhi->CreateTexture(static_cast<const uint8_t*>(converted->pixels), converted->w, converted->h);
                                    SDL_DestroySurface(converted);
                                }
                                SDL_DestroySurface(surf);
                            }
                        }
                    } else {
                        std::filesystem::path fullTexPath = modelDir / texKey;
                        if (std::filesystem::exists(fullTexPath)) {
                            diffuseTex = LoadTexture(fullTexPath.string());
                        } else {
                            logWarning(m_logger, "[AssetManager] Texture not found: " << fullTexPath.string());
                        }
                    }
                    localTexCache[texKey] = diffuseTex;
                }
            }
        }
        result.subMeshes.push_back({ meshPtr, diffuseTex });
    }

    m_modelCache[cacheKey] = result;
    return result;
}

} // namespace Eng::client