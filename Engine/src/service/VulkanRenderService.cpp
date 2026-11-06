#include "Engine/service/IRenderService.h"
#include "Engine/service/VulkanRenderService.h"
#include "Engine/tool/Window.h"
#include "Engine/tool/Log.h"

#include <SDL3/SDL_vulkan.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <stdexcept>
#include <algorithm>
#include <utility>
#include <vulkan/vulkan.hpp>

namespace Eng::service {

namespace {
    vk::CompareOp TranslateCompareOp(rhi::CompareOp op) {
        using r = rhi::CompareOp;
        switch (op) {
            case r::Never:          return vk::CompareOp::eNever;
            case r::Less:           return vk::CompareOp::eLess;
            case r::Equal:          return vk::CompareOp::eEqual;
            case r::LessOrEqual:    return vk::CompareOp::eLessOrEqual;
            case r::Greater:        return vk::CompareOp::eGreater;
            case r::NotEqual:       return vk::CompareOp::eNotEqual;
            case r::GreaterOrEqual: return vk::CompareOp::eGreaterOrEqual;
            case r::Always:         return vk::CompareOp::eAlways;
        }
        return vk::CompareOp::eLessOrEqual;
    }

    vk::CullModeFlags TranslateCullMode(rhi::CullMode mode) {
        using r = rhi::CullMode;
        switch (mode) {
            case r::None:         return vk::CullModeFlagBits::eNone;
            case r::Front:        return vk::CullModeFlagBits::eFront;
            case r::Back:         return vk::CullModeFlagBits::eBack;
            case r::FrontAndBack: return vk::CullModeFlagBits::eFrontAndBack;
        }
        return vk::CullModeFlagBits::eBack;
    }

    vk::FrontFace TranslateFrontFace(rhi::FrontFace face) {
        return face == rhi::FrontFace::Clockwise ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise;
    }

    vk::BlendFactor TranslateBlendFactor(rhi::BlendFactor f) {
        using r = rhi::BlendFactor;
        switch (f) {
            case r::Zero:             return vk::BlendFactor::eZero;
            case r::One:              return vk::BlendFactor::eOne;
            case r::SrcAlpha:         return vk::BlendFactor::eSrcAlpha;
            case r::OneMinusSrcAlpha: return vk::BlendFactor::eOneMinusSrcAlpha;
            case r::DstAlpha:         return vk::BlendFactor::eDstAlpha;
            case r::OneMinusDstAlpha: return vk::BlendFactor::eOneMinusDstAlpha;
            case r::SrcColor:         return vk::BlendFactor::eSrcColor;
            case r::OneMinusSrcColor: return vk::BlendFactor::eOneMinusSrcColor;
            case r::DstColor:         return vk::BlendFactor::eDstColor;
            case r::OneMinusDstColor: return vk::BlendFactor::eOneMinusDstColor;
        }
        return vk::BlendFactor::eOne;
    }

    vk::BlendOp TranslateBlendOp(rhi::BlendOp op) {
        using r = rhi::BlendOp;
        switch (op) {
            case r::Add:             return vk::BlendOp::eAdd;
            case r::Subtract:        return vk::BlendOp::eSubtract;
            case r::ReverseSubtract: return vk::BlendOp::eReverseSubtract;
            case r::Min:             return vk::BlendOp::eMin;
            case r::Max:             return vk::BlendOp::eMax;
        }
        return vk::BlendOp::eAdd;
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    [[maybe_unused]] vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) 
{
    auto* logger = static_cast<tool::Log*>(pUserData);
    if (messageSeverity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
        logWarning(logger, "[Vulkan] " << pCallbackData->pMessage);
    }
    return VK_FALSE;
}

VulkanRenderService::VulkanRenderService() = default;

VulkanRenderService::~VulkanRenderService() {
    try { Shutdown(); }
    catch (const std::exception& e) { std::fprintf(stderr, "[VulkanRenderService] Shutdown threw: %s\n", e.what()); }
    catch (...) { std::fputs("[VulkanRenderService] Shutdown threw unknown\n", stderr); }
}

void VulkanRenderService::Initialize(int width, int height, tool::Window* window) {
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
}

void VulkanRenderService::Shutdown() {
    if (m_isShutdown) return;
    m_isShutdown = true;

    try {
        if (*m_device) m_device.waitIdle();
    } catch (const std::exception& e) {
        logError(m_logger, "[Vulkan] waitIdle failed: " << e.what());
    }

    m_deferDeleter.Flush();

    m_defaultTexturePtr.reset();
    m_pendingFramebuffer.reset();

    for (auto& [handle, tex] : m_textures) {
        if (tex.image != VK_NULL_HANDLE && tex.ownsImage) {
            vmaDestroyImage(m_allocator, tex.image, tex.alloc);
        }
    }
    m_textures.clear();
    m_fbColorHandles.clear();

    for (auto& [handle, fb] : m_framebuffers) {
        if (fb.colorImage != VK_NULL_HANDLE) vmaDestroyImage(m_allocator, fb.colorImage, fb.colorAlloc);
        if (fb.hasDepth && fb.depthImage != VK_NULL_HANDLE) vmaDestroyImage(m_allocator, fb.depthImage, fb.depthAlloc);
    }
    m_framebuffers.clear();

    m_shaders.clear();
    m_buffers.clear();

    m_commandBuffers.clear();
    m_commandPool = nullptr;
    m_presentCompleteSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();

    m_descriptorPool = nullptr;
    m_globalSetLayout = nullptr;
    m_textureSetLayout = nullptr;

    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    
    logInfo(m_logger, "[Vulkan] Shutdown complete");
}

glm::mat4 VulkanRenderService::MakePerspectiveMatrix(float fovYRadians, float aspect, float nearZ, float farZ) const {
    glm::mat4 proj = glm::perspective(fovYRadians, aspect, nearZ, farZ);
    proj[1][1] *= -1.0f; // 翻转 Y 轴以适配 Vulkan NDC
    return proj;
}

void VulkanRenderService::createInstance() {
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

void VulkanRenderService::setupDebugMessenger() {
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

void VulkanRenderService::createSurface() {
    VkSurfaceKHR surface;
    if (!SDL_Vulkan_CreateSurface(m_window->GetSDLWindow(), *m_instance, nullptr, &surface)) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }
    m_surface = vk::raii::SurfaceKHR(m_instance, surface);
}

void VulkanRenderService::pickPhysicalDevice() {
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

bool VulkanRenderService::isDeviceSuitable(const vk::raii::PhysicalDevice& pd) {
    auto props = pd.getProperties();
    if (props.apiVersion < VK_API_VERSION_1_3) return false;

    auto features = pd.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
    const auto& vulkan13 = features.get<vk::PhysicalDeviceVulkan13Features>();
    if (!vulkan13.dynamicRendering || !vulkan13.synchronization2) return false;

    auto families = pd.getQueueFamilyProperties();
    bool hasGraphics = false;
    bool hasPresent = false;
    for (uint32_t i = 0; i < families.size(); ++i) {
        if (families[i].queueFlags & vk::QueueFlagBits::eGraphics) hasGraphics = true;
        if (pd.getSurfaceSupportKHR(i, *m_surface)) hasPresent = true;
    }
    return hasGraphics && hasPresent;
}

void VulkanRenderService::createLogicalDevice() {
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

    vk::PhysicalDeviceFeatures baseFeatures{};
    baseFeatures.multiDrawIndirect = VK_TRUE;
    baseFeatures.samplerAnisotropy = VK_TRUE;
    baseFeatures.independentBlend = VK_TRUE;

    vk::PhysicalDeviceVulkan13Features features13{};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    vk::PhysicalDeviceFeatures2 features2{};
    features2.features = baseFeatures;
    features2.pNext = &features13;

    vk::DeviceCreateInfo createInfo{
        .pNext = &features2,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data()
    };

    m_device = vk::raii::Device(m_physicalDevice, createInfo);
    m_queue = vk::raii::Queue(m_device, m_queueIndex, 0);
}

void VulkanRenderService::createVmaAllocator() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = vk::makeApiVersion(0, api_major, api_minor, 0);
    allocatorInfo.physicalDevice = *m_physicalDevice;
    allocatorInfo.device = *m_device;
    allocatorInfo.instance = *m_instance;

    // 在动态模式下，VMA 必须通过这两个入口函数来获取其他所有函数
    VmaVulkanFunctions vmaVulkanFunctions{};
    vmaVulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaVulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    
    // 将配置好的函数指针结构体传给 VMA
    allocatorInfo.pVulkanFunctions = &vmaVulkanFunctions;

    // 注意：vk_mem_alloc.h 中已经定义了 VMA_DYNAMIC_VULKAN_FUNCTIONS 1，
    // 所以这里只需提供这两个入口函数，VMA 会自动补全剩余的函数指针。

    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }
}

void VulkanRenderService::createSwapChain() {
    auto caps = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);
    auto formats = m_physicalDevice.getSurfaceFormatsKHR(*m_surface);
    auto presentModes = m_physicalDevice.getSurfacePresentModesKHR(*m_surface);

