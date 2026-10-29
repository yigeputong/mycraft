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
     * @param name 着色器名称。内部会自动拼接 `assets/shaders/build/<name>/` 路径，并根据当前后端（OpenGL/Vulkan）自动补全后缀。
     * @param info 自定义着色器配置（如是否开启深度测试、是否是天空盒）。默认为普通不透明渲染。
     * @return 着色器智能指针，失败返回 nullptr。
     */
    ShaderPtr LoadShader(const std::string& name, const ShaderCreateInfo& info = {});

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