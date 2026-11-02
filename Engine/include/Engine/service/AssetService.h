#pragma once

#include "Engine/service/IRenderService.h"
#include "Engine/tool/Log.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstddef>

namespace Eng::service {

class AssetService {
public:
    explicit AssetService(IRenderService* rhi);
    ~AssetService() = default;

    AssetService(const AssetService&) = delete;
    AssetService& operator=(const AssetService&) = delete;

    // ==================== 原始文件读取 ====================
    /**
     * @brief 读取任意二进制文件到只读字节数组。
     * @details 内部用 weak_ptr 做"读盘去重"：同一路径并发/连续调用时，
     *          只要外部仍持有上一次返回的 shared_ptr，就不会重新读盘。
     *          外部全部释放后，缓存项自动过期（弱引用失效）。
     * @return 空文件 / 打不开返回 nullptr。
     */
    std::shared_ptr<const std::vector<std::byte>> ReadFile(const std::string& path);

    // ==================== 纹理 ====================
    TexturePtr LoadTexture(const std::string& path);

    // ==================== 着色器 ====================
    ShaderPtr LoadShader(const rhi::ShaderCreateInfo& info);

    /**
     * @brief 根据着色器名读取 .spv、填充 ShaderCreateInfo。
     * @details 内部流程：
     *          1. 按后端拼路径 assets/shaders/build/<name>/vert.{vk,gl}.spv
     *          2. 调 ReadFile 读入（带 weak_ptr 去重）
     *          3. pipelineType → GraphicsPipelineState::FromLegacy
     *          4. pushConstantSize 直接透传
     * @return 完整可用的 ShaderCreateInfo。读取失败时 vertSpirv / fragSpirv 为
     *         Empty()，IsValid() 会返回 false。
     */
    rhi::ShaderCreateInfo MakeShaderCreateInfo(
        const std::string& name,
        rhi::GraphicsPipelineState pipelineState = rhi::GraphicsPipelineState::PresetOpaque(),
        rhi::VertexLayout vertexLayout = rhi::VertexLayout::Standard,
        uint32_t          pushConstantSize = 0);
private:
    IRenderService* m_rhi = nullptr;
    std::unique_ptr<tool::Log> m_logger = std::make_unique<tool::Log>();

    std::unordered_map<std::string, TexturePtr> m_textureCache;
    std::unordered_map<std::string, ShaderPtr>  m_shaderCache;

    // ★ 文件读取去重缓存：weak_ptr 让外部持有者决定生命周期
    std::unordered_map<std::string,
                       std::weak_ptr<const std::vector<std::byte>>> m_fileReadCache;
};

} // namespace Eng::service