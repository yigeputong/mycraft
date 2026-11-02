#pragma once

#define VK_NO_PROTOTYPES
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
#include <cstddef>
#include <span>
#include <vector>
#include <unordered_map>
#include <utility>

#include "Engine/service/IRenderService.h"
#include "Engine/tool/Configs.h"
#include "Engine/tool/Log.h"
#include "Engine/tool/DeferredDeleter.h"

namespace Eng::service {

class VulkanRenderService final : public IRenderService {
public:
    VulkanRenderService();
    ~VulkanRenderService() override;

    // ===== 生命周期 =====
    void Initialize(int width, int height, tool::Window* window) override;
    void Shutdown() override;
    glm::mat4 MakePerspectiveMatrix(float fovYRadians, float aspect, float nearZ, float farZ) const override;

    // ===== 视口 / 清屏 =====
    void SetViewport(int x, int y, int w, int h) override;
    void Clear(rhi::ClearFlag flag = rhi::ClearFlag::All,
               const glm::vec4& color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
               float depth = 1.0f, uint8_t stencil = 0) override;

    // ===== 帧控制 =====
    void BeginFrame() override;
    void EndFrame() override;

    // ===== 资源创建与更新 =====
    BufferPtr      CreateBuffer(const rhi::BufferCreateInfo& info) override;
    TexturePtr     CreateTexture(const rhi::TextureCreateInfo& info) override;
    ShaderPtr      CreateShader(const rhi::ShaderCreateInfo& info) override;
    FramebufferPtr CreateFramebuffer(const rhi::FramebufferCreateInfo& info) override;
    void           UpdateBuffer(const BufferPtr& buffer, size_t offset, std::span<const std::byte> data) override;

    // ===== 帧缓冲绑定 =====
    void BindFramebuffer(const FramebufferPtr& fb) override;
    void UnbindFramebuffer() override;

    // ===== 无状态绑定 =====
    void BindShader(const ShaderPtr& shader) override;
    void BindVertexBuffer(const BufferPtr& buffer, uint32_t binding = 0) override;
    void BindIndexBuffer(const BufferPtr& buffer) override;
    void BindTexture(const TexturePtr& texture, uint32_t slot) override;

    // ===== 常量缓冲 =====
    void BindUniformBuffer(const BufferPtr& buffer, uint32_t set, uint32_t binding) override;
    void PushConstants(const ShaderPtr& shader, std::span<const std::byte> data) override;

    // ===== 绘制 =====
    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0) override;
    void DrawIndexedIndirect(const BufferPtr& indirectBuffer, uint32_t drawCount, uint32_t stride) override;

    // ===== ImGui =====
    bool InitImGuiBackend() override;
    void ShutdownImGuiBackend() override;
    void ImGuiNewFrame() override;
    void ImGuiRenderDrawData() override;

    // ===== 杂项 =====
    DeviceInfo GetDeviceInfo() const override;
    void ApplySettings(const tool::WindowConfig& win, const tool::RenderConfig& render) override;
    void WaitIdle() override;
    void logValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, const char* message);
    static Eng::service::BackendAvailability Probe();

    static constexpr int api_major = 1;
    static constexpr int api_minor = 3;

private:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };

#ifdef NDEBUG
    static constexpr bool enableValidationLayers = false;
#else
    static constexpr bool enableValidationLayers = true;