    m_swapChainSurfaceFormat = chooseSwapSurfaceFormat(formats);
    vk::PresentModeKHR presentMode = chooseSwapPresentMode(presentModes);
    m_swapChainExtent = chooseSwapExtent(caps);
    uint32_t imageCount = chooseSwapMinImageCount(caps);

    // 强制使用 eIdentity，让驱动程序不要自动旋转我们的画面
    vk::SurfaceTransformFlagBitsKHR preTransform = 
        (caps.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) ? 
        vk::SurfaceTransformFlagBitsKHR::eIdentity : caps.currentTransform;

    vk::SwapchainCreateInfoKHR createInfo{
        .surface = *m_surface,
        .minImageCount = imageCount,
        .imageFormat = m_swapChainSurfaceFormat.format,
        .imageColorSpace = m_swapChainSurfaceFormat.colorSpace,
        .imageExtent = m_swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = preTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = VK_TRUE
    };

    m_swapChain = vk::raii::SwapchainKHR(m_device, createInfo);
    m_swapChainImages = m_swapChain.getImages();
    m_swapChainFirstUse.assign(m_swapChainImages.size(), true);
}

vk::SurfaceFormatKHR VulkanRenderService::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == vk::Format::eB8G8R8A8Unorm && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }
    return formats[0];
}

vk::PresentModeKHR VulkanRenderService::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& modes) {
    for (const auto& mode : modes) {
        if (mode == vk::PresentModeKHR::eMailbox) return mode;
    }
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D VulkanRenderService::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) return caps.currentExtent;
    int w, h;
    SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
    return {
        std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width),
        std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}

uint32_t VulkanRenderService::chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& caps) {
    uint32_t minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && minImageCount > caps.maxImageCount) minImageCount = caps.maxImageCount;
    return minImageCount;
}

void VulkanRenderService::createImageViews() {
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

void VulkanRenderService::cleanupSwapChain() {
    m_depthImageView = nullptr;
    m_depthImageMemory = nullptr;
    m_depthImage = nullptr;
    m_swapChainImageViews.clear();
    m_swapChain = nullptr;
}

void VulkanRenderService::recreateSwapChain() {
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &width, &height);
    
    // 如果不是 0，直接往下走；是 0 就跳过这一帧
    if (width == 0 || height == 0) return; 

    m_device.waitIdle();
    cleanupSwapChain();
    createSwapChain();
    createImageViews();
    createDepthResources();
    createSyncObjects();
    ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(m_swapChainImages.size()));
    m_framebufferResized = false;
}

void VulkanRenderService::createDepthResources() {
    vk::Format depthFormat = findDepthFormat();
    auto [image, memory] = createImage(m_swapChainExtent.width, m_swapChainExtent.height, depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_depthImage = std::move(image);
    m_depthImageMemory = std::move(memory);
    m_depthImageView = createImageView(*m_depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
}

vk::Format VulkanRenderService::findDepthFormat() {
    return findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );
}

vk::Format VulkanRenderService::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
    for (vk::Format format : candidates) {
        vk::FormatProperties props = m_physicalDevice.getFormatProperties(format);
        if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) return format;
        if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) return format;
    }
    throw std::runtime_error("Failed to find supported format!");
}

void VulkanRenderService::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = m_queueIndex
    };
    m_commandPool = vk::raii::CommandPool(m_device, poolInfo);
}

void VulkanRenderService::createCommandBuffers() {
    m_commandBuffers.clear();
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *m_commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT
    };
    m_commandBuffers = m_device.allocateCommandBuffers(allocInfo);
}

void VulkanRenderService::createSyncObjects() {
    m_presentCompleteSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_inFlightFences.emplace_back(m_device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
        m_presentCompleteSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo());
    }
    for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
        m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo());
    }
}

