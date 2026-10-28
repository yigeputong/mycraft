#pragma once

#include <volk.h>
#include <vulkan/vulkan_raii.hpp>
#if defined(_WIN32)
#include <vma/vk_mem_alloc.h>
#else
#include <vk_mem_alloc.h>
#endif

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>

#include "client/Render/RenderAPI.h"
#include "core/Configs.h"
#include "core/Log.h"

namespace Eng::client {

class VulkanAPI : public IRenderAPI {
public:
    VulkanAPI();
    ~VulkanAPI() override;

    void Initialize(int width, int height, Window* window) override;
    void Shutdown() override;

    void BeginFrame() override;
    void EndFrame() override;

    void SetViewport(int x, int y, int w, int h) override;
    void SetClearColor(float r, float g, float b, float a) override;
    void Clear(ClearFlag flag = ClearFlag::All) override;

    void SetGlobalState(const GlobalState& state) override;

    MeshPtr CreateMesh(const MeshData& data) override;
    TexturePtr CreateTexture(const std::string& path) override;
    TexturePtr CreateTextureFromPixels(const uint8_t* rgba, int w, int h) override;
    ShaderPtr CreateShader(const ShaderCreateInfo& info) override;
    FramebufferPtr CreateFramebuffer(const FramebufferCreateInfo& info) override;
    Model LoadModel(const std::string& path, bool flipUV = false) override;

    void BindFramebuffer(const FramebufferPtr& fb) override;
    void UnbindFramebuffer() override;

    void SetUniform(const ShaderPtr& shader, const std::string& name, const glm::mat4& value) override;
    void SetUniform(const ShaderPtr& shader, const std::string& name, const glm::vec3& value) override;
    void SetUniform(const ShaderPtr& shader, const std::string& name, float value) override;
    void SetUniform(const ShaderPtr& shader, const std::string& name, int value) override;

    void DrawMesh(const MeshPtr& mesh, const ShaderPtr& shader, const Material& material, const glm::mat4& model = glm::mat4(1.0f)) override;
    void DrawMeshInstanced(const MeshPtr& mesh, const ShaderPtr& shader, const Material& material, const std::vector<glm::mat4>& transforms) override;
    void DrawMeshBatch(const std::vector<MeshPtr>& meshes, const ShaderPtr& shader, const Material& mat) override;
    void DrawSkybox(const ShaderPtr& shader) override;
    void DrawFullscreenQuad(const TexturePtr& texture) override;

    bool InitImGuiBackend() override;
    void ShutdownImGuiBackend() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderDrawData() override;

    [[nodiscard]] DeviceInfo GetDeviceInfo() const override;
    void ApplySettings(const WindowConfig& win, const RenderConfig& render) override;
    void WaitIdle() override;
    void logValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, const char* message);
    static Eng::client::BackendAvailability Probe();

    static constexpr int api_major = 1;
    static constexpr int api_minor = 4;

private:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };

#ifdef NDEBUG
    static constexpr bool enableValidationLayers = false;
#else
    static constexpr bool enableValidationLayers = true;
