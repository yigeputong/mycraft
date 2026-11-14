#include "game/client/ModelLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <functional>

#include "Engine/tool/Log.h"

namespace game::client {

std::unique_ptr<LoadedModel> ModelLoader::Load(Eng::service::IRenderService* renderer,
                                               Eng::service::AssetService* assetManager, const std::string& modelPath,
                                               const std::string& texturePath) {
    auto logger = std::make_unique<Eng::tool::Log>();  // 或用全局 Logger
    Assimp::Importer importer;

    // 读取并处理网格
    const aiScene* scene =
        importer.ReadFile(modelPath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_JoinIdenticalVertices);

    if (!scene || !scene->mRootNode) {
        logError(logger, "[ModelLoader] 加载失败: " << modelPath);
        return nullptr;
    }

    auto model = std::make_unique<LoadedModel>();
    model->texture = assetManager->LoadTexture(texturePath);
    if (!model->texture) {
        logError(logger, "[ModelLoader] 贴图加载失败: " << texturePath);
    }

    // 1. 解析所有的 Mesh
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        std::vector<Eng::service::Vertex> vertices;
        std::vector<uint32_t> indices;

        for (unsigned int v = 0; v < mesh->mNumVertices; v++) {
            Eng::service::Vertex vertex;
            vertex.position = {mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z};
            if (mesh->HasNormals()) {
                vertex.normal = {mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z};
            }
            if (mesh->mTextureCoords[0]) {
                vertex.uv = {mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y};
            }
            vertex.layer = 0.0f;  // 单层贴图
            vertex.ao = 1.0f;
            vertex.light = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            vertices.push_back(vertex);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            aiFace face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j]);
            }
        }

        Eng::client::RenderMesh renderMesh;
        renderMesh.vbo = renderer->CreateBuffer(
            {.size = vertices.size() * sizeof(Eng::service::Vertex), .usage = Eng::service::rhi::BufferUsage::Vertex});
        renderer->UpdateBuffer(renderMesh.vbo, 0, std::as_bytes(std::span(vertices)));
        renderMesh.ibo = renderer->CreateBuffer(
            {.size = indices.size() * sizeof(uint32_t), .usage = Eng::service::rhi::BufferUsage::Index});
        renderer->UpdateBuffer(renderMesh.ibo, 0, std::as_bytes(std::span(indices)));
        renderMesh.indexCount = static_cast<uint32_t>(indices.size());
        renderMesh.vertexCount = static_cast<uint32_t>(vertices.size());
        renderMesh.indexed = true;

        model->meshes.push_back(std::move(renderMesh));
    }

    // 2. 递归构建节点树 (注意 Assimp 的矩阵转置)
    std::function<std::unique_ptr<ModelNode>(aiNode*)> buildTree = [&](aiNode* aiNode) -> std::unique_ptr<ModelNode> {
        auto node = std::make_unique<ModelNode>();
        node->name = aiNode->mName.C_Str();

        aiMatrix4x4 m = aiNode->mTransformation;
        node->baseTransform = glm::transpose(glm::make_mat4(&m.a1));

        for (unsigned int i = 0; i < aiNode->mNumMeshes; i++) {
            node->meshIndices.push_back(aiNode->mMeshes[i]);
        }

        for (unsigned int i = 0; i < aiNode->mNumChildren; i++) {
            node->children.push_back(buildTree(aiNode->mChildren[i]));
        }
        return node;
    };
    model->rootNode = buildTree(scene->mRootNode);

    // 3. 解析动画（★ 核心修复：合并所有的动画轨道）
    if (scene->mNumAnimations > 0) {
        AnimationClip clip;
        clip.name = "Walk";  // 强行给个名字

        // 遍历 glb 里的所有 Animation
        for (unsigned int a = 0; a < scene->mNumAnimations; a++) {
            aiAnimation* anim = scene->mAnimations[a];

            // 获取时间（如果不同动画时间不一样，取最大的那个）
            float duration = static_cast<float>(anim->mDuration / anim->mTicksPerSecond);
            if (duration > clip.duration)
                clip.duration = duration;

            // 遍历当前 Animation 的所有通道
            for (unsigned int i = 0; i < anim->mNumChannels; i++) {
                aiNodeAnim* channel = anim->mChannels[i];
                AnimationChannel animChan;
                animChan.nodeName = channel->mNodeName.C_Str();

                // 解析旋转关键帧
                for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
                    aiQuatKey key = channel->mRotationKeys[k];
                    Keyframe frame;
                    frame.time = static_cast<float>(key.mTime / anim->mTicksPerSecond);
                    frame.rotation = glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z);
                    animChan.keyframes.push_back(frame);
                }

                if (!animChan.keyframes.empty()) {
                    clip.channels.push_back(animChan);
                    logInfo(logger, "[Anim] 合并通道: " << animChan.nodeName);
                }
            }
        }

        model->clips[clip.name] = clip;
        logInfo(logger,
                "[ModelLoader] 成功合并模型动画，总时长: " << clip.duration << "s, 通道数: " << clip.channels.size());
    }

    return model;
}

}  // namespace game::client