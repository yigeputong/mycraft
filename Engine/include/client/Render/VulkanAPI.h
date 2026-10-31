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
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>

#include "client/Render/RenderAPI.h"
#include "core/Configs.h"
#include "core/Log.h"

namespace Eng::client {

class VulkanAPI final : public IRenderAPI {
public:
    VulkanAPI();
    ~VulkanAPI() override;

    void Initialize(int width, int height, Window* window) override;
    void Shutdown() override;

    void InitPipelines() override;
    void BindPipeline(rhi::PipelineType type) override;

    void BeginFrame() override;
    void EndFrame() override;

    void SetViewport(int x, int y, int w, int h) override;
    void SetClearColor(float r, float g, float b, float a) override;
    void Clear(rhi::ClearFlag flag = rhi::ClearFlag::All) override;
    void SetGlobalState(const GlobalState& state) override;

    // ★ MDI 接口
    void UploadGlobalGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                                size_t& outVboCapacity, size_t& outIboCapacity) override;
    void UpdateGlobalGeometry(uint32_t vertexOffset, const std::vector<Vertex>& vertices,
                            uint32_t indexOffset, const std::vector<uint32_t>& indices) override;
    void UploadIndirectCommands(const std::vector<rhi::DrawIndexedIndirectCommand>& commands) override;
    void DrawMDI(const ShaderPtr& shader, const Material& material, uint32_t commandCount) override;

    MeshPtr CreateMesh(const MeshData& data) override;
    TexturePtr CreateTexture(const uint8_t* rgba, int w, int h) override;
    ShaderPtr CreateShader(const ShaderCreateInfo& info) override;
    FramebufferPtr CreateFramebuffer(const FramebufferCreateInfo& info) override;

    void BindFramebuffer(const FramebufferPtr& fb) override;
    void UnbindFramebuffer() override;

    void SetUniform(const ShaderPtr& shader, const std::string& name, const glm::mat4& value) override;
    void SetUniform(const ShaderPtr& shader, const std::string& name, const glm::vec3& value) override;
    void SetUniform(const ShaderPtr& shader, const std::string& name, float value) override;
    void SetUniform(const ShaderPtr& shader, const std::string& name, int value) override;

    void DrawMesh(const MeshPtr& mesh, const ShaderPtr& shader,
                  const Material& material,
                  const rhi::PerObjectData& data = {}) override;
    void DrawMeshInstanced(const MeshPtr& mesh, const ShaderPtr& shader,
                           const Material& material,
                           const std::vector<rhi::PerObjectData>& perObjects) override;
    void DrawSkybox(const ShaderPtr& shader) override;
    void BlitToScreen(const TexturePtr& texture) override;

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
    std::unique_ptr<Log> m_logger = std::make_unique<Log>();
    Window* m_window = nullptr;

    vk::raii::Context                m_context;
    vk::raii::Instance               m_instance       = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;
    vk::raii::SurfaceKHR             m_surface        = nullptr;
    vk::raii::PhysicalDevice         m_physicalDevice = nullptr;
    
    // 设备（最基础依赖，最先声明，最后析构）
    vk::raii::Device                 m_device         = nullptr;

    DeviceInfo m_deviceInfo;
    RenderConfig m_renderConfig;
    WindowConfig m_windowConfig;
    std::vector<const char*> requiredDeviceExtension = { vk::KHRSwapchainExtensionName };

    // VMA 分配器，必须在所有 VMA 资源之后销毁
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    // =========================================================================
    // 阶段二：设备级共享资源（依赖 Device）
    // =========================================================================
    vk::raii::Queue               m_queue          = nullptr;
    uint32_t                      m_queueIndex     = ~0u;
    
    vk::raii::SwapchainKHR           m_swapChain = nullptr;
    std::vector<vk::Image>           m_swapChainImages;
    vk::SurfaceFormatKHR             m_swapChainSurfaceFormat;
    vk::Extent2D                     m_swapChainExtent;
    std::vector<bool>                m_swapChainFirstUse;

    vk::raii::DescriptorSetLayout        m_globalSetLayout  = nullptr;
    vk::raii::DescriptorSetLayout        m_textureSetLayout = nullptr;
    vk::raii::DescriptorPool             m_descriptorPool   = nullptr;
    VkDescriptorPool                     m_imguiDescriptorPool = VK_NULL_HANDLE;
    bool                                 m_imguiInitialized = false;

    vk::raii::CommandPool m_commandPool = nullptr;

    // =========================================================================
    // 阶段三：具体业务资源（依赖 Pool 和 Device）
    // =========================================================================
    std::vector<vk::raii::ImageView> m_swapChainImageViews;

    vk::raii::Image        m_depthImage       = nullptr;
    vk::raii::DeviceMemory m_depthImageMemory = nullptr;
    vk::raii::ImageView    m_depthImageView   = nullptr;

