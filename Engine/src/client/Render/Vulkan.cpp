#include "client/Render/RenderAPI.h"
#include "client/Render/VulkanAPI.h"
#include "client/Window.h"
#include "core/Log.h"

#include <SDL3/SDL_vulkan.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <cstddef>
#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <vulkan/vulkan.hpp>

namespace Eng::client {

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    [[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) 
{
    auto* logger = static_cast<Log*>(pUserData);
    if (messageSeverity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        logWarning(logger, "[Vulkan] " << pCallbackData->pMessage);
    }
    return VK_FALSE;
}

VulkanAPI::VulkanAPI() = default;

VulkanAPI::~VulkanAPI() {
    try { Shutdown(); }
    catch (const std::exception& e) { std::fprintf(stderr, "[VulkanAPI] Shutdown threw: %s\n", e.what()); }
    catch (...) { std::fputs("[VulkanAPI] Shutdown threw unknown\n", stderr); }
}

void VulkanAPI::Initialize(int width, int height, Window* window) {
    m_window = window;
    m_windowConfig.windowWidth = width;
    m_windowConfig.windowHeight = height;

    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize volk!");
    }

    m_context = vk::raii::Context();
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();

    volkLoadInstance(*m_instance);
    volkLoadDevice(*m_device);

    createVmaAllocator();
    createSwapChain();
    createImageViews();
    createDepthResources();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    createDescriptorPool();
    createGlobalDescriptors();

    // 初始化 MDI 和全局缓冲
    InitPipelines();
}

void VulkanAPI::Shutdown() {
    if (m_isShutdown) return;
    m_isShutdown = true;

    try {
        if (*m_device) m_device.waitIdle();
    } catch (const std::exception& e) {
        logError(m_logger, "[Vulkan] waitIdle failed: " << e.what());
    }

    // =========================================================================
    // 1. 显式释放所有智能指针，触发 deleter，防止析构时 UAF（底层 Device 和 Allocator 已毁）
    // =========================================================================
    m_skyCubeMesh.reset();
    m_defaultTexturePtr.reset();
    m_pendingFramebuffer.reset();
    
    // 如果你有其他长期持有的智能指针，也必须在这里 reset

    // =========================================================================
    // 2. 清理所有 VMA 资源
    // =========================================================================
    
    // 清理延迟销毁队列
    for (auto& del : m_deletionQueue) {
        if (del.p.vertexBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, del.p.vertexBuffer, del.p.vertexAlloc);
        if (del.p.indexBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, del.p.indexBuffer, del.p.indexAlloc);
        if (del.p.instanceBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, del.p.instanceBuffer, del.p.instanceAlloc);
    }
    m_deletionQueue.clear();

    // 清理 Mesh 资源
    for (auto& [handle, mesh] : m_meshes) {
        if (mesh.vertexBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, mesh.vertexBuffer, mesh.vertexAlloc);
        if (mesh.indexBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, mesh.indexBuffer, mesh.indexAlloc);
        if (mesh.instanceBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, mesh.instanceBuffer, mesh.instanceAlloc);
    }
    m_meshes.clear();

    // 清理按帧 MDI 缓冲
    for (auto& frameBuf : m_frameMDIBuffers) {
        if (frameBuf.vbo != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, frameBuf.vbo, frameBuf.vboAlloc);
        if (frameBuf.ibo != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, frameBuf.ibo, frameBuf.iboAlloc);
        if (frameBuf.indirectBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, frameBuf.indirectBuffer, frameBuf.indirectAlloc);
    }
    m_frameMDIBuffers.clear();

    // 清理 Global UBOs
    for (size_t i = 0; i < m_globalUBOs.size(); ++i) {
        if (m_globalUBOs[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_globalUBOs[i], m_globalUBOAllocs[i]);
        }
    }
    m_globalUBOs.clear();
    m_globalUBOAllocs.clear();
    m_globalUBOMapped.clear();

    // 清理纹理
    for (auto& [handle, tex] : m_textures) {
        if (tex.image != VK_NULL_HANDLE && tex.ownsImage) {
            vmaDestroyImage(m_allocator, tex.image, tex.alloc);
        }
    }
    m_textures.clear();
    m_fbColorHandles.clear();

    // 清理 Framebuffer 附加的 Image
    for (auto& [handle, fb] : m_framebuffers) {
        if (fb.colorImage != VK_NULL_HANDLE) vmaDestroyImage(m_allocator, fb.colorImage, fb.colorAlloc);
        if (fb.hasDepth && fb.depthImage != VK_NULL_HANDLE) vmaDestroyImage(m_allocator, fb.depthImage, fb.depthAlloc);
    }
    m_framebuffers.clear();

    // 清理 Shader 资源（这会销毁 VkPipeline, VkPipelineLayout, VkShaderModule）
    m_shaders.clear();
    
    // 清理 Command 资源
    m_commandBuffers.clear();
    m_commandPool = nullptr;
    
    // 清理 Sync 资源
    m_presentCompleteSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();

    // 清理描述符
    m_globalSets.clear();
    m_descriptorPool = nullptr;
    m_globalSetLayout = nullptr;
    m_textureSetLayout = nullptr;

    // =========================================================================
    // 3. 最后销毁 VMA 分配器
    // =========================================================================
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    
    logInfo(m_logger, "[Vulkan] Shutdown complete");
}

void VulkanAPI::createInstance() {
    vk::ApplicationInfo appInfo{
        .pApplicationName = "Mycraft",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "MycraftEngine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3
    };

    uint32_t sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);
    
    if (enableValidationLayers) {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
    }

    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }

    m_instance = vk::raii::Instance(m_context, createInfo);
}

void VulkanAPI::setupDebugMessenger() {
    if (!enableValidationLayers) return;
    
    vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | 
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | 
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | 
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance);

    vk::DebugUtilsMessengerCreateInfoEXT createInfo{
        .messageSeverity = severityFlags,
        .messageType = messageTypeFlags,
        .pfnUserCallback = debugCallback,
        .pUserData = m_logger.get()
    };
    m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(createInfo);
}

void VulkanAPI::createSurface() {
    VkSurfaceKHR surface;
    if (!SDL_Vulkan_CreateSurface(m_window->GetSDLWindow(), *m_instance, nullptr, &surface)) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }
    m_surface = vk::raii::SurfaceKHR(m_instance, surface);
}