#endif

    // =========================================================================
    // 阶段一：底层环境
    // =========================================================================
    std::unique_ptr<tool::Log> m_logger = std::make_unique<tool::Log>();
    tool::Window* m_window = nullptr;

    vk::raii::Context                m_context;
    vk::raii::Instance               m_instance       = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;
    vk::raii::SurfaceKHR             m_surface        = nullptr;
    vk::raii::PhysicalDevice         m_physicalDevice = nullptr;
    vk::raii::Device                 m_device         = nullptr;

    DeviceInfo              m_deviceInfo;
    tool::RenderConfig      m_renderConfig;
    tool::WindowConfig      m_windowConfig;
    std::vector<const char*> requiredDeviceExtension = { vk::KHRSwapchainExtensionName };

    VmaAllocator m_allocator = VK_NULL_HANDLE;

    // =========================================================================
    // 阶段二：设备级共享资源
    // =========================================================================
    vk::raii::Queue m_queue      = nullptr;
    uint32_t        m_queueIndex = ~0u;

    vk::raii::SwapchainKHR   m_swapChain = nullptr;
    std::vector<vk::Image>   m_swapChainImages;
    vk::SurfaceFormatKHR     m_swapChainSurfaceFormat;
    vk::Extent2D             m_swapChainExtent;
    std::vector<bool>        m_swapChainFirstUse;

    vk::raii::DescriptorSetLayout m_globalSetLayout   = nullptr;
    vk::raii::DescriptorSetLayout m_textureSetLayout  = nullptr;
    vk::raii::DescriptorPool      m_descriptorPool    = nullptr;
    VkDescriptorPool              m_imguiDescriptorPool = VK_NULL_HANDLE;
    bool                          m_imguiInitialized  = false;

    vk::raii::CommandPool m_commandPool = nullptr;

    // =========================================================================
    // 阶段三：具体业务资源
    // =========================================================================
    std::vector<vk::raii::ImageView> m_swapChainImageViews;

    vk::raii::Image        m_depthImage       = nullptr;
    vk::raii::DeviceMemory m_depthImageMemory = nullptr;
    vk::raii::ImageView    m_depthImageView   = nullptr;

    struct ShaderInternal {
        vk::raii::ShaderModule vertModule = nullptr;
        vk::raii::ShaderModule fragModule = nullptr;
        vk::raii::PipelineLayout layout   = nullptr;
        vk::raii::Pipeline       pipeline = nullptr;
        uint32_t pushConstantSize = 0;
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
        VkImage               colorImage  = VK_NULL_HANDLE;
        VmaAllocation         colorAlloc  = VK_NULL_HANDLE;
        VkImage               depthImage  = VK_NULL_HANDLE;
        VmaAllocation         depthAlloc  = VK_NULL_HANDLE;
        vk::raii::ImageView   colorView   = nullptr;
        vk::raii::ImageView   depthView   = nullptr;
        TextureHandle         colorHandle = 0;
        int                   width       = 0;
        int                   height      = 0;
        bool                  hasDepth    = false;
    };

    struct BufferInternal {
        VkBuffer                 buffer        = VK_NULL_HANDLE;
        VmaAllocation            alloc         = VK_NULL_HANDLE;
        void*                    mapped        = nullptr;
        size_t                   size          = 0;
        vk::raii::DescriptorSet  descriptorSet = nullptr;   // 仅 Uniform 用，懒分配
    };

    // =========================================================================
    // 资源容器
    // =========================================================================
    std::unordered_map<ShaderHandle,      ShaderInternal>        m_shaders;
    std::unordered_map<TextureHandle,     TextureInternal>       m_textures;
    std::unordered_map<FramebufferHandle, FramebufferInternal>   m_framebuffers;
    std::unordered_map<FramebufferHandle, TextureHandle>         m_fbColorHandles;
    std::unordered_map<BufferHandle,      BufferInternal>        m_buffers;

    std::vector<vk::raii::CommandBuffer> m_commandBuffers;
    std::vector<vk::raii::Semaphore>     m_presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore>     m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence>         m_inFlightFences;

    // =========================================================================
    // 阶段四：全局数据与状态
    // =========================================================================
    uint64_t     m_frameCounter    = 0;
    uint32_t     m_nextShader      = 1;
    uint32_t     m_nextTexture     = 1;
    uint32_t     m_nextFramebuffer = 1;
    BufferHandle m_nextBuffer      = 1;
    TexturePtr    m_defaultTexturePtr = nullptr;
    TextureHandle m_defaultTexture    = 0;

    float       m_clearColor[4]     = { 0.0f, 0.0f, 0.0f, 1.0f };
    uint32_t    m_clearPendingFlags = static_cast<uint32_t>(rhi::ClearFlag::All);

    uint32_t m_frameIndex         = 0;
    uint32_t m_imageIndex         = 0;
    bool     m_frameStarted       = false;
    bool     m_framebufferResized = false;

    bool           m_isShutdown         = false;
    FramebufferPtr m_pendingFramebuffer = nullptr;

    // 无状态绘制状态缓存
    vk::raii::PipelineLayout* m_currentLayout = nullptr;
    vk::DescriptorSet         m_boundGlobalUBO = nullptr;

    Eng::tool::DeferredDeleter m_deferDeleter;

    // =========================================================================
    // 初始化
    // =========================================================================
    bool isDeviceSuitable(const vk::raii::PhysicalDevice& pd);
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createVmaAllocator();
    void createGlobalDescriptors();

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
    vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates,
                                   vk::ImageTiling tiling, vk::FormatFeatureFlags features);
    vk::Format findDepthFormat();
    void       createDepthResources();
    uint32_t   findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
    vk::raii::ImageView createImageView(vk::Image image, vk::Format format,
                                        vk::ImageAspectFlags aspectFlags, uint32_t mipLevels = 1);
    std::pair<vk::raii::Image, vk::raii::DeviceMemory> createImage(
        uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling,
        vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties);

    void transition_image_layout(const vk::raii::CommandBuffer& cmd, vk::Image image,
                                 vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                 vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess,
                                 vk::PipelineStageFlags2 srcStage,
                                 vk::PipelineStageFlags2 dstStage,
                                 vk::ImageAspectFlags aspect);

    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    vk::raii::CommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(vk::raii::CommandBuffer&& cmd);

    void         createDescriptorPool();
    vk::Extent2D getCurrentRenderExtent() const;

    vk::raii::ShaderModule createShaderModule(std::span<const std::byte> code) const;

    // =========================================================================
    // 内部辅助
    // =========================================================================
    TextureHandle     CreateTextureInternal(const uint8_t* rgba, int w, int h, int requestedMips = 0);
    ShaderHandle      CreateShaderInternal(const rhi::ShaderCreateInfo& info);
    FramebufferHandle CreateFramebufferInternal(const rhi::FramebufferCreateInfo& info);
    BufferHandle      CreateBufferInternal(size_t size, rhi::BufferUsage usage);

    void DestroyTextureInternal(TextureHandle h);
    void DestroyShaderInternal(ShaderHandle h);
    void DestroyFramebufferInternal(FramebufferHandle h);
    void DestroyBufferInternal(BufferHandle h);
};

} // namespace Eng::service