#pragma once

#include <volk.h>
#include <vulkan/vulkan_raii.hpp>
#include <vma/vk_mem_alloc.h>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>

#include "client/Render/RenderAPI.h"
#include "core/Log.h"

namespace Eng::client {

class VulkanAPI : public IRenderAPI {
public:
    VulkanAPI();
    ~VulkanAPI() override;

    // ---- 生命周期 ----
    bool Initialize(int width, int height, Window* window) override;
    void Shutdown() override;

    // ---- 帧循环 ----
    void BeginFrame() override;
    void EndFrame() override;

    // ---- 视口 / 清屏 ----
    void SetViewport(int x, int y, int w, int h) override;
    void SetClearColor(float r, float g, float b, float a) override;
    void Clear() override;

    // ---- 矩阵 / 光照 ----
    void SetViewMatrix(const glm::mat4& view) override;
    void SetProjectionMatrix(const glm::mat4& proj) override;
    void SetModelMatrix(const glm::mat4& model) override;
    void SetLightPosition(const glm::vec3& pos) override;
    void SetLightColor(const glm::vec3& color, float intensity) override;
    void SetLightAmbient(const glm::vec3& amb) override;
    void SetViewPosition(const glm::vec3& pos) override;

    // ---- 资源创建 ----
    MeshHandle    CreateMesh(const MeshData& data) override;
    MeshHandle    CreateMeshInstance(const MeshData& data) override;
    TextureHandle CreateTexture(const std::string& path) override;
    TextureHandle CreateTextureFromMemory(const aiTexture* embedded) override;
    TextureHandle CreateTextureFromPixels(const uint8_t* rgba, int w, int h) override;
    ShaderHandle  CreateShader(const std::string& vertPath,
                               const std::string& fragPath) override;
    ShaderHandle  CreateSkybox(const std::string& vertPath,
                               const std::string& fragPath) override;
    Model         LoadModel(const std::string& path, bool flipUV = false) override;

    // ---- Framebuffer ----
    Framebuffer   CreateFramebuffer(int width, int height) override;
    void          BindFramebuffer(const Framebuffer& fb) override;
    void          UnbindFramebuffer() override;
    TextureHandle GetFramebufferTexture(const Framebuffer& fb) const override;
    void          DestroyFramebuffer(const Framebuffer& fb) override;

    // ---- 销毁 ----
    void DestroyMesh(MeshHandle handle) override;
    void DestroyTexture(TextureHandle handle) override;
    void DestroyShader(ShaderHandle handle) override;

    // ---- Uniform ----
    void SetUniform(ShaderHandle shader, const std::string& name, const glm::mat4& value) override;
    void SetUniform(ShaderHandle shader, const std::string& name, const glm::vec3& value) override;
    void SetUniform(ShaderHandle shader, const std::string& name, float value) override;
    void SetUniform(ShaderHandle shader, const std::string& name, int value) override;

    // ---- 绘制 ----
    void DrawMesh(MeshHandle mesh, ShaderHandle shader, const Material& material) override;
    void DrawMeshInstanced(MeshHandle mesh, ShaderHandle shader, const Material& material,
                           const std::vector<glm::mat4>& transforms) override;
    void DrawSkybox(ShaderHandle shader) override;
    void DrawFullscreenQuad(TextureHandle textureID) override;

    bool InitImGuiBackend() override;
    void ShutdownImGuiBackend() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderDrawData() override;

    DeviceInfo GetDeviceInfo() const override;
    void logValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, const char* message);

private:
    // ==================== 常量 ====================
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };

#ifdef NDEBUG
    static constexpr bool enableValidationLayers = false;
#else
    static constexpr bool enableValidationLayers = true;