void VulkanAPI::pickPhysicalDevice() {
    std::vector<vk::raii::PhysicalDevice> devices = m_instance.enumeratePhysicalDevices();
    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            m_physicalDevice = device;
            break;
        }
    }
    if (!*m_physicalDevice) {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

bool VulkanAPI::isDeviceSuitable(const vk::raii::PhysicalDevice& pd) {
    auto props = pd.getProperties();
    // 强制要求 Vulkan 1.3
    if (props.apiVersion < VK_API_VERSION_1_3) return false;

    auto features = pd.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
    const auto& vulkan13 = features.get<vk::PhysicalDeviceVulkan13Features>();
    if (!vulkan13.dynamicRendering || !vulkan13.synchronization2) return false;

    // 检查队列族
    auto families = pd.getQueueFamilyProperties();
    bool hasGraphics = false;
    bool hasPresent = false;
    for (uint32_t i = 0; i < families.size(); ++i) {
        if (families[i].queueFlags & vk::QueueFlagBits::eGraphics) hasGraphics = true;
        if (pd.getSurfaceSupportKHR(i, *m_surface)) hasPresent = true;
    }
    return hasGraphics && hasPresent;
}

void VulkanAPI::createLogicalDevice() {
    auto families = m_physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < families.size(); ++i) {
        if ((families[i].queueFlags & vk::QueueFlagBits::eGraphics) && 
            m_physicalDevice.getSurfaceSupportKHR(i, *m_surface)) {
            m_queueIndex = i;
            break;
        }
    }

    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo{
        .queueFamilyIndex = m_queueIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    // =========================================================
    // 1. 基础物理特性 (Vulkan 1.0 级别)
    // =========================================================
    vk::PhysicalDeviceFeatures baseFeatures{};
    
    // 允许一次 API 调用提交多个绘制指令 (MDI 必须)
    // 否则 vkCmdDrawIndexedIndirect 传入 count > 1 时，驱动会直接报错甚至崩溃。
    baseFeatures.multiDrawIndirect = VK_TRUE; 
    
    // 允许在 Shader 中设置各向异性过滤
    // 对于地形方块这种大面积平铺的纹理，如果产生斜角，开启了各向异性过滤能让远处的贴图不那么糊。
    baseFeatures.samplerAnisotropy = VK_TRUE;

    // 允许每个颜色附件单独配置混合状态（未来做后处理/UI独立混合时有用）
    baseFeatures.independentBlend = VK_TRUE;


    // =========================================================
    // 2. Vulkan 1.3 核心特性
    // =========================================================
    vk::PhysicalDeviceVulkan13Features features13{};
    
    // 动态渲染 (Dynamic Rendering)
    // 这是 Vulkan 1.3 最伟大的特性。有了它，我们彻底不需要去写极其繁琐的 VkRenderPass 和 VkFramebuffer。
    // 在 CommandBuffer 里直接调用 vkCmdBeginRendering 传入附件就能画，极大简化了代码。
    features13.dynamicRendering = VK_TRUE;
    
    // 同步 2 (Synchronization 2)
    // 它是 Vulkan 1.3 的现代同步标准。允许我们在 CommandBuffer 中使用更简洁、更安全的屏障（Barriers）。
    // 如果你用了 vk::DependencyInfo 和 pipelineBarrier2，就必须开启它。
    features13.synchronization2 = VK_TRUE;
    
    // 允许在绘制阶段动态改变更复杂的状态（比如深度测试开关、混合常量），无需重建管线。
    // 这里暂时注释，如果你以后想在 Pass 中途动态关掉深度或剔除，可以把它打开并在 Shader 配置里配置。
    // features13.extendedDynamicState = VK_TRUE;


    // =========================================================
    // 3. 组装特性链 (Features Chain)
    // =========================================================
    // 我们需要把 Vulkan 1.3 的特性挂载到基础特性的 pNext 上，然后一起传给 DeviceCreateInfo。
    vk::PhysicalDeviceFeatures2 features2{};
    features2.features = baseFeatures;
    features2.pNext = &features13; 


    // =========================================================
    // 4. 创建设备
    // =========================================================
    vk::DeviceCreateInfo createInfo{
        .pNext = &features2, // 这里必须传 features2 所在的地址，它会把 1.0 和 1.3 的特性一起报给驱动
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data()
    };

    m_device = vk::raii::Device(m_physicalDevice, createInfo);
    m_queue = vk::raii::Queue(m_device, m_queueIndex, 0);
}

void VulkanAPI::createVmaAllocator() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = *m_physicalDevice;
    allocatorInfo.device = *m_device;
    allocatorInfo.instance = *m_instance;

    // 使用 volk 导入 VMA 所需的函数指针
    VmaVulkanFunctions vmaVulkanFunctions{};
    // 注意：第一个参数是已经填充好的 allocatorInfo
    VkResult importResult = vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vmaVulkanFunctions);
    if (importResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to import Vulkan functions from volk for VMA");
    }
    allocatorInfo.pVulkanFunctions = &vmaVulkanFunctions;

    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }
}

void VulkanAPI::createSwapChain() {
    auto caps = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);
    auto formats = m_physicalDevice.getSurfaceFormatsKHR(*m_surface);
    auto presentModes = m_physicalDevice.getSurfacePresentModesKHR(*m_surface);

    m_swapChainSurfaceFormat = chooseSwapSurfaceFormat(formats);
    vk::PresentModeKHR presentMode = chooseSwapPresentMode(presentModes);
    m_swapChainExtent = chooseSwapExtent(caps);

    uint32_t imageCount = chooseSwapMinImageCount(caps);

    vk::SwapchainCreateInfoKHR createInfo{
        .surface = *m_surface,
        .minImageCount = imageCount,
        .imageFormat = m_swapChainSurfaceFormat.format,
        .imageColorSpace = m_swapChainSurfaceFormat.colorSpace,
        .imageExtent = m_swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = caps.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = VK_TRUE
    };

    m_swapChain = vk::raii::SwapchainKHR(m_device, createInfo);
    m_swapChainImages = m_swapChain.getImages();
    m_swapChainFirstUse.assign(m_swapChainImages.size(), true);
}

vk::SurfaceFormatKHR VulkanAPI::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == vk::Format::eB8G8R8A8Unorm && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }
    return formats[0];
}

vk::PresentModeKHR VulkanAPI::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& modes) {
    for (const auto& mode : modes) {
        if (mode == vk::PresentModeKHR::eMailbox) return mode;
    }
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D VulkanAPI::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) return caps.currentExtent;
    int w, h;
    SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
    return {
        std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width),
        std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}

uint32_t VulkanAPI::chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& caps) {
    uint32_t minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && minImageCount > caps.maxImageCount) minImageCount = caps.maxImageCount;
    return minImageCount;
}

void VulkanAPI::createImageViews() {
    m_swapChainImageViews.clear();
    for (auto& image : m_swapChainImages) {
        vk::ImageViewCreateInfo createInfo{
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = m_swapChainSurfaceFormat.format,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
        };
        m_swapChainImageViews.emplace_back(m_device, createInfo);
    }
}

// ==========================================
// 换出链重建逻辑
// ==========================================
void VulkanAPI::cleanupSwapChain() {
    // 销毁深度资源
    m_depthImageView = nullptr;
    m_depthImageMemory = nullptr;
    m_depthImage = nullptr;

    // 销毁 Swapchain ImageViews
    m_swapChainImageViews.clear();

    // 销毁旧的 Swapchain
    m_swapChain = nullptr;
}

void VulkanAPI::recreateSwapChain() {
    // 如果窗口被最小化了（宽高为0），一直等待直到窗口恢复
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &width, &height);
    while (width == 0 || height == 0) {
        SDL_WaitEvent(nullptr);
        SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &width, &height);
    }

    // 等待设备空闲，防止销毁正在被使用的资源
    m_device.waitIdle();

    // 1. 清理旧资源
    cleanupSwapChain();

    // 2. 重新创建
    createSwapChain();
    createImageViews();
    createDepthResources();
    
    // 重建同步对象（因为图片数量变了）
    createSyncObjects(); // 确保这个函数里清空了旧的，重新 emplace
    ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(m_swapChainImages.size()));

    // 记录窗口已经不处于 Resized 状态
    m_framebufferResized = false;
}

void VulkanAPI::createDepthResources() {
    vk::Format depthFormat = findDepthFormat();
    auto [image, memory] = createImage(m_swapChainExtent.width, m_swapChainExtent.height, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_depthImage = std::move(image);
    m_depthImageMemory = std::move(memory);
    m_depthImageView = createImageView(*m_depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
}

vk::Format VulkanAPI::findDepthFormat() {
    return findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );
}

vk::Format VulkanAPI::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
    for (vk::Format format : candidates) {
        vk::FormatProperties props = m_physicalDevice.getFormatProperties(format);
        if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) return format;
        if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) return format;
    }
    throw std::runtime_error("Failed to find supported format!");
}

void VulkanAPI::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = m_queueIndex
    };
    m_commandPool = vk::raii::CommandPool(m_device, poolInfo);
}

void VulkanAPI::createCommandBuffers() {
    m_commandBuffers.clear();
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *m_commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT
    };
    m_commandBuffers = m_device.allocateCommandBuffers(allocInfo);
}

void VulkanAPI::createSyncObjects() {
    m_presentCompleteSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_inFlightFences.emplace_back(m_device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
        m_presentCompleteSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo()); // 按帧分配
    }
    for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
        m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo()); // 按 Image 数量分配
    }
}

void VulkanAPI::createDescriptorPool() {
    std::array<vk::DescriptorPoolSize, 2> poolSizes{{
        { vk::DescriptorType::eUniformBuffer, 100 },
        { vk::DescriptorType::eCombinedImageSampler, 100 }
    }};
    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 100,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };
    m_descriptorPool = vk::raii::DescriptorPool(m_device, poolInfo);
}