void VulkanRenderService::createDescriptorPool() {
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

void VulkanRenderService::createGlobalDescriptors() {
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
}

// ==========================================
// 资源创建与更新
// ==========================================
ShaderHandle VulkanRenderService::CreateShaderInternal(const rhi::ShaderCreateInfo& info) {
    if (!info.IsValid()) {
        logError(m_logger, "[Vulkan] CreateShaderInternal: empty SPIR-V.");
        return 0;
    }

    ShaderInternal internal;
    internal.vertModule = createShaderModule(info.vertSpirv.Bytes());
    internal.fragModule = createShaderModule(info.fragSpirv.Bytes());

    vk::PipelineLayoutCreateInfo layoutInfo{};
    std::array<vk::DescriptorSetLayout, 2> setLayouts = { *m_globalSetLayout, *m_textureSetLayout };
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts    = setLayouts.data();

    vk::PushConstantRange pushRange{};
    if (info.pushConstantSize > 0) {
        pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushRange.offset     = 0;
        pushRange.size       = info.pushConstantSize;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges    = &pushRange;
    }
    internal.layout = vk::raii::PipelineLayout(m_device, layoutInfo);

    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        { .stage = vk::ShaderStageFlagBits::eVertex,   .module = *internal.vertModule, .pName = "main" },
        { .stage = vk::ShaderStageFlagBits::eFragment, .module = *internal.fragModule, .pName = "main" }
    };

    vk::VertexInputBindingDescription bindingDesc{ 0, sizeof(Vertex), vk::VertexInputRate::eVertex };
    std::vector<vk::VertexInputAttributeDescription> attrDescs;
    if (info.vertexLayout == rhi::VertexLayout::Skybox) {
        attrDescs = { { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position) } };
    } else {
        attrDescs = {
            { 0, 0, vk::Format::eR32G32B32Sfloat,    offsetof(Vertex, position) },
            { 1, 0, vk::Format::eR32G32B32Sfloat,    offsetof(Vertex, normal)   },
            { 2, 0, vk::Format::eR32G32Sfloat,       offsetof(Vertex, uv)       },
            { 3, 0, vk::Format::eR32Sfloat,          offsetof(Vertex, layer)    },
            { 4, 0, vk::Format::eR32Sfloat,          offsetof(Vertex, ao)       },
            { 5, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, light)    }
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

    const auto state = info.pipelineState.value_or(rhi::GraphicsPipelineState::PresetOpaque());

    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = vk::PolygonMode::eFill,
        .cullMode                = TranslateCullMode(state.rasterizer.cullMode),
        .frontFace               = TranslateFrontFace(state.rasterizer.frontFace),
        .depthBiasEnable         = state.rasterizer.depthBiasEnable ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = state.rasterizer.depthBiasConstant,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = state.rasterizer.depthBiasSlope,
        .lineWidth               = state.rasterizer.lineWidth
    };

    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable  = VK_FALSE
    };

    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable       = state.depthStencil.depthTestEnable  ? vk::True : vk::False,
        .depthWriteEnable      = state.depthStencil.depthWriteEnable ? vk::True : vk::False,
        .depthCompareOp        = TranslateCompareOp(state.depthStencil.depthCompareOp),
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = state.depthStencil.stencilTestEnable ? vk::True : vk::False,
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f
    };

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable         = state.blend.blendEnable ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = TranslateBlendFactor(state.blend.srcColorBlendFactor),
        .dstColorBlendFactor = TranslateBlendFactor(state.blend.dstColorBlendFactor),
        .colorBlendOp        = TranslateBlendOp(state.blend.colorBlendOp),
        .srcAlphaBlendFactor = TranslateBlendFactor(state.blend.srcAlphaBlendFactor),
        .dstAlphaBlendFactor = TranslateBlendFactor(state.blend.dstAlphaBlendFactor),
        .alphaBlendOp        = TranslateBlendOp(state.blend.alphaBlendOp),
        .colorWriteMask      = static_cast<vk::ColorComponentFlags>(state.blend.colorWriteMask)
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
        .colorAttachmentCount    = 1,
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

    internal.pipeline         = vk::raii::Pipeline(m_device, nullptr, pipelineInfo);
    internal.pushConstantSize = info.pushConstantSize;

    ShaderHandle handle = m_nextShader++;
    m_shaders[handle]   = std::move(internal);
    return handle;
}