    std::unordered_map<int, vk::raii::Pipeline> m_pipelines; 

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
        rhi::PerObjectMode       perObjectMode = rhi::PerObjectMode::None;
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
        VkImage               depthImage   = VK_NULL_HANDLE;
        VmaAllocation         depthAlloc   = VK_NULL_HANDLE;
        vk::raii::ImageView   colorView    = nullptr;
        vk::raii::ImageView   depthView    = nullptr;
        TextureHandle         colorHandle  = 0;
        int                   width        = 0;
        int                   height       = 0;
        bool                  hasDepth     = false;
    };

    struct PendingDestroy {
        VkBuffer vertexBuffer; VmaAllocation vertexAlloc;
        VkBuffer indexBuffer;  VmaAllocation indexAlloc;
        VkBuffer instanceBuffer; VmaAllocation instanceAlloc;
    };
    
    struct DeferredDeletion {
        uint64_t retireFrame;
        PendingDestroy p;
    };

    // MDI 多帧缓冲结构体
    struct FrameMDIBuffers {
        VkBuffer      vbo = VK_NULL_HANDLE;
        VmaAllocation vboAlloc = VK_NULL_HANDLE;
        void*         vboMapped = nullptr;
        VkDeviceSize  vboSize = 0;

        VkBuffer      ibo = VK_NULL_HANDLE;
        VmaAllocation iboAlloc = VK_NULL_HANDLE;
        void*         iboMapped = nullptr;
        VkDeviceSize  iboSize = 0;

        VkBuffer      indirectBuffer = VK_NULL_HANDLE;
        VmaAllocation indirectAlloc = VK_NULL_HANDLE;
        void*         indirectMapped = nullptr;
        uint32_t      indirectCapacity = 0;
    };

    // 各种含有 RAII 对象或 VMA 的容器
    std::unordered_map<MeshHandle, MeshInternal>               m_meshes;
    std::unordered_map<ShaderHandle, ShaderInternal>           m_shaders;
    std::unordered_map<TextureHandle, TextureInternal>         m_textures;
    std::unordered_map<FramebufferHandle, FramebufferInternal> m_framebuffers;
    std::unordered_map<FramebufferHandle, TextureHandle>       m_fbColorHandles;
    std::unordered_map<uint64_t, vk::raii::DescriptorSet>      m_postProcessSets;

    std::vector<vk::raii::DescriptorSet> m_globalSets;
    std::vector<vk::raii::CommandBuffer> m_commandBuffers;
    std::vector<vk::raii::Semaphore>     m_presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore>     m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence>         m_inFlightFences;

    std::vector<FrameMDIBuffers> m_frameMDIBuffers;

    MeshPtr m_skyCubeMesh = nullptr; 

    // =========================================================================
    // 阶段四：全局数据与状态
    // =========================================================================
    std::vector<DeferredDeletion> m_deletionQueue;
    uint64_t m_frameCounter = 0;

    uint32_t m_nextMesh        = 1;
    uint32_t m_nextShader      = 1;
    uint32_t m_nextTexture     = 1;
    uint32_t m_nextFramebuffer = 1;

    // 裸 VMA 缓冲
    VkBuffer      m_skyCubeBuffer = VK_NULL_HANDLE;
    VmaAllocation m_skyCubeAlloc  = VK_NULL_HANDLE;

    std::vector<VkBuffer>      m_globalUBOs;
    std::vector<VmaAllocation> m_globalUBOAllocs;
    std::vector<void*>         m_globalUBOMapped;

    TexturePtr    m_defaultTexturePtr = nullptr;
    TextureHandle m_defaultTexture   = 0;

    GlobalState m_globalState{};
    float       m_clearColor[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };
    uint32_t    m_clearPendingFlags = static_cast<uint32_t>(rhi::ClearFlag::All);

    uint32_t m_frameIndex         = 0;
    uint32_t m_imageIndex         = 0;
    bool     m_frameStarted       = false;
    bool     m_framebufferResized = false;

    bool m_isShutdown = false;
    FramebufferPtr m_pendingFramebuffer = nullptr;

    // 初始化
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
    vk::Extent2D getCurrentRenderExtent() const;

    std::vector<char> readFile(const std::string& filename);
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const;
    void createSkyboxMesh();
    
    MeshHandle CreateMeshInternal(const MeshData& data);
    TextureHandle CreateTextureInternal(const uint8_t* rgba, int w, int h);
    ShaderHandle CreateShaderInternal(const ShaderCreateInfo& info);
    FramebufferHandle CreateFramebufferInternal(const FramebufferCreateInfo& info);
    
    void DestroyMeshInternal(MeshHandle h);
    void DestroyTextureInternal(TextureHandle h);
    void DestroyShaderInternal(ShaderHandle h);
    void DestroyFramebufferInternal(FramebufferHandle h);
};

}