void VulkanAPI::createGlobalDescriptors() {
    vk::DescriptorSetLayoutBinding uboBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eAllGraphics
    };
    vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = 1, .pBindings = &uboBinding };
    m_globalSetLayout = vk::raii::DescriptorSetLayout(m_device, layoutInfo);

    vk::DescriptorSetLayoutBinding samplerBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    vk::DescriptorSetLayoutCreateInfo texLayoutInfo{ .bindingCount = 1, .pBindings = &samplerBinding };
    m_textureSetLayout = vk::raii::DescriptorSetLayout(m_device, texLayoutInfo);

    m_globalUBOs.resize(MAX_FRAMES_IN_FLIGHT);
    m_globalUBOAllocs.resize(MAX_FRAMES_IN_FLIGHT);
    m_globalUBOMapped.resize(MAX_FRAMES_IN_FLIGHT);
    m_globalSets.clear();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(GlobalState);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocResult;
        vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &m_globalUBOs[i], &m_globalUBOAllocs[i], &allocResult);
        m_globalUBOMapped[i] = allocResult.pMappedData;

        // 分配 DescriptorSet
        vk::DescriptorSetAllocateInfo allocSetInfo{
            .descriptorPool = *m_descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &*m_globalSetLayout
        };
        auto sets = m_device.allocateDescriptorSets(allocSetInfo);
        m_globalSets.push_back(std::move(sets[0]));

        // 写入 DescriptorSet
        vk::DescriptorBufferInfo dbi{ 
            .buffer = vk::Buffer(m_globalUBOs[i]), 
            .offset = 0, 
            .range = sizeof(GlobalState) 
        };
        
        vk::WriteDescriptorSet write{
            .dstSet = *m_globalSets[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &dbi
        };
        m_device.updateDescriptorSets(write, {});
    }
}


// ==========================================
// MDI 终极接口 (基于 Vulkan 1.3)
// ==========================================
void VulkanAPI::InitPipelines() {
    // ★ MDI 缓冲不需要在这里预分配。
    // UploadGlobalGeometry 和 UploadIndirectCommands 会根据实际需求自动扩容。
    // 这里只需要保证按帧的数组大小正确即可。
    m_frameMDIBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    // 2. 预创建管线（使用 Dynamic Rendering）
    m_pipelines.clear();
    
    // 注意：这里的 Pipeline 是预编译的骨架，实际项目可能会在 CreateShader 里动态创建
    // 为了保证不漏，我们这里只做初始化占位，真正的 Pipeline 在 Shader 创建时内部完成
}

void VulkanAPI::BindPipeline([[maybe_unused]]rhi::PipelineType type) {
    // Vulkan 在绘制时直接绑定 m_pipelines 里对应的 Pipeline
}

void VulkanAPI::UploadGlobalGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return;

    VkDeviceSize reqVboSize = vertices.size() * sizeof(Vertex);
    VkDeviceSize reqIboSize = indices.size() * sizeof(uint32_t);

    // ★ 遍历所有的飞行帧，让 Frame 0 和 Frame 1 都更新
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto& frameBuf = m_frameMDIBuffers[i];

        auto ensureBuffer = [&](VkBuffer& buffer, VmaAllocation& alloc, void*& mapped, 
                                VkDeviceSize& currentSize, VkDeviceSize reqSize, 
                                VkBufferUsageFlags usage, const void* srcData) {
            if (reqSize > currentSize) {
                m_device.waitIdle();
                if (buffer != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(m_allocator, buffer, alloc);
                }
                VkDeviceSize newSize = std::max(reqSize * 2, VkDeviceSize(512 * 1024 * 1024));
                VkBufferCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                info.pNext = nullptr;
                info.size  = newSize;
                info.usage = usage;
                VmaAllocationCreateInfo allocInfo{};
                allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
                allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
                VmaAllocationInfo allocResult;
                vmaCreateBuffer(m_allocator, &info, &allocInfo, &buffer, &alloc, &allocResult);
                mapped = allocResult.pMappedData;
                currentSize = newSize;
            }
            memcpy(mapped, srcData, reqSize);
            vmaFlushAllocation(m_allocator, alloc, 0, reqSize);
        };

        ensureBuffer(frameBuf.vbo, frameBuf.vboAlloc, frameBuf.vboMapped, 
                     frameBuf.vboSize, reqVboSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data());
                     
        ensureBuffer(frameBuf.ibo, frameBuf.iboAlloc, frameBuf.iboMapped, 
                     frameBuf.iboSize, reqIboSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data());
    }
}

void VulkanAPI::UploadIndirectCommands(const std::vector<rhi::DrawIndexedIndirectCommand>& commands) {
    if (commands.empty()) return;
    
    // ★ 获取当前帧专属的 Buffer
    auto& frameBuf = m_frameMDIBuffers[m_frameIndex];
    VkDeviceSize reqSize = commands.size() * sizeof(rhi::DrawIndexedIndirectCommand);

    if (commands.size() > frameBuf.indirectCapacity) {
        m_device.waitIdle();
        
        if (frameBuf.indirectBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, frameBuf.indirectBuffer, frameBuf.indirectAlloc);
        }
        
        // 预留 1.5 倍容量，且给个下限防止初始容量过小
        frameBuf.indirectCapacity = std::max(static_cast<uint32_t>(commands.size() * 1.5f), 100u);
        VkDeviceSize newSize = frameBuf.indirectCapacity * sizeof(rhi::DrawIndexedIndirectCommand);

        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.pNext = nullptr;
        info.size = newSize;
        info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        
        VmaAllocationInfo allocResult;
        // ★ 注意这里是 &frameBuf.indirectBuffer，不是 &FrameMDIBuffers::indirectBuffer
        vmaCreateBuffer(m_allocator, &info, &allocInfo, &frameBuf.indirectBuffer, &frameBuf.indirectAlloc, &allocResult);
        frameBuf.indirectMapped = allocResult.pMappedData;
    }

    // 直接写入持久映射的内存并刷新
    memcpy(frameBuf.indirectMapped, commands.data(), reqSize);
    vmaFlushAllocation(m_allocator, frameBuf.indirectAlloc, 0, reqSize);
}