TextureHandle VulkanRenderService::CreateTextureInternal(const rhi::TextureCreateInfo& info) {
    if (!info.pixels.data() || info.width <= 0 || info.height <= 0)
        return 0;

    TextureInternal internal;
    
    // 根据 info.format 映射 VkFormat 和 bpp (bytes per pixel)
    vk::Format vkFormat = vk::Format::eR8G8B8A8Unorm;
    int bpp = 4;
    switch (info.format) {
        case rhi::TextureFormat::RGBA8_UNORM:   vkFormat = vk::Format::eR8G8B8A8Unorm; bpp = 4; break;
        case rhi::TextureFormat::RGBA16_SFLOAT: vkFormat = vk::Format::eR16G16B16A16Sfloat; bpp = 8; break;
        case rhi::TextureFormat::R8_UNORM:      vkFormat = vk::Format::eR8Unorm; bpp = 1; break;
        case rhi::TextureFormat::D32_SFLOAT:    vkFormat = vk::Format::eD32Sfloat; bpp = 4; break;
    }

    // 强制至少 1 层，配合 Shader 中的 sampler2DArray
    uint32_t arrayLayers = std::max(1u, static_cast<uint32_t>(info.arrayLayers));
    VkDeviceSize imageSize = static_cast<size_t>(info.width) * info.height * arrayLayers * bpp;

    uint32_t maxPossibleMips = static_cast<uint32_t>(std::floor(std::log2(std::max(info.width, info.height)))) + 1;
    uint32_t mipLevels = 1;
    if (info.requestedMips == 0) {
        if (m_renderConfig.mipmapLevels > 0) {
            mipLevels = std::min(maxPossibleMips, static_cast<uint32_t>(m_renderConfig.mipmapLevels + 1));
        }
    } else {
        mipLevels = std::min(maxPossibleMips, static_cast<uint32_t>(info.requestedMips));
    }

    VkBuffer stagingBuf; VmaAllocation stagingAlloc;
    vk::BufferCreateInfo stagingInfo{ .size = static_cast<vk::DeviceSize>(imageSize), .usage = vk::BufferUsageFlagBits::eTransferSrc, .sharingMode = vk::SharingMode::eExclusive };
    VmaAllocationCreateInfo stagingAllocInfo{}; stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    vmaCreateBuffer(m_allocator, reinterpret_cast<const VkBufferCreateInfo*>(&stagingInfo), &stagingAllocInfo, &stagingBuf, &stagingAlloc, nullptr);

    void* mapped;
    vmaMapMemory(m_allocator, stagingAlloc, &mapped);
    memcpy(mapped, info.pixels.data(), imageSize);
    vmaUnmapMemory(m_allocator, stagingAlloc);

    vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D, .format = vkFormat,
        .extent = { static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height), 1 },
        .mipLevels = mipLevels, .arrayLayers = arrayLayers, .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive, .initialLayout = vk::ImageLayout::eUndefined
    };
    
    if (info.format == rhi::TextureFormat::D32_SFLOAT) {
        imageInfo.usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    }

    VmaAllocationCreateInfo allocInfo{}; allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateImage(m_allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &internal.image, &internal.alloc, nullptr);

    auto cmd = beginSingleTimeCommands();

    vk::ImageMemoryBarrier2 initBarrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe, .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer, .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined, .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .image = internal.image, .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, arrayLayers }
    };
    
    if (info.format == rhi::TextureFormat::D32_SFLOAT) {
        initBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    }
    
    cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &initBarrier });

    vk::BufferImageCopy region{
        .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
        .imageSubresource = { 
            (info.format == rhi::TextureFormat::D32_SFLOAT ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor), 
            0, 0, arrayLayers 
        },
        .imageOffset = { 0, 0, 0 }, .imageExtent = { static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height), 1 }
    };
    cmd.copyBufferToImage(stagingBuf, internal.image, vk::ImageLayout::eTransferDstOptimal, region);

    // Mipmap 生成逻辑 (深度图通常不需要 Mipmap)
    if (mipLevels > 1 && info.format != rhi::TextureFormat::D32_SFLOAT) {
        vk::ImageMemoryBarrier2 barrier0{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer, .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer, .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal, .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .image = internal.image, .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, arrayLayers }
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier0 });

        int32_t mipW = info.width, mipH = info.height;
        for (uint32_t i = 1; i < mipLevels; ++i) {
            vk::ImageMemoryBarrier2 dstBarrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eTransfer, .srcAccessMask = vk::AccessFlagBits2::eNone,
                .dstStageMask = vk::PipelineStageFlagBits2::eTransfer, .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .oldLayout = vk::ImageLayout::eUndefined, .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .image = internal.image, .subresourceRange = { vk::ImageAspectFlagBits::eColor, i, 1, 0, arrayLayers }
            };
            cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &dstBarrier });

            std::array<vk::Offset3D, 2> srcOffsets = { vk::Offset3D{0, 0, 0}, vk::Offset3D{mipW, mipH, 1} };
            std::array<vk::Offset3D, 2> dstOffsets = { vk::Offset3D{0, 0, 0}, vk::Offset3D{mipW > 1 ? mipW / 2 : 1, mipH > 1 ? mipH / 2 : 1, 1} };

            vk::ImageBlit blit{
                .srcSubresource = { vk::ImageAspectFlagBits::eColor, i - 1, 0, arrayLayers }, .srcOffsets = srcOffsets,
                .dstSubresource = { vk::ImageAspectFlagBits::eColor, i, 0, arrayLayers }, .dstOffsets = dstOffsets
            };
            cmd.blitImage(internal.image, vk::ImageLayout::eTransferSrcOptimal, internal.image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

            if (i + 1 < mipLevels) {
                vk::ImageMemoryBarrier2 srcBarrier{
                    .srcStageMask = vk::PipelineStageFlagBits2::eTransfer, .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                    .dstStageMask = vk::PipelineStageFlagBits2::eTransfer, .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
                    .oldLayout = vk::ImageLayout::eTransferDstOptimal, .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                    .image = internal.image, .subresourceRange = { vk::ImageAspectFlagBits::eColor, i, 1, 0, arrayLayers }
                };
                cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &srcBarrier });
            }
            if (mipW > 1) mipW /= 2;
            if (mipH > 1) mipH /= 2;
        }

        vk::ImageMemoryBarrier2 lastBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer, .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer, .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal, .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .image = internal.image, .subresourceRange = { vk::ImageAspectFlagBits::eColor, mipLevels - 1, 1, 0, arrayLayers }
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &lastBarrier });

        vk::ImageMemoryBarrier2 finalBarrierAll{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer, .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader, .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferSrcOptimal, .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = internal.image, .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, arrayLayers }
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &finalBarrierAll });
    } else {
        vk::ImageAspectFlags aspect = (info.format == rhi::TextureFormat::D32_SFLOAT) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
        vk::ImageLayout finalLayout = (info.format == rhi::TextureFormat::D32_SFLOAT) ? vk::ImageLayout::eDepthAttachmentOptimal : vk::ImageLayout::eShaderReadOnlyOptimal;
        
        vk::ImageMemoryBarrier2 finalBarrier1Layer{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer, .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader, .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal, .newLayout = finalLayout,
            .image = internal.image, .subresourceRange = { aspect, 0, 1, 0, arrayLayers }
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &finalBarrier1Layer });
    }

    endSingleTimeCommands(std::move(cmd));
    vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);

    // ★ 强制使用 e2DArray，配合 Shader 中的 sampler2DArray
    vk::ImageViewType viewType = vk::ImageViewType::e2DArray; 
    vk::ImageAspectFlags aspectMask = (info.format == rhi::TextureFormat::D32_SFLOAT) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;

    vk::ImageViewCreateInfo viewInfo{
        .image = internal.image, 
        .viewType = viewType, 
        .format = vkFormat,
        .subresourceRange = { aspectMask, 0, mipLevels, 0, arrayLayers }
    };
    internal.view = vk::raii::ImageView(m_device, viewInfo);

    // Sampler 配置
    vk::Filter vkMagFilter = (info.magFilter == rhi::FilterMode::Linear) ? vk::Filter::eLinear : vk::Filter::eNearest;
    vk::Filter vkMinFilter = vk::Filter::eNearest;
    vk::SamplerMipmapMode vkMipmapMode = vk::SamplerMipmapMode::eNearest;
    switch (info.minFilter) {
        case rhi::MinFilterMode::Nearest:                vkMinFilter = vk::Filter::eNearest; vkMipmapMode = vk::SamplerMipmapMode::eNearest; break;
        case rhi::MinFilterMode::Linear:                 vkMinFilter = vk::Filter::eLinear; vkMipmapMode = vk::SamplerMipmapMode::eNearest; break;
        case rhi::MinFilterMode::Nearest_Mipmap_Nearest: vkMinFilter = vk::Filter::eNearest; vkMipmapMode = vk::SamplerMipmapMode::eNearest; break;
        case rhi::MinFilterMode::Linear_Mipmap_Nearest:  vkMinFilter = vk::Filter::eLinear; vkMipmapMode = vk::SamplerMipmapMode::eNearest; break;
        case rhi::MinFilterMode::Nearest_Mipmap_Linear:  vkMinFilter = vk::Filter::eNearest; vkMipmapMode = vk::SamplerMipmapMode::eLinear; break;
        case rhi::MinFilterMode::Linear_Mipmap_Linear:   vkMinFilter = vk::Filter::eLinear; vkMipmapMode = vk::SamplerMipmapMode::eLinear; break;
    }
    if (mipLevels <= 1) vkMipmapMode = vk::SamplerMipmapMode::eNearest;

    auto mapWrap = [](rhi::WrapMode w) -> vk::SamplerAddressMode {
        switch (w) {
            case rhi::WrapMode::Repeat: return vk::SamplerAddressMode::eRepeat;
            case rhi::WrapMode::MirroredRepeat: return vk::SamplerAddressMode::eMirroredRepeat;
            default: return vk::SamplerAddressMode::eClampToEdge;
        }
    };

    vk::SamplerCreateInfo samplerInfo{
        .magFilter = vkMagFilter, 
        .minFilter = vkMinFilter, 
        .mipmapMode = vkMipmapMode,
        .addressModeU = mapWrap(info.wrapU), 
        .addressModeV = mapWrap(info.wrapV), 
        .addressModeW = mapWrap(info.wrapU),
        .anisotropyEnable = (mipLevels > 1 && info.enableAnisotropy && m_renderConfig.enableAnisotropic) ? vk::True : vk::False,
        .maxAnisotropy = static_cast<float>(m_renderConfig.maxAnisotropy), 
        .maxLod = static_cast<float>(mipLevels)
    };
    internal.sampler = vk::raii::Sampler(m_device, samplerInfo);

    vk::DescriptorSetAllocateInfo allocSetInfo{ .descriptorPool = *m_descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &*m_textureSetLayout };
    internal.set = std::move(m_device.allocateDescriptorSets(allocSetInfo)[0]);

    vk::DescriptorImageInfo imageDesc{ .sampler = *internal.sampler, .imageView = *internal.view, .imageLayout = (info.format == rhi::TextureFormat::D32_SFLOAT) ? vk::ImageLayout::eDepthAttachmentOptimal : vk::ImageLayout::eShaderReadOnlyOptimal };
    vk::WriteDescriptorSet write{
        .dstSet = *internal.set, .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageDesc
    };
    m_device.updateDescriptorSets(write, {});

    TextureHandle handle = m_nextTexture++;
    m_textures[handle] = std::move(internal);
    return handle;
}