#endif

    // 记录日志
    std::unique_ptr<Log> m_logger = std::make_unique<Log>();

    // ==================== 资源结构 ====================
    struct MeshInternal {
        VkBuffer      vertexBuffer   = VK_NULL_HANDLE;
        VmaAllocation vertexAlloc    = VK_NULL_HANDLE;
        VkBuffer      indexBuffer    = VK_NULL_HANDLE;
        VmaAllocation indexAlloc     = VK_NULL_HANDLE;
        uint32_t      indexCount     = 0;
        VkBuffer      instanceBuffer = VK_NULL_HANDLE;
        VmaAllocation instanceAlloc  = VK_NULL_HANDLE;
    };

    struct ShaderInternal {
        vk::raii::ShaderModule vertModule = nullptr;
        vk::raii::ShaderModule fragModule = nullptr;
        vk::raii::PipelineLayout layout   = nullptr;
        vk::raii::Pipeline       pipeline = nullptr;
    };

    struct TextureInternal {
        VkImage                 image     = VK_NULL_HANDLE;
        VmaAllocation           alloc     = VK_NULL_HANDLE;
        vk::raii::ImageView     view      = nullptr;
        vk::raii::Sampler       sampler   = nullptr;
        vk::raii::DescriptorSet set       = nullptr;
        bool                    ownsImage = true;
    };

    struct FramebufferInternal {
        VkImage               colorImage   = VK_NULL_HANDLE;
        VmaAllocation         colorAlloc   = VK_NULL_HANDLE;
        vk::raii::ImageView   colorView    = nullptr;
        vk::raii::Sampler     colorSampler = nullptr;
        VkImage               depthImage   = VK_NULL_HANDLE;
        VmaAllocation         depthAlloc   = VK_NULL_HANDLE;
        vk::raii::ImageView   depthView    = nullptr;
        TextureHandle         colorHandle  = 0;
        int                   width        = 0;
        int                   height       = 0;
    };

    // ==================== 资源表 ====================
    std::unordered_map<MeshHandle, MeshInternal>               m_meshes;
    std::unordered_map<ShaderHandle, ShaderInternal>           m_shaders;
    std::unordered_map<TextureHandle, TextureInternal>         m_textures;
    std::unordered_map<FramebufferHandle, FramebufferInternal> m_framebuffers;
    std::unordered_map<std::string, TextureHandle>             m_textureCache;

    uint32_t m_nextMesh        = 1;
    uint32_t m_nextShader      = 1;
    uint32_t m_nextTexture     = 1;
    uint32_t m_nextFramebuffer = 1;

    // ==================== Vulkan 核心 ====================
    SDL_Window*                      m_window         = nullptr;
    vk::raii::Instance               m_instance       = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;
    vk::raii::PhysicalDevice         m_physicalDevice = nullptr;
    vk::raii::Device                 m_device         = nullptr;
    VmaAllocator                     m_allocator      = VK_NULL_HANDLE;
    uint32_t                         m_queueIndex     = ~0u;
    vk::raii::Queue                  m_queue          = nullptr;
    vk::raii::SurfaceKHR             m_surface        = nullptr;

    std::vector<const char*> requiredDeviceExtension = { vk::KHRSwapchainExtensionName };

    DeviceInfo m_deviceInfo;

    // ==================== Swapchain ====================
    vk::raii::SwapchainKHR           m_swapChain = nullptr;
    std::vector<vk::Image>           m_swapChainImages;
    vk::SurfaceFormatKHR             m_swapChainSurfaceFormat;
    vk::Extent2D                     m_swapChainExtent;
    std::vector<vk::raii::ImageView> m_swapChainImageViews;

    // ==================== 深度 ====================
    vk::raii::Image        m_depthImage       = nullptr;
    vk::raii::DeviceMemory m_depthImageMemory = nullptr;
    vk::raii::ImageView    m_depthImageView   = nullptr;

    // ==================== Descriptor / UBO ====================
    vk::raii::DescriptorSetLayout        m_globalSetLayout  = nullptr;
    vk::raii::DescriptorSetLayout        m_textureSetLayout = nullptr;
    vk::raii::DescriptorPool             m_descriptorPool   = nullptr;
    std::vector<VkBuffer>                m_globalUBOs;
    std::vector<VmaAllocation>           m_globalUBOAllocs;
    std::vector<void*>                   m_globalUBOMapped;
    std::vector<vk::raii::DescriptorSet> m_globalSets;
    TextureHandle                        m_defaultTexture   = 0;
    VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;
    bool m_imguiInitialized = false;

    // ==================== 全屏 quad ====================
    vk::raii::ShaderModule   m_fullscreenVert     = nullptr;
    vk::raii::ShaderModule   m_fullscreenFrag     = nullptr;
    vk::raii::PipelineLayout m_fullscreenLayout   = nullptr;
    vk::raii::Pipeline       m_fullscreenPipeline = nullptr;

    // ==================== Skybox ====================
    VkBuffer      m_skyCubeBuffer = VK_NULL_HANDLE;
    VmaAllocation m_skyCubeAlloc  = VK_NULL_HANDLE;

    // ==================== 帧循环 ====================
    vk::raii::CommandPool                 m_commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer>  m_commandBuffers;
    std::vector<vk::raii::Semaphore>      m_presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore>      m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence>          m_inFlightFences;
    uint32_t                              m_frameIndex         = 0;
    uint32_t                              m_imageIndex         = 0;
    bool                                  m_frameStarted       = false;
    bool                                  m_framebufferResized = false;

    // ==================== CPU 端状态缓存 ====================
    GlobalUBOData m_globalUBOData{};
    glm::mat4     m_modelMatrix{ 1.0f };
    float         m_clearColor[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool          m_clearPending     = true;
    bool          m_renderPassActive = false;

    Framebuffer m_pendingFramebuffer{};

    // ==================== 内部方法 ====================
    // --- 初始化 ---
    bool isDeviceSuitable(const vk::raii::PhysicalDevice& pd);
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void logDeviceInfo();
    void createLogicalDevice();
    void createVmaAllocator();
    void createFullscreenPipeline();
    void createSkyboxMesh();

    // --- Swapchain ---
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
    vk::PresentModeKHR   chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& modes);
    vk::Extent2D         chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& caps);
    uint32_t             chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& caps);
    void createSwapChain();
    void createImageViews();
    void cleanupSwapChain();
    void recreateSwapChain();

    // --- 深度 ---
    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates,
                                   vk::ImageTiling tiling,
                                   vk::FormatFeatureFlags features);
    vk::Format findDepthFormat();
    void       createDepthResources();
    uint32_t   findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

    // --- 图像工具 ---
    vk::raii::ImageView createImageView(vk::Image image, vk::Format format,
                                        vk::ImageAspectFlags aspectFlags);
    std::pair<vk::raii::Image, vk::raii::DeviceMemory>
        createImage(uint32_t width, uint32_t height, vk::Format format,
                    vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                    vk::MemoryPropertyFlags properties);

    // --- 布局转换 ---
    void transition_image_layout(const vk::raii::CommandBuffer& cmd,
                                 vk::Image image,
                                 vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                 vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess,
                                 vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage,
                                 vk::ImageAspectFlags aspect);

    // --- 命令 / 同步 ---
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    vk::raii::CommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(vk::raii::CommandBuffer&& cmd);

    // --- Descriptor ---
    void createDescriptorPool();
    void createGlobalDescriptors();

    // --- 渲染 ---
    void ensureRenderPassActive();

    // --- Shader 工具 ---
    static std::vector<char> readFile(const std::string& filename);
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const;
    ShaderHandle createShaderInternal(const std::string& vertPath,
                                    const std::string& fragPath,
                                    bool isSky,
                                    const char* vertEntry = "main",
                                    const char* fragEntry = "main");
};

} // namespace Eng::client