#endif

    std::unique_ptr<Log> m_logger = std::make_unique<Log>();

    struct MeshInternal {
        VkBuffer      vertexBuffer   = VK_NULL_HANDLE;
        VmaAllocation vertexAlloc    = VK_NULL_HANDLE;
        VkBuffer      indexBuffer    = VK_NULL_HANDLE;
        VmaAllocation indexAlloc     = VK_NULL_HANDLE;
        uint32_t      indexCount     = 0;
        VkBuffer      instanceBuffer = VK_NULL_HANDLE;
        VmaAllocation instanceAlloc  = VK_NULL_HANDLE;
        uint32_t      instanceCapacity = 0;
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

    std::unordered_map<MeshHandle, MeshInternal>               m_meshes;
    std::unordered_map<ShaderHandle, ShaderInternal>           m_shaders;
    std::unordered_map<TextureHandle, TextureInternal>         m_textures;
    std::unordered_map<FramebufferHandle, FramebufferInternal> m_framebuffers;
    std::unordered_map<std::string, TextureHandle>             m_textureCache;
    std::unordered_map<FramebufferHandle, TextureHandle>       m_fbColorHandles;

    uint32_t m_nextMesh        = 1;
    uint32_t m_nextShader      = 1;
    uint32_t m_nextTexture     = 1;
    uint32_t m_nextFramebuffer = 1;

    Window*                          m_window         = nullptr;
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

    vk::raii::SwapchainKHR           m_swapChain = nullptr;
    std::vector<vk::Image>           m_swapChainImages;
    vk::SurfaceFormatKHR             m_swapChainSurfaceFormat;
    vk::Extent2D                     m_swapChainExtent;
    std::vector<vk::raii::ImageView> m_swapChainImageViews;
    std::vector<bool>                m_swapChainFirstUse;

    vk::raii::Image        m_depthImage       = nullptr;
    vk::raii::DeviceMemory m_depthImageMemory = nullptr;
    vk::raii::ImageView    m_depthImageView   = nullptr;

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

    vk::raii::ShaderModule   m_fullscreenVert     = nullptr;
    vk::raii::ShaderModule   m_fullscreenFrag     = nullptr;
    vk::raii::PipelineLayout m_fullscreenLayout   = nullptr;
    vk::raii::Pipeline       m_fullscreenPipeline = nullptr;

    MeshPtr m_skyCubeMesh = nullptr; // 使用 Ptr 管理
    VkBuffer      m_skyCubeBuffer = VK_NULL_HANDLE;
    VmaAllocation m_skyCubeAlloc  = VK_NULL_HANDLE;

    vk::raii::CommandPool                 m_commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer>  m_commandBuffers;
    std::vector<vk::raii::Semaphore>      m_presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore>      m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence>          m_inFlightFences;
    uint32_t                              m_frameIndex         = 0;
    uint32_t                              m_imageIndex         = 0;
    bool                                  m_frameStarted       = false;
    bool                                  m_framebufferResized = false;
    bool                                  m_imageInAttachment = false;

    GlobalState m_globalState{};
    float         m_clearColor[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };
    uint32_t      m_clearPendingFlags = static_cast<uint32_t>(ClearFlag::All);
    bool          m_renderPassActive = false;

    FramebufferPtr m_pendingFramebuffer = nullptr;

    RenderConfig m_renderConfig;
    WindowConfig m_windowConfig;
    
    // 内部销毁接口
    void DestroyMeshInternal(MeshHandle h);
    void DestroyTextureInternal(TextureHandle h);
    void DestroyShaderInternal(ShaderHandle h);
    void DestroyFramebufferInternal(FramebufferHandle h);

    // 初始化
    bool isDeviceSuitable(const vk::raii::PhysicalDevice& pd);
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createVmaAllocator();
    void createFullscreenPipeline();
    void createSkyboxMesh();

    // Swapchain
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
    vk::PresentModeKHR   chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& modes);
    vk::Extent2D         chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& caps);
    uint32_t             chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& caps);
    void createSwapChain();
    void createImageViews();
    void cleanupSwapChain();
    void recreateSwapChain();

    // 深度 / 图像
    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);
    vk::Format findDepthFormat();
    void       createDepthResources();
    uint32_t   findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
    vk::raii::ImageView createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags);
    std::pair<vk::raii::Image, vk::raii::DeviceMemory> createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties);

    void transition_image_layout(const vk::raii::CommandBuffer& cmd, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage, vk::ImageAspectFlags aspect);

    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    vk::raii::CommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(vk::raii::CommandBuffer&& cmd);

    void createDescriptorPool();
    void createGlobalDescriptors();

    vk::Extent2D getCurrentRenderExtent() const;
    void ensureRenderPassActive();

    static std::vector<char> readFile(const std::string& filename);
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const;
    
    // 底层实际创建的逻辑，返回 handle
    MeshHandle CreateMeshInternal(const MeshData& data);
    TextureHandle CreateTextureInternal(const uint8_t* rgba, int w, int h);
    ShaderHandle CreateShaderInternal(const ShaderCreateInfo& info);
    FramebufferHandle CreateFramebufferInternal(int width, int height);
};

} // namespace Eng::client