FramebufferPtr VulkanRenderService::CreateFramebuffer(const rhi::FramebufferCreateInfo& info) {
    FramebufferInternal fb;
    fb.width = info.width; fb.height = info.height; fb.hasDepth = info.hasDepth;

    vk::ImageCreateInfo colorImageInfo{
        .imageType = vk::ImageType::e2D, .format = vk::Format::eR8G8B8A8Unorm,
        .extent = { static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height), 1 },
        .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive, .initialLayout = vk::ImageLayout::eUndefined
    };
    VmaAllocationCreateInfo colorAllocInfo{}; colorAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateImage(m_allocator, reinterpret_cast<const VkImageCreateInfo*>(&colorImageInfo), &colorAllocInfo, &fb.colorImage, &fb.colorAlloc, nullptr);
    fb.colorView = createImageView(fb.colorImage, vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor);

    if (info.hasDepth) {
        vk::ImageCreateInfo depthImageInfo{
            .imageType = vk::ImageType::e2D, .format = findDepthFormat(),
            .extent = { static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height), 1 },
            .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment, .sharingMode = vk::SharingMode::eExclusive, .initialLayout = vk::ImageLayout::eUndefined
        };
        VmaAllocationCreateInfo depthAllocInfo{}; depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateImage(m_allocator, reinterpret_cast<const VkImageCreateInfo*>(&depthImageInfo), &depthAllocInfo, &fb.depthImage, &fb.depthAlloc, nullptr);
        fb.depthView = createImageView(fb.depthImage, findDepthFormat(), vk::ImageAspectFlagBits::eDepth);
    }

    TextureInternal texInt;
    texInt.image = fb.colorImage; texInt.alloc = fb.colorAlloc; texInt.ownsImage = false;
    
    vk::ImageViewCreateInfo viewInfo{
        .image = fb.colorImage, .viewType = vk::ImageViewType::e2D, .format = vk::Format::eR8G8B8A8Unorm,
        .subresourceRange = { .aspectMask=vk::ImageAspectFlagBits::eColor, .baseMipLevel=0, .levelCount=1, .baseArrayLayer=0, .layerCount=1 }
    };
    texInt.view = vk::raii::ImageView(m_device, viewInfo);
    vk::SamplerCreateInfo samplerInfo{}; samplerInfo.magFilter = vk::Filter::eLinear; samplerInfo.minFilter = vk::Filter::eLinear;
    texInt.sampler = vk::raii::Sampler(m_device, samplerInfo);
    
    vk::DescriptorSetAllocateInfo allocSetInfo{ .descriptorPool = *m_descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &*m_textureSetLayout };
    texInt.set = std::move(m_device.allocateDescriptorSets(allocSetInfo)[0]);
    
    vk::DescriptorImageInfo imageDesc{ .sampler = *texInt.sampler, .imageView = *texInt.view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
    vk::WriteDescriptorSet write{
        .dstSet = *texInt.set, .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageDesc
    };
    m_device.updateDescriptorSets(write, {});
    
    fb.colorHandle = m_nextTexture++;
    m_textures[fb.colorHandle] = std::move(texInt);

    FramebufferHandle h = m_nextFramebuffer++;
    m_fbColorHandles[h] = fb.colorHandle;
    m_framebuffers.emplace(h, std::move(fb));

    TexturePtr colorTexturePtr = TexturePtr(new Texture(m_fbColorHandles[h]), [this](Texture* t) { this->DestroyTextureInternal(t->GetHandle()); delete t; });
    
    return FramebufferPtr(new Framebuffer(h, colorTexturePtr, info.width, info.height), [this](Framebuffer* f) { 
        auto it = m_framebuffers.find(f->GetHandle());
        if (it != m_framebuffers.end()) {
            m_device.waitIdle();
            vmaDestroyImage(m_allocator, it->second.colorImage, it->second.colorAlloc);
            if (it->second.hasDepth) vmaDestroyImage(m_allocator, it->second.depthImage, it->second.depthAlloc);
            m_fbColorHandles.erase(f->GetHandle());
            m_framebuffers.erase(it);
        }
        delete f; 
    });
}

BufferHandle VulkanRenderService::CreateBufferInternal(size_t size, rhi::BufferUsage usage) {
    BufferInternal internal;
    internal.size = size;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (usage == rhi::BufferUsage::Vertex)   info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (usage == rhi::BufferUsage::Index)    info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (usage == rhi::BufferUsage::Indirect) info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (usage == rhi::BufferUsage::Uniform)  info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VmaAllocationInfo allocResult;
    vmaCreateBuffer(m_allocator, &info, &allocInfo, &internal.buffer, &internal.alloc, &allocResult);
    internal.mapped = allocResult.pMappedData;

    BufferHandle handle = m_nextBuffer++;
    m_buffers.emplace(handle, std::move(internal));
    return handle;
}