// 资源管理 (Shader / Mesh / Texture / Framebuffer)
ShaderHandle VulkanAPI::CreateShaderInternal(const ShaderCreateInfo& info) {
    auto vertCode = readFile(info.vertPath);
    auto fragCode = readFile(info.fragPath);

    ShaderInternal internal;
    internal.vertModule = createShaderModule(vertCode);
    internal.fragModule = createShaderModule(fragCode);

    // ==========================================
    // 1. Pipeline Layout (Global UBO + Texture + PushConstant?)
    // ==========================================
    vk::PipelineLayoutCreateInfo layoutInfo{};
    std::array<vk::DescriptorSetLayout, 2> setLayouts = { *m_globalSetLayout, *m_textureSetLayout };
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();

    vk::PushConstantRange pushRange{};
    if (info.perObjectMode == rhi::PerObjectMode::Default) {
        pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushRange.offset     = 0;
        pushRange.size       = sizeof(rhi::PerObjectData);
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges    = &pushRange;
    }
    internal.layout = vk::raii::PipelineLayout(m_device, layoutInfo);

    // ==========================================
    // 2. Shader Stages
    // ==========================================
    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        { .stage = vk::ShaderStageFlagBits::eVertex,   .module = *internal.vertModule, .pName = "main" },
        { .stage = vk::ShaderStageFlagBits::eFragment, .module = *internal.fragModule, .pName = "main" }
    };

    // ==========================================
    // 3. Vertex Input
    // ==========================================
    vk::VertexInputBindingDescription bindingDesc{ 0, sizeof(Vertex), vk::VertexInputRate::eVertex };
    std::vector<vk::VertexInputAttributeDescription> attrDescs;
    if (info.vertexLayout == rhi::VertexLayout::Skybox) {
        attrDescs = { { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position) } };
    } else {
        attrDescs = {
            { 0, 0, vk::Format::eR32G32B32Sfloat,    offsetof(Vertex, position) },
            { 1, 0, vk::Format::eR32G32B32Sfloat,    offsetof(Vertex, normal)   },
            { 2, 0, vk::Format::eR32G32Sfloat,       offsetof(Vertex, uv)       },
            { 3, 0, vk::Format::eR32Sfloat,          offsetof(Vertex, ao)       },
            { 4, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, light)    }
        };
    }
    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions    = attrDescs.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = VK_FALSE
    };

    vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1, .pViewports = nullptr, .scissorCount = 1, .pScissors = nullptr
    };

    // ==========================================
    // 4. 根据 PipelineType 推导状态
    // ==========================================
    bool isSkybox      = (info.pipelineType == rhi::PipelineType::Skybox);
    bool isUI          = (info.pipelineType == rhi::PipelineType::UI);
    bool isHand        = (info.pipelineType == rhi::PipelineType::Hand);
    bool isTransparent = (info.pipelineType == rhi::PipelineType::Transparent);

    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = vk::PolygonMode::eFill,
        .cullMode                = (isSkybox || isUI) ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
        .frontFace               = vk::FrontFace::eCounterClockwise,  // ★ 抵消 Y 轴翻转
        .depthBiasEnable         = VK_FALSE,
        .lineWidth               = 1.0f
    };

    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable  = VK_FALSE
    };

    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        // Skybox 不要关闭深度测试，必须开启以比对地形深度；但关闭深度写入
        .depthTestEnable     = (isUI || isHand) ? vk::False : vk::True, 
        .depthWriteEnable    = (isUI || isHand || isTransparent || isSkybox) ? vk::False : vk::True,
        .depthCompareOp      = vk::CompareOp::eLessOrEqual,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable   = VK_FALSE,
        .minDepthBounds      = 0.0f,
        .maxDepthBounds      = 1.0f
    };

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable         = (isTransparent || isUI) ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp        = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp        = vk::BlendOp::eAdd,
        .colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable   = VK_FALSE,
        .logicOp         = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments    = &colorBlendAttachment
    };

    std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates    = dynamicStates.data()
    };

    vk::PipelineRenderingCreateInfo renderingInfo{
        .colorAttachmentCount   = 1,
        .pColorAttachmentFormats = &m_swapChainSurfaceFormat.format,
        .depthAttachmentFormat   = findDepthFormat()
    };

    vk::GraphicsPipelineCreateInfo pipelineInfo{
        .pNext               = &renderingInfo,
        .stageCount          = 2,
        .pStages             = shaderStages,
        .pVertexInputState   = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState      = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState   = &multisampling,
        .pDepthStencilState  = &depthStencil,
        .pColorBlendState    = &colorBlending,
        .pDynamicState       = &dynamicState,
        .layout              = *internal.layout,
        .renderPass          = nullptr,
        .subpass             = 0
    };

    internal.pipeline      = vk::raii::Pipeline(m_device, nullptr, pipelineInfo);
    internal.perObjectMode = info.perObjectMode;   // ★ 缓存供 DrawMesh 判断

    ShaderHandle handle = m_nextShader++;
    m_shaders[handle]   = std::move(internal);
    return handle;
}

void VulkanAPI::DestroyShaderInternal(ShaderHandle h) {
    auto it = m_shaders.find(h);
    if (it == m_shaders.end()) return;
    
    // vk::raii 对象会在 erase 时自动调用底层的 vkDestroyPipeline 等
    // 但是为了安全起见，我们等待显卡空闲
    m_device.waitIdle();
    m_shaders.erase(it);
}

MeshHandle VulkanAPI::CreateMeshInternal(const MeshData& data) {
    MeshInternal internal;

    auto createAndUploadBuffer = [&](VkDeviceSize size, const void* srcData, VkBufferUsageFlags usage, VkBuffer& outBuf, VmaAllocation& outAlloc) {
        if (size == 0) return;
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateBuffer(m_allocator, &info, &allocInfo, &outBuf, &outAlloc, nullptr);

        // Staging Buffer
        VkBuffer stagingBuf; VmaAllocation stagingAlloc;
        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = size;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        vmaCreateBuffer(m_allocator, &stagingInfo, &stagingAllocInfo, &stagingBuf, &stagingAlloc, nullptr);

        void* mapped;
        vmaMapMemory(m_allocator, stagingAlloc, &mapped);
        memcpy(mapped, srcData, size);
        vmaUnmapMemory(m_allocator, stagingAlloc);

        auto cmd = beginSingleTimeCommands();
        vk::BufferCopy copyRegion{ 0, 0, size };
        cmd.copyBuffer(stagingBuf, outBuf, copyRegion);
        endSingleTimeCommands(std::move(cmd));
        
        vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);
    };

    createAndUploadBuffer(data.vertices.size() * sizeof(Vertex), data.vertices.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, internal.vertexBuffer, internal.vertexAlloc);
    createAndUploadBuffer(data.indices.size() * sizeof(uint32_t), data.indices.data(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, internal.indexBuffer, internal.indexAlloc);
    internal.indexCount = data.indices.size();

    // Instance Buffer
    internal.instanceCapacity = 100; 
    VkBufferCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    instanceInfo.size = internal.instanceCapacity * sizeof(glm::mat4);
    instanceInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VmaAllocationCreateInfo instanceAllocInfo{};
    instanceAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    vmaCreateBuffer(m_allocator, &instanceInfo, &instanceAllocInfo, &internal.instanceBuffer, &internal.instanceAlloc, nullptr);

    MeshHandle handle = m_nextMesh++;
    m_meshes[handle] = internal;
    return handle;
}

MeshPtr VulkanAPI::CreateMesh(const MeshData& data) {
    MeshHandle h = CreateMeshInternal(data);
    return MeshPtr(new Mesh(h), [this](Mesh* m) { this->DestroyMeshInternal(m->GetHandle()); delete m; });
}

void VulkanAPI::DestroyMeshInternal(MeshHandle h) {
    auto it = m_meshes.find(h);
    if (it == m_meshes.end()) return;
    
    // 延迟销毁
    PendingDestroy pd;
    pd.vertexBuffer = it->second.vertexBuffer;
    pd.vertexAlloc = it->second.vertexAlloc;
    pd.indexBuffer = it->second.indexBuffer;
    pd.indexAlloc = it->second.indexAlloc;
    pd.instanceBuffer = it->second.instanceBuffer;
    pd.instanceAlloc = it->second.instanceAlloc;
    
    m_deletionQueue.push_back({ m_frameCounter + MAX_FRAMES_IN_FLIGHT, pd });
    m_meshes.erase(it);
}

