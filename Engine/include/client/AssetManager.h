#pragma once

#include "client/Render/RenderAPI.h"
#include "core/Log.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace Eng::client {

/**
 * @brief 资源管理器（AssetManager）
 * 
 * 统一管理游戏资产的加载与缓存（模型、纹理、着色器）。
 * 通过依赖注入接收 IRenderAPI 指针，将底层资源创建逻辑与业务层解耦。
 * 内置缓存机制，避免相同的文件被重复读取磁盘或重复创建 GPU 资源。
 */
class AssetManager {
public:
    /**
     * @brief 构造资源管理器
     * @param rhi 渲染硬件接口（RHI）指针。必须保证其生命周期长于 AssetManager。
     */
    explicit AssetManager(IRenderAPI* rhi);

    /**
     * @brief 析构函数。
     * @note 会自动释放内部缓存的所有 TexturePtr、ShaderPtr 和 Model（触发 GPU 资源回收）。
     */
    ~AssetManager() = default;

    // 禁止拷贝与赋值，防止 GPU 资源被意外共享或重复释放
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    /**
     * @brief 加载（或从缓存获取）纹理资源。
     * @param path 纹理文件路径（如 "assets/textures/logo.png"）。
     * @return 纹理智能指针，失败返回 nullptr。
     */
    TexturePtr LoadTexture(const std::string& path);

    /**
     * @brief 加载（或从缓存获取）着色器。
     * @details 该函数不再负责路径拼装，仅依据传入的 `info` 进行着色器管线创建。
     *          内部使用 `vertPath + "|" + fragPath` 作为缓存键（Key），确保相同路径的
     *          着色器不会被重复创建，同时避免同名不同路径的冲突。
     * @param info 着色器创建信息。调用方必须填写 `vertPath` 和 `fragPath`。
     *             同时需指定 `pipelineType`（如 Opaque/Transparent/Skybox/UI）和
     *             `vertexLayout`（如 Standard/Skybox）。
     * @return 着色器智能指针（ShaderPtr），加载失败返回 nullptr。
     * @warning `info.vertPath` 和 `info.fragPath` 不能为空。
     */
    ShaderPtr LoadShader(const ShaderCreateInfo& info);

    /**
     * @brief 根据着色器名称和状态，构造标准的 ShaderCreateInfo。
     * @details 自动处理平台差异（Vulkan/OpenGL），拼装引擎默认的着色器路径：
     *          `assets/shaders/build/<name>/vert.(vk|gl).spv`
     *          `assets/shaders/build/<name>/frag.(vk|gl).spv`
     * @param name 着色器名称（对应 assets 下的目录名，如 "mesh", "sky", "water"）。
     * @param pipelineType 渲染管线类型（默认不透明 Opaque）。
     * @param vertexLayout 顶点输入布局类型（默认 Standard）。
     * @return 填充好路径和状态信息的 ShaderCreateInfo，可直接传给 LoadShader。
     */
    ShaderCreateInfo MakeShaderCreateInfo(const std::string& name, 
                                          rhi::PipelineType pipelineType = rhi::PipelineType::Opaque,
                                          rhi::VertexLayout vertexLayout = rhi::VertexLayout::Standard);

    /**
     * @brief 使用 Assimp 加载（或从缓存获取）外部模型。
     * @param path 模型文件路径（如 "assets/models/player.gltf"）。
     * @param flipUV 是否翻转 UV（针对部分 DirectX 导出的模型）。
     * @return 解析后的 Model 结构。如果失败则返回空的 Model（IsValid() == false）。
     */
    Model LoadModel(const std::string& path, bool flipUV = false);

private:
    IRenderAPI* m_rhi = nullptr; ///< 渲染后端接口指针
    std::unique_ptr<Log> m_logger = std::make_unique<Log>();

    // ==================== 缓存池 ====================
    std::unordered_map<std::string, TexturePtr> m_textureCache; ///< 纹理缓存，Key 为文件路径
    std::unordered_map<std::string, ShaderPtr>  m_shaderCache;  ///< 着色器缓存，Key 为名称
    std::unordered_map<std::string, Model>      m_modelCache;   ///< 模型缓存，Key 为路径（含 flipUV 标记）
};

} // namespace Eng::client