// ==========================================
// 资源销毁
// ==========================================
void VulkanRenderService::DestroyShaderInternal(ShaderHandle h) {
    auto it = m_shaders.find(h);
    if (it == m_shaders.end()) return;
    m_device.waitIdle();
    m_shaders.erase(it);
}

void VulkanRenderService::DestroyTextureInternal(TextureHandle h) {
    auto it = m_textures.find(h);
    if (it != m_textures.end()) {
        if (it->second.image != VK_NULL_HANDLE && it->second.ownsImage) {
            m_device.waitIdle();
            vmaDestroyImage(m_allocator, it->second.image, it->second.alloc);
        }
        m_textures.erase(it);
    }
}

void VulkanRenderService::DestroyFramebufferInternal(FramebufferHandle h) {
    auto it = m_framebuffers.find(h);
    if (it == m_framebuffers.end()) return;
    m_device.waitIdle();
    vmaDestroyImage(m_allocator, it->second.colorImage, it->second.colorAlloc);
    if (it->second.hasDepth) vmaDestroyImage(m_allocator, it->second.depthImage, it->second.depthAlloc);
    m_framebuffers.erase(it);
    
    auto texIt = m_fbColorHandles.find(h);
    if (texIt != m_fbColorHandles.end()) m_fbColorHandles.erase(texIt);
}

void VulkanRenderService::DestroyBufferInternal(BufferHandle h) {
    auto it = m_buffers.find(h);
    if (it == m_buffers.end()) return;

    if (it->second.buffer != VK_NULL_HANDLE) {
        VkBuffer buf = it->second.buffer;
        VmaAllocation alloc = it->second.alloc;
        // 交给通用延迟销毁器，延迟 MAX_FRAMES_IN_FLIGHT 帧
        m_deferDeleter.Defer([this, buf, alloc]() {
            vmaDestroyBuffer(m_allocator, buf, alloc);
        }, m_frameCounter, MAX_FRAMES_IN_FLIGHT);
    }
    m_buffers.erase(it);
}

// ==========================================
// 接口封装
// ==========================================
BufferPtr VulkanRenderService::CreateBuffer(const rhi::BufferCreateInfo& info) {
    if (info.size == 0) return nullptr;
    BufferHandle h = CreateBufferInternal(info.size, info.usage);
    if (h == 0) return nullptr;
    return BufferPtr(new Buffer(h), [this](Buffer* b) {
        this->DestroyBufferInternal(b->GetHandle());
        delete b;
    });
}

TexturePtr VulkanRenderService::CreateTexture(const rhi::TextureCreateInfo& info) {
    if (info.format != rhi::TextureFormat::RGBA8_UNORM) return nullptr;
    if (!info.pixels.data() || info.width <= 0 || info.height <= 0) return nullptr;
    TextureHandle th = CreateTextureInternal(info);
    if (th == 0) return nullptr;
    return TexturePtr(new Texture(th), [this](Texture* t) {
        this->DestroyTextureInternal(t->GetHandle());
        delete t;
    });
}

ShaderPtr VulkanRenderService::CreateShader(const rhi::ShaderCreateInfo& info) {
    ShaderHandle h = CreateShaderInternal(info);
    if (h == 0) return nullptr;
    return ShaderPtr(new Shader(h), [this](Shader* s) {
        this->DestroyShaderInternal(s->GetHandle());
        delete s;
    });
}

void VulkanRenderService::UpdateBuffer(const BufferPtr& buffer, size_t offset, std::span<const std::byte> data) {
    if (!buffer) return;
    auto it = m_buffers.find(buffer->GetHandle());
    if (it != m_buffers.end() && it->second.mapped) {
        memcpy(static_cast<char*>(it->second.mapped) + offset, data.data(), data.size());
        vmaFlushAllocation(m_allocator, it->second.alloc, offset, data.size());
    }
}

void VulkanRenderService::BindFramebuffer(const FramebufferPtr& fb) { m_pendingFramebuffer = fb; }
void VulkanRenderService::UnbindFramebuffer() { m_pendingFramebuffer = nullptr; }

// ==========================================
// 绑定与绘制 (无状态)
// ==========================================
void VulkanRenderService::BindShader(const ShaderPtr& shader) {
    if (!shader) return;
    auto shaderIt = m_shaders.find(shader->GetHandle());
    if (shaderIt == m_shaders.end() || !*(shaderIt->second.pipeline)) return;

    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *shaderIt->second.pipeline);
    m_currentLayout = &shaderIt->second.layout;

    if (m_boundGlobalUBO) {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *shaderIt->second.layout, 0, { m_boundGlobalUBO }, {});
    }
}

void VulkanRenderService::BindVertexBuffer(const BufferPtr& buffer, uint32_t binding) {
    if (!buffer || binding != 0) return;
    auto it = m_buffers.find(buffer->GetHandle());
    if (it != m_buffers.end()) {
        m_commandBuffers[m_frameIndex].bindVertexBuffers(0, { vk::Buffer(it->second.buffer) }, { vk::DeviceSize(0) });
    }
}

void VulkanRenderService::BindIndexBuffer(const BufferPtr& buffer) {
    if (!buffer) return;
    auto it = m_buffers.find(buffer->GetHandle());
    if (it != m_buffers.end()) {
        m_commandBuffers[m_frameIndex].bindIndexBuffer(it->second.buffer, 0, vk::IndexType::eUint32);
    }
}

void VulkanRenderService::BindTexture(const TexturePtr& texture, uint32_t slot) {
    if (!texture || slot != 0) return;
    auto texIt = m_textures.find(texture->GetHandle());
    if (texIt != m_textures.end() && m_currentLayout) {
        m_commandBuffers[m_frameIndex].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_currentLayout, 1, { *texIt->second.set }, {});
    }
}

void VulkanRenderService::BindUniformBuffer(const BufferPtr& buffer, uint32_t set, uint32_t binding) {
    if (!buffer || set != 0 || binding != 0) return;
    auto it = m_buffers.find(buffer->GetHandle());
    if (it == m_buffers.end() || it->second.buffer == VK_NULL_HANDLE) return;

    auto& bufInternal = it->second;

    if (bufInternal.descriptorSet == nullptr) {
        vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = *m_descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &*m_globalSetLayout };
        bufInternal.descriptorSet = std::move(m_device.allocateDescriptorSets(allocInfo)[0]);

        vk::DescriptorBufferInfo dbi{ .buffer = vk::Buffer(bufInternal.buffer), .offset = 0, .range = bufInternal.size };
        vk::WriteDescriptorSet write{
            .dstSet = *bufInternal.descriptorSet, .dstBinding = 0, .dstArrayElement = 0, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer, .pBufferInfo = &dbi
        };
        m_device.updateDescriptorSets(write, {});
    }

    m_boundGlobalUBO = *bufInternal.descriptorSet;

    if (m_currentLayout) {
        m_commandBuffers[m_frameIndex].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_currentLayout, set, { *bufInternal.descriptorSet }, {});
    }
}