TextureHandle VulkanAPI::CreateTextureInternal(const uint8_t* rgba, int w, int h) {
    TextureInternal internal;
    VkDeviceSize imageSize = static_cast<size_t>(w) * h * 4;

    VkBuffer stagingBuf; VmaAllocation stagingAlloc;
    
    // 使用 C++ 结构体
    vk::BufferCreateInfo stagingInfo{
        .size = static_cast<vk::DeviceSize>(imageSize),
        .usage = vk::BufferUsageFlagBits::eTransferSrc,
        .sharingMode = vk::SharingMode::eExclusive
    };
    
    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    
    // reinterpret_cast 传给 VMA
    vmaCreateBuffer(m_allocator, reinterpret_cast<const VkBufferCreateInfo*>(&stagingInfo), 
                    &stagingAllocInfo, &stagingBuf, &stagingAlloc, nullptr);

    void* mapped;
    vmaMapMemory(m_allocator, stagingAlloc, &mapped);
    memcpy(mapped, rgba, imageSize);
    vmaUnmapMemory(m_allocator, stagingAlloc);

    // ★ 使用 C++ 结构体
    vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .extent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    // ★ reinterpret_cast 传给 VMA
    vmaCreateImage(m_allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), 
                   &allocInfo, &internal.image, &internal.alloc, nullptr);

    auto cmd = beginSingleTimeCommands();
    transition_image_layout(cmd, internal.image, 
        vk::ImageLayout::eUndefined, 
        vk::ImageLayout::eTransferDstOptimal,
        vk::AccessFlagBits2::eNone, 
        vk::AccessFlagBits2::eTransferWrite, 
        vk::PipelineStageFlagBits2::eTopOfPipe, 
        vk::PipelineStageFlagBits2::eTransfer, 
        vk::ImageAspectFlagBits::eColor
    );

    // ★ 顺便把 BufferImageCopy 也换成指定初始化，消除位置传参风险
    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { 
            .aspectMask = vk::ImageAspectFlagBits::eColor, 
            .mipLevel = 0, 
            .baseArrayLayer = 0, 
            .layerCount = 1 
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 }
    };
    cmd.copyBufferToImage(stagingBuf, internal.image, vk::ImageLayout::eTransferDstOptimal, region);
    
    transition_image_layout(cmd, internal.image, 
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::AccessFlagBits2::eTransferWrite, 
        vk::AccessFlagBits2::eShaderRead, 
        vk::PipelineStageFlagBits2::eTransfer, 
        vk::PipelineStageFlagBits2::eFragmentShader, 
        vk::ImageAspectFlagBits::eColor
    );
    endSingleTimeCommands(std::move(cmd));

    vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);

    internal.view = createImageView(internal.image, vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor);

    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eNearest;
    samplerInfo.minFilter = vk::Filter::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    internal.sampler = vk::raii::Sampler(m_device, samplerInfo);

    vk::DescriptorSetAllocateInfo allocSetInfo{
        .descriptorPool = *m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &*m_textureSetLayout
    };
    auto sets = m_device.allocateDescriptorSets(allocSetInfo);
    internal.set = std::move(sets[0]);

    vk::DescriptorImageInfo imageDesc{
        .sampler = *internal.sampler,
        .imageView = *internal.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    
    vk::WriteDescriptorSet write{
        .dstSet = *internal.set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &imageDesc
    };
    m_device.updateDescriptorSets(write, {});

    TextureHandle handle = m_nextTexture++;
    m_textures[handle] = std::move(internal);
    return handle;
}

TexturePtr VulkanAPI::CreateTexture(const uint8_t* rgba, int w, int h) {
    TextureHandle th = CreateTextureInternal(rgba, w, h);
    return TexturePtr(new Texture(th), [this](Texture* t) { this->DestroyTextureInternal(t->GetHandle()); delete t; });
}

void VulkanAPI::DestroyTextureInternal(TextureHandle h) {
    auto it = m_textures.find(h);
    if (it != m_textures.end()) {
        if (it->second.image != VK_NULL_HANDLE && it->second.ownsImage) {
            m_device.waitIdle();
            vmaDestroyImage(m_allocator, it->second.image, it->second.alloc);
        }
        m_textures.erase(it);
    }
}

FramebufferPtr VulkanAPI::CreateFramebuffer(const FramebufferCreateInfo& info) {
    FramebufferInternal fb;
    fb.width = info.width;
    fb.height = info.height;
    fb.hasDepth = info.hasDepth;

    // 1. 创建 Color Image
    vk::ImageCreateInfo colorImageInfo{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .extent = { static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height), 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo colorAllocInfo{};
    colorAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateImage(m_allocator, reinterpret_cast<const VkImageCreateInfo*>(&colorImageInfo), 
                   &colorAllocInfo, &fb.colorImage, &fb.colorAlloc, nullptr);

    fb.colorView = createImageView(fb.colorImage, vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor);

    // 2. 创建 Depth Image
    if (info.hasDepth) {
        vk::ImageCreateInfo depthImageInfo{
            .imageType = vk::ImageType::e2D,
            .format = findDepthFormat(),
            .extent = { static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height), 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined
        };

        VmaAllocationCreateInfo depthAllocInfo{};
        depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateImage(m_allocator, reinterpret_cast<const VkImageCreateInfo*>(&depthImageInfo), 
                       &depthAllocInfo, &fb.depthImage, &fb.depthAlloc, nullptr);

        fb.depthView = createImageView(fb.depthImage, findDepthFormat(), vk::ImageAspectFlagBits::eDepth);
    }

    // 3. 注册颜色纹理供采样
    TextureInternal texInt;
    texInt.image = fb.colorImage;
    texInt.alloc = fb.colorAlloc;
    
    // 剥离所有权！颜色图的生命周期由 Framebuffer 全权负责
    texInt.ownsImage = false; 
    
    vk::ImageViewCreateInfo viewInfo{
        .image = fb.colorImage,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .subresourceRange = { .aspectMask=vk::ImageAspectFlagBits::eColor, .baseMipLevel=0, .levelCount=1, .baseArrayLayer=0, .layerCount=1 }
    };
    texInt.view = vk::raii::ImageView(m_device, viewInfo);
    
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    texInt.sampler = vk::raii::Sampler(m_device, samplerInfo);
    
    vk::DescriptorSetAllocateInfo allocSetInfo{
        .descriptorPool = *m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &*m_textureSetLayout
    };
    texInt.set = std::move(m_device.allocateDescriptorSets(allocSetInfo)[0]);
    
    vk::DescriptorImageInfo imageDesc{
        .sampler = *texInt.sampler,
        .imageView = *texInt.view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    
    vk::WriteDescriptorSet write{
        .dstSet = *texInt.set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &imageDesc
    };
    m_device.updateDescriptorSets(write, {});
    
    fb.colorHandle = m_nextTexture++;
    m_textures[fb.colorHandle] = std::move(texInt);

    // 4. 存入 Map (使用 emplace 解决不可拷贝问题)
    FramebufferHandle h = m_nextFramebuffer++;
    m_fbColorHandles[h] = fb.colorHandle;
    
    m_framebuffers.emplace(h, std::move(fb));

    // 5. 封装并返回
    TexturePtr colorTexturePtr = TexturePtr(new Texture(m_fbColorHandles[h]), [this](Texture* t) { 
        this->DestroyTextureInternal(t->GetHandle()); 
        delete t; 
    });
    
    return FramebufferPtr(new Framebuffer(h, colorTexturePtr, info.width, info.height), [this](Framebuffer* f) { 
        auto it = m_framebuffers.find(f->GetHandle());
        if (it != m_framebuffers.end()) {
            m_device.waitIdle();
            // 只有 Framebuffer 在这里销毁 Image
            vmaDestroyImage(m_allocator, it->second.colorImage, it->second.colorAlloc);
            if (it->second.hasDepth) {
                vmaDestroyImage(m_allocator, it->second.depthImage, it->second.depthAlloc);
            }
            
            // 清理映射表，防止句柄泄漏
            m_fbColorHandles.erase(f->GetHandle());
            m_framebuffers.erase(it);
        }
        delete f; 
    });
}

void VulkanAPI::DestroyFramebufferInternal(FramebufferHandle h) {
    auto it = m_framebuffers.find(h);
    if (it == m_framebuffers.end()) return;

    m_device.waitIdle();
    vmaDestroyImage(m_allocator, it->second.colorImage, it->second.colorAlloc);
    if (it->second.hasDepth) {
        vmaDestroyImage(m_allocator, it->second.depthImage, it->second.depthAlloc);
    }
    m_framebuffers.erase(it);
    
    auto texIt = m_fbColorHandles.find(h);
    if (texIt != m_fbColorHandles.end()) {
        m_fbColorHandles.erase(texIt);
    }
}

void VulkanAPI::BindFramebuffer(const FramebufferPtr& fb) { m_pendingFramebuffer = fb; }
void VulkanAPI::UnbindFramebuffer() { m_pendingFramebuffer = nullptr; }

// ==========================================
// 帧控制与渲染主循环
// ==========================================
void VulkanAPI::BeginFrame() {
    m_frameStarted = false; // ★ 确保初始状态

    (void)m_device.waitForFences({ *m_inFlightFences[m_frameIndex] }, VK_TRUE, UINT64_MAX);

    // 使用 RAII 正确的方式获取图片
    try {
        auto resultValue = m_swapChain.acquireNextImage(UINT64_MAX, *m_presentCompleteSemaphores[m_frameIndex], nullptr);
        
        if (resultValue.result == vk::Result::eSuboptimalKHR) {
            // 窗口尺寸变了，但这一帧还可以继续画，可以标记一下等画完再重建
            m_framebufferResized = true; 
        } else if (resultValue.result == vk::Result::eErrorOutOfDateKHR) {
            recreateSwapChain();
            return;
        }
        
        m_imageIndex = resultValue.value;
    } catch (const vk::OutOfDateKHRError&) {
        // RAII 遇到 OutOfDate 会直接抛异常
        recreateSwapChain();
        return;
    }

    m_device.resetFences({ *m_inFlightFences[m_frameIndex] });
    
    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.reset();
    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    
    // ============ 处理延迟销毁 ============
    for (auto it = m_deletionQueue.begin(); it != m_deletionQueue.end(); ) {
        if (it->retireFrame <= m_frameCounter) {
            vmaDestroyBuffer(m_allocator, it->p.vertexBuffer, it->p.vertexAlloc);
            vmaDestroyBuffer(m_allocator, it->p.indexBuffer, it->p.indexAlloc);
            vmaDestroyBuffer(m_allocator, it->p.instanceBuffer, it->p.instanceAlloc);
            it = m_deletionQueue.erase(it);
        } else ++it;
    }
    m_frameCounter++;

    // ============ 图像布局转换 ============
    vk::Image targetImage = m_pendingFramebuffer
        ? vk::Image(m_framebuffers[m_pendingFramebuffer->GetHandle()].colorImage)
        : m_swapChainImages[m_imageIndex];
    vk::ImageLayout layout = vk::ImageLayout::eColorAttachmentOptimal;

    transition_image_layout(cmd, targetImage, 
        vk::ImageLayout::eUndefined, layout, 
        vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eColorAttachmentWrite, 
        vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eColorAttachmentOutput, 
        vk::ImageAspectFlagBits::eColor);

    vk::Image depthTargetImage = m_pendingFramebuffer 
        ? vk::Image(m_framebuffers[m_pendingFramebuffer->GetHandle()].depthImage) 
        : *m_depthImage;
    if (depthTargetImage != VK_NULL_HANDLE) {
        transition_image_layout(cmd, depthTargetImage, 
            vk::ImageLayout::eUndefined, 
            vk::ImageLayout::eDepthAttachmentOptimal, 
            vk::AccessFlagBits2::eNone, 
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite, 
            vk::PipelineStageFlagBits2::eTopOfPipe, 
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, 
            vk::ImageAspectFlagBits::eDepth);
    }

    vk::RenderingAttachmentInfo colorAttachment{};
    colorAttachment.imageView = m_pendingFramebuffer ? *m_framebuffers[m_pendingFramebuffer->GetHandle()].colorView : *m_swapChainImageViews[m_imageIndex];
    colorAttachment.imageLayout = layout;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.clearValue.color = vk::ClearColorValue(std::array<float, 4>{ m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] });

    vk::RenderingAttachmentInfo depthAttachment{};
    depthAttachment.imageView = m_pendingFramebuffer ? *m_framebuffers[m_pendingFramebuffer->GetHandle()].depthView : *m_depthImageView;
    depthAttachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

    vk::RenderingInfo info{};
    info.renderArea = vk::Rect2D({0, 0}, m_swapChainExtent);
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colorAttachment;
    info.pDepthAttachment = &depthAttachment;

    cmd.beginRendering(info);

    cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, (float)m_swapChainExtent.width, (float)m_swapChainExtent.height, 0.0f, 1.0f));
    cmd.setScissor(0, vk::Rect2D({0, 0}, m_swapChainExtent));

    // ★ 添加 Flush 防止镜头闪烁
    memcpy(m_globalUBOMapped[m_frameIndex], &m_globalState, sizeof(GlobalState));
    vmaFlushAllocation(m_allocator, m_globalUBOAllocs[m_frameIndex], 0, sizeof(GlobalState));

    m_frameStarted = true; // ★ 标记录制成功
}

