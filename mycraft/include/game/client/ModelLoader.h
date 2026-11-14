#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Engine/client/SceneRenderer.h"

namespace game::client {

// 关键帧数据（只保留旋转，因为猪的位移/缩放没变）
struct Keyframe {
    float time = 0.0f;
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

// 动画通道（对应一个骨骼/节点的动画数据）
struct AnimationChannel {
    std::string nodeName;
    std::vector<Keyframe> keyframes;
};

// 动画片段
struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationChannel> channels;
};

// 模型场景节点
struct ModelNode {
    std::string name;
    glm::mat4 baseTransform = glm::mat4(1.0f);         // 初始相对矩阵
    glm::mat4 currentAnimTransform = glm::mat4(1.0f);  // 运行时动画矩阵
    std::vector<uint32_t> meshIndices;
    std::vector<std::unique_ptr<ModelNode>> children;
};

// 完整模型
struct LoadedModel {
    std::vector<Eng::client::RenderMesh> meshes;
    std::unique_ptr<ModelNode> rootNode;
    std::unordered_map<std::string, AnimationClip> clips;
    Eng::service::TexturePtr texture;
};

// ECS 预热组件
struct RenderableComponent {
    LoadedModel* model = nullptr;
    std::string currentClip = "Walk";
    float animTime = 0.0f;
    bool isLooping = true;
};

// 静态加载工具类
class ModelLoader {
public:
    static std::unique_ptr<LoadedModel> Load(Eng::service::IRenderService* renderer,
                                             Eng::service::AssetService* assetManager, const std::string& modelPath,
                                             const std::string& texturePath);
};

}  // namespace game::client