void VulkanRenderService::PushConstants(const ShaderPtr& shader, std::span<const std::byte> data) {
    if (!shader || data.empty()) return;
    auto shaderIt = m_shaders.find(shader->GetHandle());
    if (shaderIt == m_shaders.end() || shaderIt->second.pushConstantSize == 0) return;

    const auto effectiveSize = std::min<size_t>(data.size(), shaderIt->second.pushConstantSize);
    m_commandBuffers[m_frameIndex].pushConstants(
        *shaderIt->second.layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, static_cast<uint32_t>(effectiveSize), data.data());
}

void VulkanRenderService::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex) {
    m_commandBuffers[m_frameIndex].draw(vertexCount, instanceCount, firstVertex, 0);
}

void VulkanRenderService::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset) {
    m_commandBuffers[m_frameIndex].drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, 0);
}

void VulkanRenderService::DrawIndexedIndirect(const BufferPtr& indirectBuffer, uint32_t drawCount, uint32_t stride) {
    if (!indirectBuffer) return;
    auto it = m_buffers.find(indirectBuffer->GetHandle());
    if (it != m_buffers.end()) {
        m_commandBuffers[m_frameIndex].drawIndexedIndirect(it->second.buffer, 0, drawCount, stride);
    }
}

// ==========================================
// 帧控制
// ==========================================
void VulkanRenderService::BeginFrame() {
    m_frameStarted = false;

    // 每帧检查窗口实际尺寸，如果变了就立刻重建 Swapchain
    int curW = 0, curH = 0;
    SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &curW, &curH);
    if (curW > 0 && curH > 0 && 
        (std::cmp_not_equal(curW, m_swapChainExtent.width) || 
         std::cmp_not_equal(curH, m_swapChainExtent.height))) {
        m_framebufferResized = true;
    }

    if (m_framebufferResized) {
        recreateSwapChain();
    }

    (void)m_device.waitForFences({ *m_inFlightFences[m_frameIndex] }, VK_TRUE, UINT64_MAX);

    try {
        auto resultValue = m_swapChain.acquireNextImage(UINT64_MAX, *m_presentCompleteSemaphores[m_frameIndex], nullptr);
        if (resultValue.result == vk::Result::eSuboptimalKHR) m_framebufferResized = true; 
        else if (resultValue.result == vk::Result::eErrorOutOfDateKHR) { recreateSwapChain(); return; }
        m_imageIndex = resultValue.value;
    } catch (const vk::OutOfDateKHRError&) { recreateSwapChain(); return; }

    m_device.resetFences({ *m_inFlightFences[m_frameIndex] });
    
    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.reset();
    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    m_frameCounter++;
    m_deferDeleter.Process(m_frameCounter);

    vk::Image targetImage = m_pendingFramebuffer ? vk::Image(m_framebuffers[m_pendingFramebuffer->GetHandle()].colorImage) : m_swapChainImages[m_imageIndex];
    vk::ImageLayout layout = vk::ImageLayout::eColorAttachmentOptimal;

    transition_image_layout(cmd, targetImage, vk::ImageLayout::eUndefined, layout, 
        vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eColorAttachmentWrite, 
        vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eColorAttachmentOutput, 
        vk::ImageAspectFlagBits::eColor);

    vk::Image depthTargetImage = m_pendingFramebuffer ? vk::Image(m_framebuffers[m_pendingFramebuffer->GetHandle()].depthImage) : *m_depthImage;
    if (depthTargetImage != VK_NULL_HANDLE) {
        transition_image_layout(cmd, depthTargetImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal, 
            vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, 
            vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, 
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

    m_frameStarted = true;
}

void VulkanRenderService::EndFrame() {
    if (!m_frameStarted) return;
    m_frameStarted = false;

    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.endRendering();

    ImGui::Render(); 
    ImGuiRenderDrawData(); 

    if (!m_pendingFramebuffer) {
        transition_image_layout(cmd, vk::Image(m_swapChainImages[m_imageIndex]), 
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR, 
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eNone, 
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eBottomOfPipe, 
            vk::ImageAspectFlagBits::eColor);
    } else {
        transition_image_layout(cmd, vk::Image(m_framebuffers[m_pendingFramebuffer->GetHandle()].colorImage), 
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 
            vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderRead, 
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader, 
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
// 辅助工具
// ==========================================
vk::raii::CommandBuffer VulkanRenderService::beginSingleTimeCommands() {
    vk::CommandBufferAllocateInfo allocInfo{ .commandPool = *m_commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 };
    vk::raii::CommandBuffer cmd = std::move(m_device.allocateCommandBuffers(allocInfo).front());
    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    return cmd;
}

void VulkanRenderService::endSingleTimeCommands(vk::raii::CommandBuffer&& cmd) {
    cmd.end();
    vk::SubmitInfo submitInfo{ .commandBufferCount = 1, .pCommandBuffers = &*cmd };
    m_queue.submit(submitInfo, nullptr);
    m_queue.waitIdle();
}

void VulkanRenderService::transition_image_layout(const vk::raii::CommandBuffer& cmd, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage, vk::ImageAspectFlags aspect) {
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

std::pair<vk::raii::Image, vk::raii::DeviceMemory> VulkanRenderService::createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties) {
    vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D, .format = format,
        .extent = { width, height, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1, .tiling = tiling, .usage = usage, .sharingMode = vk::SharingMode::eExclusive
    };
    vk::raii::Image image = vk::raii::Image(m_device, imageInfo);
    vk::MemoryRequirements memReqs = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{ .allocationSize = memReqs.size, .memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties) };
    vk::raii::DeviceMemory memory = vk::raii::DeviceMemory(m_device, allocInfo);
    image.bindMemory(*memory, 0);
    return { std::move(image), std::move(memory) };
}

vk::raii::ImageView VulkanRenderService::createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels) {
    vk::ImageViewCreateInfo viewInfo{
        .image = image, .viewType = vk::ImageViewType::e2D, .format = format,
        .subresourceRange = { aspectFlags, 0, mipLevels, 0, 1 } 
    };
    return vk::raii::ImageView(m_device, viewInfo);
}

uint32_t VulkanRenderService::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = m_physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) return i;
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

vk::raii::ShaderModule VulkanRenderService::createShaderModule(std::span<const std::byte> code) const {
    vk::ShaderModuleCreateInfo createInfo{ .codeSize = code.size_bytes(), .pCode = reinterpret_cast<const uint32_t*>(code.data()) };
    return vk::raii::ShaderModule(m_device, createInfo);
}

void VulkanRenderService::SetViewport(int x, int y, int w, int h) {
    m_commandBuffers[m_frameIndex].setViewport(0, vk::Viewport(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f));
    m_commandBuffers[m_frameIndex].setScissor(0, vk::Rect2D({x, y}, {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}));
}

void VulkanRenderService::Clear(rhi::ClearFlag flag, const glm::vec4& color, float depth, uint8_t stencil) {
    m_clearPendingFlags = static_cast<uint32_t>(flag);
    m_clearColor[0] = color.r; m_clearColor[1] = color.g; m_clearColor[2] = color.b; m_clearColor[3] = color.a;
    (void)depth; (void)stencil;
}

// ==========================================
// ImGui 与杂项
// ==========================================
bool VulkanRenderService::InitImGuiBackend() {
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    vkCreateDescriptorPool(*m_device, &pool_info, nullptr, &m_imguiDescriptorPool);
    
    ImGui_ImplSDL3_InitForVulkan(m_window->GetSDLWindow());

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_MAKE_VERSION(api_major, api_minor, 0);
    init_info.Instance = *m_instance; init_info.PhysicalDevice = *m_physicalDevice;
    init_info.Device = *m_device; init_info.QueueFamily = m_queueIndex; init_info.Queue = *m_queue;
    init_info.PipelineCache = VK_NULL_HANDLE; init_info.DescriptorPool = m_imguiDescriptorPool;
    init_info.MinImageCount = m_swapChainImages.size(); init_info.ImageCount = m_swapChainImages.size();
    init_info.Allocator = nullptr; init_info.CheckVkResultFn = nullptr;
    init_info.UseDynamicRendering = true;
    
    VkPipelineRenderingCreateInfoKHR pipeline_rendering_info = {};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipeline_rendering_info.colorAttachmentCount = 1;
    pipeline_rendering_info.pColorAttachmentFormats = reinterpret_cast<const VkFormat*>(&m_swapChainSurfaceFormat.format);
    pipeline_rendering_info.depthAttachmentFormat = VK_FORMAT_UNDEFINED;

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

void VulkanRenderService::ShutdownImGuiBackend() {
    if (!m_imguiInitialized) return;
    m_device.waitIdle();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    vkDestroyDescriptorPool(*m_device, m_imguiDescriptorPool, nullptr);
    m_imguiInitialized = false;
}

void VulkanRenderService::ImGuiNewFrame() {
    if (!m_imguiInitialized) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void VulkanRenderService::ImGuiRenderDrawData() {
    if (!m_imguiInitialized) return;
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->CmdListsCount == 0) return;

    auto& cmd = m_commandBuffers[m_frameIndex];

    vk::RenderingAttachmentInfo colorAttachment{};
    colorAttachment.imageView = m_pendingFramebuffer ? *m_framebuffers[m_pendingFramebuffer->GetHandle()].colorView : *m_swapChainImageViews[m_imageIndex];
    colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;

    vk::RenderingInfo renderingInfo{};
    renderingInfo.renderArea = vk::Rect2D({0, 0}, m_swapChainExtent);
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = nullptr;

    cmd.beginRendering(renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(draw_data, *cmd);
    cmd.endRendering();
}

DeviceInfo VulkanRenderService::GetDeviceInfo() const {
    DeviceInfo info;
    info.backend = "Vulkan";
    auto props = m_physicalDevice.getProperties();
    info.deviceName = std::string(props.deviceName.data()); 
    
    switch (props.vendorID) {
        case 0x10DE: info.vendor = "NVIDIA"; break;
        case 0x1002: info.vendor = "AMD"; break;
        case 0x8086: info.vendor = "Intel"; break;
        default: info.vendor = "Unknown"; break;
    }

    info.apiVersion = std::to_string(VK_VERSION_MAJOR(props.apiVersion)) + "." + std::to_string(VK_VERSION_MINOR(props.apiVersion)) + "." + std::to_string(VK_VERSION_PATCH(props.apiVersion));
    info.driverVersion = std::to_string(props.driverVersion);
    info.shadingLanguage = "SPIR-V";
    return info;
}

void VulkanRenderService::ApplySettings(const tool::WindowConfig& win, const tool::RenderConfig& render) {
    m_windowConfig = win;
    m_renderConfig = render;
}

void VulkanRenderService::WaitIdle() {
    if (*m_device) m_device.waitIdle();
}

Eng::service::BackendAvailability VulkanRenderService::Probe() {
    Eng::service::BackendAvailability r;
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        r.reason = "Failed to load Vulkan library via SDL";
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
                auto props = pd.getProperties();
                r.caps.apiName = "Vulkan";
                r.caps.apiMajor = vk::versionMajor(props.apiVersion);
                r.caps.apiMinor = vk::versionMinor(props.apiVersion);
                r.caps.maxTextureSize = props.limits.maxImageDimension2D;
                r.caps.maxComputeWorkGroupInvocations = props.limits.maxComputeWorkGroupInvocations;
                r.caps.maxVertexAttributes = props.limits.maxVertexInputAttributes;
                r.caps.maxVertexBufferBindings = props.limits.maxVertexInputBindings;

                auto f10 = pd.getFeatures();
                r.caps.supportsGeometryShader = f10.geometryShader == vk::True;
                r.caps.supportsMultiDrawIndirect = f10.multiDrawIndirect == vk::True;
                r.caps.supportsSparseBinding = f10.sparseBinding == vk::True;
                r.caps.supportsCompute = true;
                
                auto f12 = pd.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features>().get<vk::PhysicalDeviceVulkan12Features>();
                r.caps.supportsBufferDeviceAddress = f12.bufferDeviceAddress == vk::True;
                r.caps.supportsTimelineSemaphore = f12.timelineSemaphore == vk::True;
                r.caps.supportsBindless = f12.descriptorIndexing == vk::True;

                auto f13 = pd.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>().get<vk::PhysicalDeviceVulkan13Features>();
                r.caps.supportsDynamicRendering = f13.dynamicRendering == vk::True;
                r.caps.supportsTessellation = f10.tessellationShader == vk::True;

                auto availableExts = pd.enumerateDeviceExtensionProperties();
                auto hasExt = [&](const char* name) {
                    return std::any_of(availableExts.begin(), availableExts.end(), [&](const auto& e) { return std::string(e.extensionName) == name; });
                };
                
                r.caps.supportsMeshShaders = hasExt(vk::EXTMeshShaderExtensionName);
                r.caps.supportsRayTracing = hasExt(vk::KHRAccelerationStructureExtensionName) && hasExt(vk::KHRRayTracingPipelineExtensionName);
                break;
            }
        }
        
        if (found13) r.available = true;
        else r.reason = "No Vulkan 1.3 compatible GPU found";
    } catch (const std::exception& e) {
        r.reason = e.what();
    }
    
    return r;
}

} // namespace Eng::service