void VulkanAPI::EndFrame() {
    if (!m_frameStarted) return; // ★ 如果这一帧被跳过，直接返回，防止死锁
    m_frameStarted = false;

    auto& cmd = m_commandBuffers[m_frameIndex];

    cmd.endRendering();

    ImGui::Render(); 
    ImGuiRenderDrawData(); 

    // ★ 正确区分 Swapchain 和 Framebuffer 的布局转换
    if (!m_pendingFramebuffer) {
        transition_image_layout(cmd, vk::Image(m_swapChainImages[m_imageIndex]), 
            vk::ImageLayout::eColorAttachmentOptimal, 
            vk::ImageLayout::ePresentSrcKHR, 
            vk::AccessFlagBits2::eColorAttachmentWrite, 
            vk::AccessFlagBits2::eNone, 
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, 
            vk::PipelineStageFlagBits2::eBottomOfPipe, 
            vk::ImageAspectFlagBits::eColor);
    } else {
        transition_image_layout(cmd, vk::Image(m_framebuffers[m_pendingFramebuffer->GetHandle()].colorImage), 
            vk::ImageLayout::eColorAttachmentOptimal, 
            vk::ImageLayout::eShaderReadOnlyOptimal, 
            vk::AccessFlagBits2::eColorAttachmentWrite, 
            vk::AccessFlagBits2::eShaderRead, 
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, 
            vk::PipelineStageFlagBits2::eFragmentShader, 
            vk::ImageAspectFlagBits::eColor);
    }
    
    cmd.end();

    vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = 1, .pWaitSemaphores = &*m_presentCompleteSemaphores[m_frameIndex], .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1, .pCommandBuffers = &*cmd,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &*m_renderFinishedSemaphores[m_imageIndex]
    };

    m_queue.submit(submitInfo, *m_inFlightFences[m_frameIndex]);

    vk::PresentInfoKHR presentInfo{
        .waitSemaphoreCount = 1, .pWaitSemaphores = &*m_renderFinishedSemaphores[m_imageIndex],
        .swapchainCount = 1, .pSwapchains = &*m_swapChain, .pImageIndices = &m_imageIndex
    };
    
    try {
        vk::Result result = m_queue.presentKHR(presentInfo);
        if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || m_framebufferResized) {
            m_framebufferResized = false;
            recreateSwapChain();
        }
    } catch (const vk::OutOfDateKHRError&) {
        m_framebufferResized = false;
        recreateSwapChain();
    } catch (const std::exception& e) {
        logError(m_logger, "[Vulkan] presentKHR threw: " << e.what());
    }
    
    m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ==========================================
// 辅助工具（图像转换与命令缓冲）
// ==========================================
vk::raii::CommandBuffer VulkanAPI::beginSingleTimeCommands() {
    vk::CommandBufferAllocateInfo allocInfo{ .commandPool = *m_commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 };
    vk::raii::CommandBuffer cmd = std::move(m_device.allocateCommandBuffers(allocInfo).front());
    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    return cmd;
}

void VulkanAPI::endSingleTimeCommands(vk::raii::CommandBuffer&& cmd) {
    cmd.end();
    vk::SubmitInfo submitInfo{ .commandBufferCount = 1, .pCommandBuffers = &*cmd };
    m_queue.submit(submitInfo, nullptr);
    m_queue.waitIdle();
}

void VulkanAPI::transition_image_layout(const vk::raii::CommandBuffer& cmd, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage, vk::ImageAspectFlags aspect) {
    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = srcStage, .srcAccessMask = srcAccess,
        .dstStageMask = dstStage, .dstAccessMask = dstAccess,
        .oldLayout = oldLayout, .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = { aspect, 0, 1, 0, 1 }
    };
    vk::DependencyInfo depInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier };
    cmd.pipelineBarrier2(depInfo);
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory> VulkanAPI::createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties) {
    vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D, .format = format,
        .extent = { width, height, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1, .tiling = tiling, .usage = usage, .sharingMode = vk::SharingMode::eExclusive
    };
    vk::raii::Image image = vk::raii::Image(m_device, imageInfo);
    vk::MemoryRequirements memReqs = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memReqs.size,
        .memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties)
    };
    vk::raii::DeviceMemory memory = vk::raii::DeviceMemory(m_device, allocInfo);
    image.bindMemory(*memory, 0);
    return { std::move(image), std::move(memory) };
}

