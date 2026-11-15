#include "game/core/ecs/RenderSystem.h"

#include <cmath>
#include <unordered_map>

#include "game/core/ecs/EntityManager.h"

namespace game::core::ecs {

void RenderSystem::Render(EntityManager& world, const RenderContext& ctx) {
    std::unordered_map<BlockId, std::vector<Eng::service::rhi::PerObjectData>> itemInstances;

    for (auto& e : world.GetEntities()) {
        if (!e.active || !e.renderable)
            continue;
        auto& rc = *e.renderable;

        // 1. 渲染模型（猪）
        if (rc.model) {
            glm::mat4 worldMat = glm::translate(glm::mat4(1.0f), e.transform.position);
            worldMat = glm::rotate(worldMat, glm::radians(e.transform.yaw), glm::vec3(0, 1, 0));
            auto it = rc.model->clips.find(rc.currentClip);
            if (it != rc.model->clips.end()) {
                const auto& clip = it->second;
                float loopTime = (clip.duration > 0.0f) ? std::fmod(rc.animTime, clip.duration) : 0.0f;
                ApplyAnimationToNodes(rc.model->rootNode.get(), clip, loopTime);
            }
            RenderNode(rc.model->rootNode.get(), worldMat, rc, ctx.renderer, ctx.entityShader);
        }

        // 2. 收集掉落物实例化数据
        if (e.item && rc.blockType != AIR_ID) {
            const float entityPhase = static_cast<float>(GetEntityIndex(e.id)) * 0.5f;

            Eng::service::rhi::PerObjectData data;
            data.model = glm::translate(glm::mat4(1.0f), e.transform.position);
            data.model = glm::rotate(data.model, e.item->bobTime * 2.0f, glm::vec3(0, 1, 0));
            data.model = glm::translate(
                data.model, glm::vec3(0.0f, std::sin(e.item->bobTime * 3.0f + entityPhase) * 0.1f + 0.1f, 0.0f));

            if (e.item->isPickingUp) {
                float scale = 1.0f - e.item->pickupT;
                data.model = glm::scale(data.model, glm::vec3(0.25f * scale));
            } else {
                data.model = glm::scale(data.model, glm::vec3(0.25f));
            }

            itemInstances[rc.blockType].push_back(data);
        }
    }

    // 3. 提交掉落物批量渲染
    for (const auto& [type, perObjects] : itemInstances) {
        const auto& mesh = ctx.getItemMeshFunc(type);
        if (mesh.vbo) {
            ctx.renderer->SubmitInstancedMesh(mesh, ctx.entityShader, ctx.atlasTexture, perObjects);
        }
    }
}

void RenderSystem::RenderNode(client::ModelNode* node, const glm::mat4& parentTransform, const RenderableComponent& rc,
                              Eng::client::SceneRenderer* renderer, const Eng::service::ShaderPtr& entityShader) {
    glm::mat4 localMat = node->baseTransform * node->currentAnimTransform;
    glm::mat4 worldMat = parentTransform * localMat;
    for (uint32_t idx : node->meshIndices) {
        renderer->SubmitMesh(rc.model->meshes[idx], entityShader, rc.model->texture, {.model = worldMat});
    }
    for (const auto& child : node->children)
        RenderNode(child.get(), worldMat, rc, renderer, entityShader);
}

void RenderSystem::ApplyAnimationToNodes(client::ModelNode* node, const client::AnimationClip& clip, float animTime) {
    node->currentAnimTransform = glm::mat4(1.0f);
    for (const auto& channel : clip.channels) {
        if (channel.nodeName == node->name && channel.keyframes.size() >= 2) {
            size_t idx1 = 0;
            for (size_t i = 0; i < channel.keyframes.size(); ++i) {
                if (channel.keyframes[i].time <= animTime)
                    idx1 = i;
                else
                    break;
            }
            size_t idx2 = (idx1 + 1) % channel.keyframes.size();
            const auto& k1 = channel.keyframes[idx1];
            const auto& k2 = channel.keyframes[idx2];
            float timeDiff = k2.time - k1.time;
            if (timeDiff < 0.0f && clip.duration > 0.0f)
                timeDiff += clip.duration;
            float t = (timeDiff > 0.0f) ? (animTime - k1.time) / timeDiff : 0.0f;
            if (t < 0.0f)
                t += 1.0f;
            glm::quat finalRot = glm::slerp(k1.rotation, k2.rotation, t);
            node->currentAnimTransform = glm::mat4_cast(finalRot);
            break;
        }
    }
    for (const auto& child : node->children)
        ApplyAnimationToNodes(child.get(), clip, animTime);
}

}  // namespace game::core::ecs