vk::raii::ImageView VulkanAPI::createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo{
        .image = image, .viewType = vk::ImageViewType::e2D, .format = format,
        .subresourceRange = { aspectFlags, 0, 1, 0, 1 }
    };
    return vk::raii::ImageView(m_device, viewInfo);
}

uint32_t VulkanAPI::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = m_physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) return i;
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

std::vector<char> VulkanAPI::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open file: " + filename);
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
}

vk::raii::ShaderModule VulkanAPI::createShaderModule(const std::vector<char>& code) const {
    vk::ShaderModuleCreateInfo createInfo{ .codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t*>(code.data()) };
    return vk::raii::ShaderModule(m_device, createInfo);
}

void VulkanAPI::createSkyboxMesh() {
    // 1x1x1 的立方体，36 个顶点（不使用索引）
    const float s = 1.0f;
    float verts[] = {
         s,-s,-s,  s,-s, s,  s, s, s,   s, s, s,  s, s,-s,  s,-s,-s,
        -s,-s, s, -s,-s,-s, -s, s,-s,  -s, s,-s, -s, s, s, -s,-s, s,
        -s, s,-s,  s, s,-s,  s, s, s,   s, s, s, -s, s, s, -s, s,-s,
        -s,-s, s,  s,-s, s,  s,-s,-s,   s,-s,-s, -s,-s,-s, -s,-s, s,
        -s,-s, s,  s,-s, s,  s, s, s,   s, s, s, -s, s, s, -s,-s, s,
         s,-s,-s, -s,-s,-s, -s, s,-s,  -s, s,-s,  s, s,-s,  s,-s,-s,
    };
    
    std::vector<Vertex> v(36);
    for (size_t i = 0; i < 36; ++i) {
        v[i].position = { verts[i*3], verts[i*3+1], verts[i*3+2] };
        v[i].normal   = { 0, 0, 0 };
        v[i].uv       = { 0, 0 };
        v[i].ao       = 1.0f;
        v[i].light    = glm::vec4(1.0f);
    }
    
    MeshData md{ v, {} };
    m_skyCubeMesh = CreateMesh(md);
}

void VulkanAPI::SetViewport(int x, int y, int w, int h) {
    m_commandBuffers[m_frameIndex].setViewport(0, vk::Viewport(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f));
    m_commandBuffers[m_frameIndex].setScissor(0, vk::Rect2D({x, y}, {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}));
}

void VulkanAPI::SetClearColor(float r, float g, float b, float a) {
    m_clearColor[0] = r; m_clearColor[1] = g; m_clearColor[2] = b; m_clearColor[3] = a;
}

void VulkanAPI::Clear(rhi::ClearFlag flag) {
    m_clearPendingFlags = static_cast<uint32_t>(flag);
}

void VulkanAPI::SetGlobalState(const GlobalState& state) {
    m_globalState = state;

    // Vulkan 后端自动翻转 Y 轴，并适配 Z 范围 [0, 1]
    // 业务层无需关心底层 API 差异，继续用 OpenGL 的标准矩阵传进来即可
    m_globalState.projection[1][1] *= -1.0f; 
    
    glm::mat4 zRemap(1.0f);
    zRemap[2][2] = 0.5f;
    zRemap[3][2] = 0.5f;
    m_globalState.projection = zRemap * m_globalState.projection;
}

void VulkanAPI::DrawMesh(const MeshPtr& mesh, const ShaderPtr& shader,
                         const Material& material, const rhi::PerObjectData& data) {
    if (!mesh || !shader) return;
    auto meshIt   = m_meshes.find(mesh->GetHandle());
    auto shaderIt = m_shaders.find(shader->GetHandle());
    if (meshIt == m_meshes.end() || shaderIt == m_shaders.end()) return;

    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *shaderIt->second.pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           *shaderIt->second.layout, 0, { *m_globalSets[m_frameIndex] }, {});

    // ★ Per-object 数据通过 push constant 传（仅 Default 模式的 shader 才有 range）
    if (shaderIt->second.perObjectMode == rhi::PerObjectMode::Default) {
        cmd.pushConstants<rhi::PerObjectData>(
            *shaderIt->second.layout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, data);
    }

    vk::DeviceSize offsets[] = { 0 };
    cmd.bindVertexBuffers(0, vk::Buffer(meshIt->second.vertexBuffer), offsets);

    if (meshIt->second.indexBuffer != VK_NULL_HANDLE) {
        cmd.bindIndexBuffer(vk::Buffer(meshIt->second.indexBuffer), 0, vk::IndexType::eUint32);
    }

    if (material.diffuse) {
        auto texIt = m_textures.find(material.diffuse->GetHandle());
        if (texIt != m_textures.end()) {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   *shaderIt->second.layout, 1, { *texIt->second.set }, {});
        }
    }

    if (meshIt->second.indexBuffer != VK_NULL_HANDLE) {
        cmd.drawIndexed(meshIt->second.indexCount, 1, 0, 0, 0);
    } else {
        cmd.draw(meshIt->second.indexCount, 1, 0, 0);
    }
}

void VulkanAPI::DrawMeshInstanced(const MeshPtr& mesh, const ShaderPtr& shader,
                                  const Material& material,
                                  const std::vector<rhi::PerObjectData>& perObjects) {
    if (!mesh || !shader || perObjects.empty()) return;
    // 现阶段实体数量 < 150，逐个 push constant 的开销远小于引入 instance buffer 的复杂度。
    // 未来粒子/大规模相同 mesh 再引入 Storage Buffer + gl_InstanceIndex。
    for (const auto& data : perObjects) {
        DrawMesh(mesh, shader, material, data);
    }
}

void VulkanAPI::DrawMDI(const ShaderPtr& shader, const Material& material, uint32_t commandCount) {
    if (commandCount == 0 || !shader || m_frameIndex >= m_commandBuffers.size()) return;
    
    auto& cmd = m_commandBuffers[m_frameIndex];
    auto shaderIt = m_shaders.find(shader->GetHandle());
    if (shaderIt == m_shaders.end() || !*(shaderIt->second.pipeline)) return;

    auto& frameBuf = m_frameMDIBuffers[m_frameIndex];
    if (frameBuf.vbo == VK_NULL_HANDLE || frameBuf.ibo == VK_NULL_HANDLE) return;

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *shaderIt->second.pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *shaderIt->second.layout, 0, { *m_globalSets[m_frameIndex] }, {});

    if (material.diffuse) {
        auto texIt = m_textures.find(material.diffuse->GetHandle());
        if (texIt != m_textures.end()) {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *shaderIt->second.layout, 1, { *texIt->second.set }, {});
        }
    }

    vk::DeviceSize offsets[] = { 0 };
    cmd.bindVertexBuffers(0, vk::Buffer(frameBuf.vbo), offsets); 
    cmd.bindIndexBuffer(vk::Buffer(frameBuf.ibo), 0, vk::IndexType::eUint32);

    cmd.drawIndexedIndirect(frameBuf.indirectBuffer, 0, commandCount, sizeof(rhi::DrawIndexedIndirectCommand));
}

void VulkanAPI::DrawSkybox(const ShaderPtr& shader) {
    if (!shader) return;
    
    // 懒加载：第一次画天空盒时生成 Mesh
    if (!m_skyCubeMesh) {
        createSkyboxMesh();
    }
    // 极端情况防崩：万一创建失败
    if (!m_skyCubeMesh) return;

    auto shaderIt = m_shaders.find(shader->GetHandle());
    auto meshIt   = m_meshes.find(m_skyCubeMesh->GetHandle());
    if (shaderIt == m_shaders.end() || meshIt == m_meshes.end()) return;

    auto& cmd = m_commandBuffers[m_frameIndex];
    auto& mesh = meshIt->second;
    
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *shaderIt->second.pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *shaderIt->second.layout, 0, { *m_globalSets[m_frameIndex] }, {});
    
    vk::DeviceSize offsets[] = { 0 };
    cmd.bindVertexBuffers(0, vk::Buffer(mesh.vertexBuffer), offsets);
    
    cmd.draw(36, 1, 0, 0);
}

void VulkanAPI::BlitToScreen([[maybe_unused]] const TexturePtr& texture) {
    if (!texture || !m_frameStarted) return;
    // TODO: 绑定全屏三角形管线 + 纹理 DescriptorSet + cmd.draw(3, 1, 0, 0);
}

// ==========================================
// ImGui 集成
// ==========================================
bool VulkanAPI::InitImGuiBackend() {
    // 1. 为 ImGui 创建专用的 Descriptor Pool
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    vkCreateDescriptorPool(*m_device, &pool_info, nullptr, &m_imguiDescriptorPool);

    // 2. 初始化 ImGui 的 Vulkan 后端
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL3_InitForVulkan(m_window->GetSDLWindow());

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_MAKE_VERSION(api_major, api_minor, 0);
    init_info.Instance = *m_instance;
    init_info.PhysicalDevice = *m_physicalDevice;
    init_info.Device = *m_device;
    init_info.QueueFamily = m_queueIndex;
    init_info.Queue = *m_queue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = m_imguiDescriptorPool;
    init_info.MinImageCount = m_swapChainImages.size();
    init_info.ImageCount = m_swapChainImages.size();
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;

    // 核心：告诉 ImGui 我们使用 Vulkan 1.3 的 Dynamic Rendering
    init_info.UseDynamicRendering = true;
    
    // 配置动态渲染的附件格式
    VkPipelineRenderingCreateInfoKHR pipeline_rendering_info = {};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipeline_rendering_info.colorAttachmentCount = 1;
    pipeline_rendering_info.pColorAttachmentFormats = reinterpret_cast<const VkFormat*>(&m_swapChainSurfaceFormat.format);
    pipeline_rendering_info.depthAttachmentFormat = VK_FORMAT_UNDEFINED; // UI 不需要深度

    // 新版 API：所有管线信息都放进了 PipelineInfoMain
    init_info.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_info;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        logError(m_logger, "[Vulkan] Failed to initialize ImGui Vulkan backend");
        return false;
    }

    m_imguiInitialized = true;
    return true;
}

void VulkanAPI::ShutdownImGuiBackend() {
    if (!m_imguiInitialized) return;
    m_device.waitIdle();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(*m_device, m_imguiDescriptorPool, nullptr);
    m_imguiInitialized = false;
}

void VulkanAPI::ImGuiNewFrame() {
    if (!m_imguiInitialized) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void VulkanAPI::ImGuiRenderDrawData() {
    if (!m_imguiInitialized) return;
    
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->CmdListsCount == 0) return;

    auto& cmd = m_commandBuffers[m_frameIndex];

    // 开启一个新的 Dynamic Rendering 实例专门给 UI 用（或者合并在前一个里）
    // 因为我们是 UI，关闭深度测试和背面剔除
    vk::RenderingAttachmentInfo colorAttachment{};
    colorAttachment.imageView = m_pendingFramebuffer ? *m_framebuffers[m_pendingFramebuffer->GetHandle()].colorView : *m_swapChainImageViews[m_imageIndex];
    colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad; // ★ 重要：保留之前画好的地形，不能 Clear
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;

    vk::RenderingInfo renderingInfo{};
    renderingInfo.renderArea = vk::Rect2D({0, 0}, m_swapChainExtent);
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = nullptr; // UI 不需要深度

    // 注意：如果你的 EndFrame 还没有调用 cmd.endRendering()，这里要用以下代码，或者把它挪到 EndFrame 里
    // 为了逻辑清晰，我们通常让 ImGui 渲染在最后一个单独的 Dynamic Rendering 里
    cmd.beginRendering(renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(draw_data, *cmd);
    cmd.endRendering();
}

// ==========================================
// 杂项 (设备信息、配置、探针)
// ==========================================
[[nodiscard]] DeviceInfo VulkanAPI::GetDeviceInfo() const {
    DeviceInfo info;
    info.backend = "Vulkan";
    auto props = m_physicalDevice.getProperties();
    info.deviceName = std::string(props.deviceName.data()); 
    
    // Vendor ID 转字符串（简单处理）
    switch (props.vendorID) {
        case 0x10DE: info.vendor = "NVIDIA"; break;
        case 0x1002: info.vendor = "AMD"; break;
        case 0x8086: info.vendor = "Intel"; break;
        default: info.vendor = "Unknown"; break;
    }

    info.apiVersion = std::to_string(VK_VERSION_MAJOR(props.apiVersion)) + "." +
                      std::to_string(VK_VERSION_MINOR(props.apiVersion)) + "." +
                      std::to_string(VK_VERSION_PATCH(props.apiVersion));
    
    info.driverVersion = std::to_string(props.driverVersion);
    info.shadingLanguage = "SPIR-V";
    return info;
}

void VulkanAPI::ApplySettings(const WindowConfig& win, const RenderConfig& render) {
    m_windowConfig = win;
    m_renderConfig = render;
    // 控制 VSync (仅能在创建 Swapchain 时决定，重启或重建时生效)
    // 在真正的引擎中，这里会触发 recreateSwapChain
}

void VulkanAPI::WaitIdle() {
    if (*m_device) {
        m_device.waitIdle();
    }
}

void VulkanAPI::logValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, const char* message) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        logWarning(m_logger, "[Vulkan Validation] " << message);
    } else {
        logInfo(m_logger, "[Vulkan Validation] " << message);
    }
}

Eng::client::BackendAvailability VulkanAPI::Probe() {
    Eng::client::BackendAvailability r;
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        r.reason = "Failed to load Vulkan library via SDL";
        return r;
    }

    uint32_t extCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(&extCount)) {
        r.reason = "Failed to get Vulkan instance extensions";
        return r;
    }

    vk::ApplicationInfo appInfo{ .pApplicationName = "Probe", .apiVersion = VK_API_VERSION_1_3 };
    const char* exts[] = { vk::KHRSurfaceExtensionName };
    vk::InstanceCreateInfo createInfo{ .pApplicationInfo = &appInfo, .enabledExtensionCount = 1, .ppEnabledExtensionNames = exts };

    try {
        vk::raii::Context ctx;
        vk::raii::Instance instance(ctx, createInfo);
        auto physicalDevices = instance.enumeratePhysicalDevices();
        
        bool found13 = false;
        for (const auto& pd : physicalDevices) {
            if (pd.getProperties().apiVersion >= VK_API_VERSION_1_3) {
                found13 = true;
                break;
            }
        }
        
        if (found13) {
            r.available = true;
        } else {
            r.reason = "No Vulkan 1.3 compatible GPU found";
        }
    } catch (const std::exception& e) {
        r.reason = e.what();
    }
    
    return r;
}

ShaderPtr VulkanAPI::CreateShader(const ShaderCreateInfo& info) {
    ShaderHandle h = CreateShaderInternal(info);
    if (h == 0) return nullptr;
    return ShaderPtr(new Shader(h), [this](Shader* s) {
        this->DestroyShaderInternal(s->GetHandle());
        delete s;
    });
}

void VulkanAPI::SetUniform([[maybe_unused]] const ShaderPtr& shader, [[maybe_unused]] const std::string& name, [[maybe_unused]] const glm::mat4& value) {
    // Vulkan 中通常通过 UBO/DescriptorSet 设置 Uniform，不支持直接按名字查找
    // 如果你的项目依赖这个接口，建议改用 Push Constants 或者更新 Descriptor Set
    // 这里暂时留空，避免链接报错
}

void VulkanAPI::SetUniform([[maybe_unused]] const ShaderPtr& shader, [[maybe_unused]] const std::string& name, [[maybe_unused]] const glm::vec3& value) {
    // 同上，留空
}

void VulkanAPI::SetUniform([[maybe_unused]] const ShaderPtr& shader, [[maybe_unused]] const std::string& name, [[maybe_unused]] float value) {
    // 同上，留空
}

void VulkanAPI::SetUniform([[maybe_unused]] const ShaderPtr& shader, [[maybe_unused]] const std::string& name, [[maybe_unused]] int value) {
    // 同上，留空
}

} // namespace Eng::client