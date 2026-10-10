#include "client/Render/VulkanAPI.h"

#include "client/Window.h"
#include "core/Log.h"

#include <SDL3/SDL_vulkan.h>
#include <SDL3_image/SDL_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <iostream>
#include <stdexcept>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <array>
#include <limits>
#include <algorithm>

namespace Eng::client {

VulkanAPI::VulkanAPI() = default;

VulkanAPI::~VulkanAPI() {
    try { Shutdown(); }
    catch (...) {
        // 析构阶段不抛，只记录
        std::fputs("[Vulkan] Shutdown threw exception\n", stderr);
    }
}

// ============================================================
// 工具
// ============================================================
std::vector<char> VulkanAPI::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }
    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return buffer;
}

vk::raii::ShaderModule VulkanAPI::createShaderModule(const std::vector<char>& code) const {
    vk::ShaderModuleCreateInfo ci{
        .codeSize = code.size(),
        .pCode    = reinterpret_cast<const uint32_t*>(code.data())
    };
    return vk::raii::ShaderModule(m_device, ci);
}

// ============================================================
// Initialize
// ============================================================
void VulkanAPI::Initialize(int /*width*/, int /*height*/, Window* window) {
    m_window = window;

    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createVmaAllocator();
    createCommandPool();
    createSwapChain();
    createImageViews();
    createDepthResources();
    createDescriptorPool();
    createGlobalDescriptors();
    createFullscreenPipeline();
    createSkyboxMesh();
    createCommandBuffers();
    createSyncObjects();
}

// ============================================================
// Instance
// ============================================================
void VulkanAPI::createInstance() {
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("volkInitialize failed");
    }

    vk::ApplicationInfo appInfo{
        .pApplicationName   = "Mycraft",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "MycraftEngine",
        .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion         = vk::ApiVersion14
    };

    Uint32 extCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    if (!sdlExts) throw std::runtime_error("SDL_Vulkan_GetInstanceExtensions failed");

    std::vector<const char*> exts(sdlExts, sdlExts + extCount);
    if (enableValidationLayers) exts.push_back(vk::EXTDebugUtilsExtensionName);

    std::vector<const char*> layers;
    if (enableValidationLayers) layers.assign(validationLayers.begin(), validationLayers.end());

    vk::InstanceCreateInfo ci{
        .pApplicationInfo        = &appInfo,
        .enabledLayerCount       = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames     = layers.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(exts.size()),
        .ppEnabledExtensionNames = exts.data()
    };

    vk::raii::Context context;
    m_instance = vk::raii::Instance(context, ci);
    volkLoadInstance(*m_instance);
}

namespace {
    VKAPI_ATTR vk::Bool32 VKAPI_CALL vkDebugCb(
        vk::DebugUtilsMessageSeverityFlagBitsEXT      severity,
        vk::DebugUtilsMessageTypeFlagsEXT             /*type*/,
        const vk::DebugUtilsMessengerCallbackDataEXT* data,
        void*                                          userData)
    {
        auto* api = static_cast<VulkanAPI*>(userData);
        if (api) {
            api->logValidation(
                static_cast<VkDebugUtilsMessageSeverityFlagBitsEXT>(severity),
                data->pMessage);
        }
        return vk::False;
    }
}

void VulkanAPI::logValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                               const char* message) {
    std::istringstream iss(message);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            logError(m_logger, "[VulkanValidation] " << line);
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            logWarning(m_logger, "[VulkanValidation] " << line);
        }
    }
}

void VulkanAPI::setupDebugMessenger() {
    if (!enableValidationLayers) return;
    vk::DebugUtilsMessengerCreateInfoEXT ci{
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType     = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
        .pfnUserCallback = &vkDebugCb,
        .pUserData       = this
    };
    m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(ci);
}

void VulkanAPI::createSurface() {
    VkSurfaceKHR raw;
    if (!SDL_Vulkan_CreateSurface(m_window->GetSDLWindow(), *m_instance, nullptr, &raw)) {
        throw std::runtime_error("SDL_Vulkan_CreateSurface failed");
    }
    m_surface = vk::raii::SurfaceKHR(m_instance, raw);
}

// ============================================================
// 物理设备
// ============================================================
bool VulkanAPI::isDeviceSuitable(const vk::raii::PhysicalDevice& pd) {
    if (pd.getProperties().apiVersion < vk::ApiVersion14) return false;

    auto qfs = pd.getQueueFamilyProperties();
    bool hasGfx = std::ranges::any_of(qfs, [](auto& q) {
        return !!(q.queueFlags & vk::QueueFlagBits::eGraphics);
    });
    if (!hasGfx) return false;

    auto feats = pd.getFeatures2<
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();

    auto f11  = feats.get<vk::PhysicalDeviceVulkan11Features>();
    auto f13  = feats.get<vk::PhysicalDeviceVulkan13Features>();
    auto fEDS = feats.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();

    if (!f11.shaderDrawParameters) {
        logDebug(m_logger, "[Vulkan] GPU missing shaderDrawParameters");
        return false;
    }
    if (!f13.dynamicRendering) {
        logDebug(m_logger, "[Vulkan] GPU missing dynamicRendering");
        return false;
    }
    if (!f13.synchronization2) {
        logDebug(m_logger, "[Vulkan] GPU missing synchronization2");
        return false;
    }
    if (!fEDS.extendedDynamicState) {
        logDebug(m_logger, "[Vulkan] GPU missing extendedDynamicState");
        return false;
    }
    return true;
}

void VulkanAPI::pickPhysicalDevice() {
    auto devices = m_instance.enumeratePhysicalDevices();
    auto it = std::ranges::find_if(devices, [&](auto& pd) { return isDeviceSuitable(pd); });
    if (it == devices.end()) throw std::runtime_error("no suitable GPU");
    m_physicalDevice = *it;
}

void VulkanAPI::createLogicalDevice() {
    auto qfs = m_physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < qfs.size(); ++i) {
        if ((qfs[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
            m_physicalDevice.getSurfaceSupportKHR(i, *m_surface)) {
            m_queueIndex = i;
            break;
        }
    }
    if (m_queueIndex == ~0u) throw std::runtime_error("no graphics+present queue");

    // ==================== 手动连 pNext 链（不用 StructureChain） ====================
    vk::PhysicalDeviceFeatures2 f2{};
    f2.sType = vk::StructureType::ePhysicalDeviceFeatures2;

    vk::PhysicalDeviceVulkan11Features f11{};
    f11.sType = vk::StructureType::ePhysicalDeviceVulkan11Features;
    f11.shaderDrawParameters = vk::True;

    vk::PhysicalDeviceVulkan13Features f13{};
    f13.sType = vk::StructureType::ePhysicalDeviceVulkan13Features;
    f13.synchronization2 = vk::True;
    f13.dynamicRendering = vk::True;

    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT fEDS{};
    fEDS.sType = vk::StructureType::ePhysicalDeviceExtendedDynamicStateFeaturesEXT;
    fEDS.extendedDynamicState = vk::True;


    // 调整 pNext 链
    f2.pNext   = &f11;
    f11.pNext  = &f13;
    f13.pNext  = &fEDS;
    fEDS.pNext = nullptr;

    // ==================== 纯 C 结构构造 device create ====================
    float prio = 1.0f;

    VkDeviceQueueCreateInfo cQci{};
    cQci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    cQci.queueFamilyIndex = m_queueIndex;
    cQci.queueCount       = 1;
    cQci.pQueuePriorities = &prio;

    VkDeviceCreateInfo cDci{};
    cDci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    cDci.pNext                   = &f2;               // ★ 直接指向链条头
    cDci.queueCreateInfoCount    = 1;
    cDci.pQueueCreateInfos       = &cQci;
    cDci.enabledExtensionCount   = static_cast<uint32_t>(requiredDeviceExtension.size());
    cDci.ppEnabledExtensionNames = requiredDeviceExtension.data();

    // ★ raw vkCreateDevice
    VkDevice rawDevice = VK_NULL_HANDLE;
    VkResult res = vkCreateDevice(*m_physicalDevice, &cDci, nullptr, &rawDevice);
    if (res != VK_SUCCESS) {
        logError(m_logger, "[Vulkan] vkCreateDevice failed: " << (int)res);
        throw std::runtime_error(std::format("vkCreateDevice failed: result={}", (int)res));
    }

    // raii 接管 raw handle
    m_device = vk::raii::Device(m_physicalDevice, rawDevice);
    volkLoadDevice(*m_device);
    m_queue  = vk::raii::Queue(m_device, m_queueIndex, 0);
}

void VulkanAPI::createVmaAllocator() {
    VmaVulkanFunctions vkFuncs{};
    vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vkFuncs.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo ci{};
    ci.physicalDevice   = *m_physicalDevice;
    ci.device           = *m_device;
    ci.instance         = *m_instance;
    ci.vulkanApiVersion = vk::ApiVersion14;
    ci.pVulkanFunctions = &vkFuncs;

    if (vmaCreateAllocator(&ci, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("vmaCreateAllocator failed");
    }
}

void VulkanAPI::createFullscreenPipeline() {
    m_fullscreenVert = createShaderModule(readFile("assets/shaders/build/post/vert.vk.spv"));
    m_fullscreenFrag = createShaderModule(readFile("assets/shaders/build/post/frag.vk.spv"));

    vk::PipelineShaderStageCreateInfo stages[] = {
        {.stage = vk::ShaderStageFlagBits::eVertex,   .module = *m_fullscreenVert, .pName = "main"},
        {.stage = vk::ShaderStageFlagBits::eFragment, .module = *m_fullscreenFrag, .pName = "main"}
    };

    vk::PipelineVertexInputStateCreateInfo vi{};   // 空 —— 顶点从 SV_VertexID 来
    vk::PipelineInputAssemblyStateCreateInfo ia{.topology = vk::PrimitiveTopology::eTriangleList};

    std::vector<vk::DynamicState> dynStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dyn{
        .dynamicStateCount = static_cast<uint32_t>(dynStates.size()),
        .pDynamicStates = dynStates.data()
    };

    vk::PipelineViewportStateCreateInfo vps{.viewportCount = 1, .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rast{
        .depthClampEnable = vk::False, .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f
    };
    vk::PipelineMultisampleStateCreateInfo ms{
        .rasterizationSamples = vk::SampleCountFlagBits::e1
    };
    vk::PipelineColorBlendAttachmentState blendAtt{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };
    vk::PipelineColorBlendStateCreateInfo blend{
        .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &blendAtt
    };

    vk::PipelineDepthStencilStateCreateInfo ds{
        .depthTestEnable       = vk::False,   // ★ fullscreen 不做深度测试
        .depthWriteEnable      = vk::False,
        .depthCompareOp        = vk::CompareOp::eAlways,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable     = vk::False
    };

    // ★ 用一个 setLayout —— m_textureSetLayout（sampler2D）
    // 在这个 pipeline 里它就是 set 0
    vk::PipelineLayoutCreateInfo plci{
        .setLayoutCount = 1,
        .pSetLayouts = &*m_textureSetLayout
    };
    m_fullscreenLayout = vk::raii::PipelineLayout(m_device, plci);

        // ==================== 手动 C 结构体（同 createShaderInternal）====================
    VkFormat colorFmt = static_cast<VkFormat>(m_swapChainSurfaceFormat.format);
    VkFormat depthFmt = static_cast<VkFormat>(findDepthFormat());

    VkPipelineRenderingCreateInfo priC{};
    priC.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    priC.pNext                   = nullptr;
    priC.viewMask                = 0;
    priC.colorAttachmentCount    = 1;
    priC.pColorAttachmentFormats = &colorFmt;
    priC.depthAttachmentFormat   = depthFmt;
    priC.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo gpciC{};
    gpciC.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpciC.pNext               = &priC;
    gpciC.flags               = 0;
    gpciC.stageCount          = 2;
    gpciC.pStages             = reinterpret_cast<VkPipelineShaderStageCreateInfo const*>(stages);
    gpciC.pVertexInputState   = reinterpret_cast<VkPipelineVertexInputStateCreateInfo const*>(&vi);
    gpciC.pInputAssemblyState = reinterpret_cast<VkPipelineInputAssemblyStateCreateInfo const*>(&ia);
    gpciC.pTessellationState  = nullptr;
    gpciC.pViewportState      = reinterpret_cast<VkPipelineViewportStateCreateInfo const*>(&vps);
    gpciC.pRasterizationState = reinterpret_cast<VkPipelineRasterizationStateCreateInfo const*>(&rast);
    gpciC.pMultisampleState   = reinterpret_cast<VkPipelineMultisampleStateCreateInfo const*>(&ms);
    gpciC.pDepthStencilState  = reinterpret_cast<VkPipelineDepthStencilStateCreateInfo const*>(&ds);
    gpciC.pColorBlendState    = reinterpret_cast<VkPipelineColorBlendStateCreateInfo const*>(&blend);
    gpciC.pDynamicState       = reinterpret_cast<VkPipelineDynamicStateCreateInfo const*>(&dyn);
    gpciC.layout              = *m_fullscreenLayout;
    gpciC.renderPass          = VK_NULL_HANDLE;
    gpciC.subpass             = 0;
    gpciC.basePipelineHandle  = VK_NULL_HANDLE;
    gpciC.basePipelineIndex   = -1;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VkResult res = vkCreateGraphicsPipelines(*m_device, VK_NULL_HANDLE, 1,
                                              &gpciC, nullptr, &rawPipeline);
    if (res != VK_SUCCESS) {
        logError(m_logger, "[Vulkan] vkCreateGraphicsPipelines(fullscreen) failed: " << (int)res);
        throw std::runtime_error(std::format("vkCreateGraphicsPipelines(fullscreen) failed: result={}", (int)res));
    }
    m_fullscreenPipeline = vk::raii::Pipeline(m_device, rawPipeline);
}

// ============================================================
// Swapchain
// ============================================================
vk::SurfaceFormatKHR VulkanAPI::chooseSwapSurfaceFormat(
    const std::vector<vk::SurfaceFormatKHR>& formats) {
    auto it = std::ranges::find_if(formats, [](auto& f) {
        return f.format == vk::Format::eB8G8R8A8Unorm &&
               f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    return it != formats.end() ? *it : formats[0];
}

vk::PresentModeKHR VulkanAPI::chooseSwapPresentMode(
        const std::vector<vk::PresentModeKHR>& modes) {
    if (m_windowConfig.vsync) return vk::PresentModeKHR::eFifo;   // 必支持
    for (auto m : modes) if (m == vk::PresentModeKHR::eMailbox) return m;
    return vk::PresentModeKHR::eImmediate;
}

vk::Extent2D VulkanAPI::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return caps.currentExtent;
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
    return {
        std::clamp<uint32_t>(w, caps.minImageExtent.width,  caps.maxImageExtent.width),
        std::clamp<uint32_t>(h, caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}

uint32_t VulkanAPI::chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& caps) {
    uint32_t n = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount > 0 && caps.maxImageCount < n) n = caps.maxImageCount;
    return n;
}

void VulkanAPI::createSwapChain() {
    auto caps    = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);
    auto formats = m_physicalDevice.getSurfaceFormatsKHR(*m_surface);
    auto modes   = m_physicalDevice.getSurfacePresentModesKHR(*m_surface);

    m_swapChainSurfaceFormat = chooseSwapSurfaceFormat(formats);
    m_swapChainExtent        = chooseSwapExtent(caps);

    vk::SwapchainCreateInfoKHR ci{
        .surface          = *m_surface,
        .minImageCount    = chooseSwapMinImageCount(caps),
        .imageFormat      = m_swapChainSurfaceFormat.format,
        .imageColorSpace  = m_swapChainSurfaceFormat.colorSpace,
        .imageExtent      = m_swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode      = chooseSwapPresentMode(modes),
        .clipped          = true,
        .oldSwapchain     = nullptr
    };
    m_swapChain       = vk::raii::SwapchainKHR(m_device, ci);
    m_swapChainImages = m_swapChain.getImages();
}

vk::raii::ImageView VulkanAPI::createImageView(vk::Image image, vk::Format format,
                                                vk::ImageAspectFlags aspect) {
    vk::ImageViewCreateInfo ci{
        .image    = image,
        .viewType = vk::ImageViewType::e2D,
        .format   = format,
        .subresourceRange = { aspect, 0, 1, 0, 1 }
    };
    return vk::raii::ImageView(m_device, ci);
}

void VulkanAPI::createImageViews() {
    m_swapChainImageViews.clear();
    m_swapChainImageViews.reserve(m_swapChainImages.size());
    for (auto& img : m_swapChainImages) {
        m_swapChainImageViews.emplace_back(
            createImageView(img, m_swapChainSurfaceFormat.format,
                            vk::ImageAspectFlagBits::eColor));
    }
    // 每个 swapchain image 首次使用前是 Undefined
    m_swapChainFirstUse.assign(m_swapChainImages.size(), true);
}

// ============================================================
// 深度
// ============================================================
vk::Format VulkanAPI::findSupportedFormat(const std::vector<vk::Format>& candidates,
                                           vk::ImageTiling tiling,
                                           vk::FormatFeatureFlags features) {
    for (auto f : candidates) {
        auto props = m_physicalDevice.getFormatProperties(f);
        if ((tiling == vk::ImageTiling::eOptimal &&
             (props.optimalTilingFeatures & features) == features)) {
            return f;
        }
    }
    throw std::runtime_error("no supported format");
}

vk::Format VulkanAPI::findDepthFormat() {
    return findSupportedFormat(
        { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

uint32_t VulkanAPI::findMemoryType(uint32_t filter, vk::MemoryPropertyFlags props) {
    auto mp = m_physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((filter & (1 << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("no suitable memory type");
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory> VulkanAPI::createImage(uint32_t w, uint32_t h, vk::Format format,
                        vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                        vk::MemoryPropertyFlags props) {
    vk::ImageCreateInfo ci{
        .imageType   = vk::ImageType::e2D,
        .format      = format,
        .extent      = { w, h, 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = vk::SampleCountFlagBits::e1,
        .tiling      = tiling,
        .usage       = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };
    vk::raii::Image image(m_device, ci);
    auto req = image.getMemoryRequirements();
    vk::MemoryAllocateInfo ai{
        .allocationSize  = req.size,
        .memoryTypeIndex = findMemoryType(req.memoryTypeBits, props)
    };
    vk::raii::DeviceMemory mem(m_device, ai);
    image.bindMemory(*mem, 0);
    return { std::move(image), std::move(mem) };
}

void VulkanAPI::createDepthResources() {
    auto fmt = findDepthFormat();
    std::tie(m_depthImage, m_depthImageMemory) = createImage(
        m_swapChainExtent.width, m_swapChainExtent.height, fmt,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_depthImageView = createImageView(*m_depthImage, fmt, vk::ImageAspectFlagBits::eDepth);

    // 一次性转到 DepthAttachmentOptimal —— 之后永远保持
    {
        auto cmd = beginSingleTimeCommands();
        transition_image_layout(cmd, *m_depthImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
            {}, vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::ImageAspectFlagBits::eDepth);
        endSingleTimeCommands(std::move(cmd));
    }
}

// ============================================================
// 布局转换
// ============================================================
void VulkanAPI::transition_image_layout(const vk::raii::CommandBuffer& cmd,
                                         vk::Image image,
                                         vk::ImageLayout oldL, vk::ImageLayout newL,
                                         vk::AccessFlags2 srcA, vk::AccessFlags2 dstA,
                                         vk::PipelineStageFlags2 srcS,
                                         vk::PipelineStageFlags2 dstS,
                                         vk::ImageAspectFlags aspect) {
    vk::ImageMemoryBarrier2 b{
        .srcStageMask        = srcS,
        .srcAccessMask       = srcA,
        .dstStageMask        = dstS,
        .dstAccessMask       = dstA,
        .oldLayout           = oldL,
        .newLayout           = newL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = { aspect, 0, 1, 0, 1 }
    };
    cmd.pipelineBarrier2({ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b });
}

// ============================================================
// Descriptor / UBO
// ============================================================
void VulkanAPI::createDescriptorPool() {
    std::array<vk::DescriptorPoolSize, 2> sizes{{
        { .type = vk::DescriptorType::eUniformBuffer,        .descriptorCount = MAX_FRAMES_IN_FLIGHT },
        { .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 128 }
    }};
    vk::DescriptorPoolCreateInfo ci{
        .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets       = 1024,
        .poolSizeCount = static_cast<uint32_t>(sizes.size()),
        .pPoolSizes    = sizes.data()
    };
    m_descriptorPool = vk::raii::DescriptorPool(m_device, ci);
}

void VulkanAPI::createGlobalDescriptors() {
    // ============ set 0: 只含 UBO ============
    vk::DescriptorSetLayoutBinding uboBinding{
        .binding         = 0,
        .descriptorType  = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags      = vk::ShaderStageFlagBits::eVertex |
                           vk::ShaderStageFlagBits::eFragment
    };
    vk::DescriptorSetLayoutCreateInfo slci{
        .bindingCount = 1,
        .pBindings    = &uboBinding
    };
    m_globalSetLayout = vk::raii::DescriptorSetLayout(m_device, slci);

    // ============ set 1: Texture ============
    vk::DescriptorSetLayoutBinding texBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };
    vk::DescriptorSetLayoutCreateInfo texSlci{
        .bindingCount = 1, .pBindings = &texBinding
    };
    m_textureSetLayout = vk::raii::DescriptorSetLayout(m_device, texSlci);

    // ---- 后面每帧一份 UBO + set 的逻辑完全不变 ----
    m_globalUBOs.clear();
    m_globalUBOAllocs.clear();
    m_globalUBOMapped.clear();
    m_globalSets.clear();

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = sizeof(GlobalUBOData);
        bci.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer raw = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(m_allocator, &bci, &ai, &raw, &alloc, &info) != VK_SUCCESS)
            throw std::runtime_error("vmaCreateBuffer (UBO) failed");

        m_globalUBOs.push_back(raw);
        m_globalUBOAllocs.push_back(alloc);
        m_globalUBOMapped.push_back(info.pMappedData);

        vk::DescriptorSetAllocateInfo dsai{
            .descriptorPool = m_descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &*m_globalSetLayout
        };
        auto sets = m_device.allocateDescriptorSets(dsai);
        m_globalSets.push_back(std::move(sets[0]));

        vk::DescriptorBufferInfo dbi{ raw, 0, sizeof(GlobalUBOData) };
        vk::WriteDescriptorSet write{
            .dstSet = *m_globalSets[i],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &dbi
        };
        m_device.updateDescriptorSets(write, {});
    }
}

// ============================================================
// 命令 / 同步
// ============================================================
void VulkanAPI::createCommandPool() {
    vk::CommandPoolCreateInfo ci{
        .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = m_queueIndex
    };
    m_commandPool = vk::raii::CommandPool(m_device, ci);
}

void VulkanAPI::createCommandBuffers() {
    vk::CommandBufferAllocateInfo ai{
        .commandPool = m_commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT
    };
    m_commandBuffers = vk::raii::CommandBuffers(m_device, ai);
}

void VulkanAPI::createSyncObjects() {
    m_presentCompleteSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();

    for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
        m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_presentCompleteSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
        m_inFlightFences.emplace_back(m_device,
            vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
    }
}

vk::raii::CommandBuffer VulkanAPI::beginSingleTimeCommands() {
    vk::CommandBufferAllocateInfo ai{
        .commandPool = m_commandPool, .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };
    vk::raii::CommandBuffer cmd = std::move(vk::raii::CommandBuffers(m_device, ai).front());
    cmd.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    return cmd;
}

void VulkanAPI::endSingleTimeCommands(vk::raii::CommandBuffer&& cmd) {
    cmd.end();
    vk::SubmitInfo si{ .commandBufferCount = 1, .pCommandBuffers = &*cmd };
    m_queue.submit(si, nullptr);
    m_queue.waitIdle();
}

// ============================================================
// 状态 setter
// ============================================================
void VulkanAPI::SetViewport(int, int, int, int) {}

void VulkanAPI::SetClearColor(float r, float g, float b, float a) {
    m_clearColor[0] = r; m_clearColor[1] = g; m_clearColor[2] = b; m_clearColor[3] = a;
}

void VulkanAPI::Clear() { m_clearPending = true; }

void VulkanAPI::SetViewMatrix(const glm::mat4& v)   { m_globalUBOData.view = v; }
void VulkanAPI::SetProjectionMatrix(const glm::mat4& p) {
    glm::mat4 flipped = p;
    flipped[1][1] *= -1.0f;   // ★ Vulkan Y 翻转
    m_globalUBOData.projection = flipped;
}
void VulkanAPI::SetModelMatrix(const glm::mat4& m)  { m_modelMatrix = m; }
void VulkanAPI::SetLightPosition(const glm::vec3& p){ m_globalUBOData.lightDir = p; }
void VulkanAPI::SetLightColor(const glm::vec3& c, float i) {
    m_globalUBOData.lightColor = c;
    m_globalUBOData.lightIntensity = i;
}
void VulkanAPI::SetLightAmbient(const glm::vec3& a) { m_globalUBOData.lightAmbient = a; }
void VulkanAPI::SetViewPosition(const glm::vec3& p) { m_globalUBOData.viewPos = p; }

// ============================================================
// 帧循环
// ============================================================
void VulkanAPI::BeginFrame() {
    auto fenceRes = m_device.waitForFences(*m_inFlightFences[m_frameIndex], vk::True, UINT64_MAX);
    if (fenceRes != vk::Result::eSuccess) throw std::runtime_error("waitForFences failed");

    // ★ 循环代替递归 —— 处理连续 out-of-date
    bool acquired = false;
    while (!acquired) {
        try {
            auto [res, idx] = m_swapChain.acquireNextImage(
                UINT64_MAX, *m_presentCompleteSemaphores[m_frameIndex], nullptr);
            m_imageIndex = idx;

            if (res == vk::Result::eSuboptimalKHR) {
                recreateSwapChain();
                continue;
            }
            acquired = true;
        } catch (const vk::OutOfDateKHRError&) {
            recreateSwapChain();
        }
    }

    m_device.resetFences(*m_inFlightFences[m_frameIndex]);
    m_commandBuffers[m_frameIndex].reset();
    m_commandBuffers[m_frameIndex].begin({});

    m_frameStarted     = true;
    m_renderPassActive = false;
    m_clearPending     = true;
}

void VulkanAPI::ensureRenderPassActive() {
    if (m_renderPassActive) return;

    auto& cmd = m_commandBuffers[m_frameIndex];

    vk::ImageView colorView;
    vk::ImageView depthView;
    vk::Extent2D  extent;
    bool toFBO = m_pendingFramebuffer.handle != 0;

    if (toFBO) {
        auto fit = m_framebuffers.find(m_pendingFramebuffer.handle);
        if (fit == m_framebuffers.end()) return;

        colorView = *fit->second.colorView;
        depthView = *fit->second.depthView;
        extent    = vk::Extent2D{ static_cast<uint32_t>(fit->second.width),
                        static_cast<uint32_t>(fit->second.height) };

        // FBO color: ShaderReadOnly → ColorAttachment
        transition_image_layout(cmd, fit->second.colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits2::eShaderRead, vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::ImageAspectFlagBits::eColor);
    } else {
        // 渲染到 swapchain
        colorView = m_swapChainImageViews[m_imageIndex];
        depthView = *m_depthImageView;
        extent    = m_swapChainExtent;

        // ★ swapchain image：首次 Undefined，之后 PresentSrcKHR
        vk::ImageLayout oldLayout = m_swapChainFirstUse[m_imageIndex]
            ? vk::ImageLayout::eUndefined
            : vk::ImageLayout::ePresentSrcKHR;
        m_swapChainFirstUse[m_imageIndex] = false;

        transition_image_layout(cmd, m_swapChainImages[m_imageIndex],
            oldLayout, vk::ImageLayout::eColorAttachmentOptimal,
            {}, vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::ImageAspectFlagBits::eColor);

        // ★ depth 已经在 DepthAttachmentOptimal —— 无需 barrier
    }

    vk::ClearValue clearColor{};
    clearColor.color = vk::ClearColorValue(m_clearColor[0], m_clearColor[1],
                                            m_clearColor[2], m_clearColor[3]);
    vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

    vk::RenderingAttachmentInfo colorAtt{
        .imageView = colorView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = m_clearPending ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor
    };
    vk::RenderingAttachmentInfo depthAtt{
        .imageView = depthView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearDepth
    };
    vk::RenderingInfo ri{
        .renderArea = { {0,0}, extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAtt,
        .pDepthAttachment = &depthAtt
    };

    cmd.beginRendering(ri);
    cmd.setViewport(0, vk::Viewport(0, 0, (float)extent.width, (float)extent.height, 0, 1));
    cmd.setScissor(0, vk::Rect2D({0,0}, extent));

    m_renderPassActive = true;
    m_clearPending = false;
}

void VulkanAPI::EndFrame() {
    if (!m_frameStarted) return;

    std::memcpy(m_globalUBOMapped[m_frameIndex], &m_globalUBOData, sizeof(GlobalUBOData));
    vmaFlushAllocation(m_allocator, m_globalUBOAllocs[m_frameIndex], 0, VK_WHOLE_SIZE);

    // ★ ImGui 提交 + 渲染（必须在 render pass 内）
    if (m_imguiInitialized) {
        ImGui::Render();
        ImGuiRenderDrawData();
    }

    // 关所有未关的 render pass
    if (m_renderPassActive) {
        m_commandBuffers[m_frameIndex].endRendering();
        m_renderPassActive = false;
    }

    // FBO color: ColorAttachment → ShaderReadOnly（供下一帧采样）
    if (m_pendingFramebuffer.handle) {
        auto fit = m_framebuffers.find(m_pendingFramebuffer.handle);
        if (fit != m_framebuffers.end()) {
            transition_image_layout(m_commandBuffers[m_frameIndex], fit->second.colorImage,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::ImageAspectFlagBits::eColor);
        }
    }

    // swapchain → Present
    transition_image_layout(m_commandBuffers[m_frameIndex], m_swapChainImages[m_imageIndex],
        vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite, {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eBottomOfPipe,
        vk::ImageAspectFlagBits::eColor);

    m_commandBuffers[m_frameIndex].end();

    vk::PipelineStageFlags waitStage(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    vk::SubmitInfo si{
        .waitSemaphoreCount = 1, .pWaitSemaphores = &*m_presentCompleteSemaphores[m_frameIndex],
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1, .pCommandBuffers = &*m_commandBuffers[m_frameIndex],
        .signalSemaphoreCount = 1, .pSignalSemaphores = &*m_renderFinishedSemaphores[m_imageIndex]
    };
    m_queue.submit(si, *m_inFlightFences[m_frameIndex]);

    vk::PresentInfoKHR pi{
        .waitSemaphoreCount = 1, .pWaitSemaphores = &*m_renderFinishedSemaphores[m_imageIndex],
        .swapchainCount = 1, .pSwapchains = &*m_swapChain,
        .pImageIndices = &m_imageIndex
    };

    bool needRecreate = m_framebufferResized;
    try {
        auto r = m_queue.presentKHR(pi);
        if (r == vk::Result::eSuboptimalKHR) needRecreate = true;
    } catch (const vk::OutOfDateKHRError&) {
        needRecreate = true;
    }

    if (needRecreate) {
        m_framebufferResized = false;
        recreateSwapChain();
    }

    m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    m_frameStarted = false;
}

// ============================================================
// 资源创建
// ============================================================
MeshHandle VulkanAPI::CreateMesh(const MeshData& data) {
    MeshInternal mi;

    if (!data.vertices.empty()) {
        VkDeviceSize size = data.vertices.size() * sizeof(Vertex);
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo info{};
        if (vmaCreateBuffer(m_allocator, &bci, &ai,
                            &mi.vertexBuffer, &mi.vertexAlloc, &info) != VK_SUCCESS)
            throw std::runtime_error("vmaCreateBuffer (vertex) failed");
        std::memcpy(info.pMappedData, data.vertices.data(), size);
        vmaFlushAllocation(m_allocator, mi.vertexAlloc, 0, VK_WHOLE_SIZE);
    }

    if (!data.indices.empty()) {
        VkDeviceSize size = data.indices.size() * sizeof(uint32_t);
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo info{};
        if (vmaCreateBuffer(m_allocator, &bci, &ai,
                            &mi.indexBuffer, &mi.indexAlloc, &info) != VK_SUCCESS)
            throw std::runtime_error("vmaCreateBuffer (index) failed");
        std::memcpy(info.pMappedData, data.indices.data(), size);
        vmaFlushAllocation(m_allocator, mi.indexAlloc, 0, VK_WHOLE_SIZE);
        mi.indexCount = static_cast<uint32_t>(data.indices.size());
    }

    MeshHandle h = m_nextMesh++;
    m_meshes.emplace(h, mi);
    return h;
}

void VulkanAPI::DestroyMesh(MeshHandle h) {
    vkDeviceWaitIdle(*m_device);
    auto it = m_meshes.find(h);
    if (it == m_meshes.end()) return;
    if (it->second.vertexAlloc != VK_NULL_HANDLE)
        vmaDestroyBuffer(m_allocator, it->second.vertexBuffer, it->second.vertexAlloc);
    if (it->second.indexAlloc != VK_NULL_HANDLE)
        vmaDestroyBuffer(m_allocator, it->second.indexBuffer, it->second.indexAlloc);
    m_meshes.erase(it);
}

ShaderHandle VulkanAPI::CreateShader(const std::string& vertPath,
                                      const std::string& fragPath) {
    return createShaderInternal(vertPath, fragPath, /*isSky=*/false);
}

ShaderHandle VulkanAPI::CreateSkybox(const std::string& vertPath,
                                      const std::string& fragPath) {
    return createShaderInternal(vertPath, fragPath, /*isSky=*/true,
                                "main", "main");
}

ShaderHandle VulkanAPI::createShaderInternal(const std::string& vertPath,
                                   const std::string& fragPath,
                                   bool isSky,
                                   const char* vertEntry,
                                   const char* fragEntry) {
    ShaderInternal si;
    si.vertModule = createShaderModule(readFile(vertPath));
    si.fragModule = createShaderModule(readFile(fragPath));

    // ==================== Shader stages ====================
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = *si.vertModule;
    stages[0].pName  = vertEntry;

    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = *si.fragModule;
    stages[1].pName  = fragEntry;

    // ==================== Vertex input ====================
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)   };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)       };
    attrs[3] = { 3, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Vertex, ao)       };

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    if (isSky) {
        vi.vertexAttributeDescriptionCount = 1;
    } else {
        vi.vertexAttributeDescriptionCount = 4;
    }
    vi.pVertexAttributeDescriptions    = attrs;

    // ==================== Input assembly ====================
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // ==================== Dynamic state ====================
    VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynStates;

    // ==================== Viewport ====================
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.scissorCount  = 1;

    // ==================== Rasterization ====================
    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.depthClampEnable        = VK_FALSE;
    rast.rasterizerDiscardEnable = VK_FALSE;
    rast.polygonMode             = VK_POLYGON_MODE_FILL;
    rast.cullMode                = isSky ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    rast.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rast.depthBiasEnable         = VK_FALSE;
    rast.lineWidth               = 1.0f;

    // ==================== Multisample ====================
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ==================== Depth / stencil ====================
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable       = VK_TRUE;
    ds.depthWriteEnable      = isSky ? VK_FALSE : VK_TRUE;
    ds.depthCompareOp        = isSky ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable     = VK_FALSE;

    // ==================== Color blend ====================
    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable         = VK_TRUE;
    blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.logicOpEnable   = VK_FALSE;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAtt;

    // ==================== Pipeline layout（vk:: 保留，逻辑较复杂） ====================
    std::array<vk::DescriptorSetLayout, 2> setLayouts = {
        *m_globalSetLayout, *m_textureSetLayout
    };
    vk::PushConstantRange pcRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
        .offset     = 0,
        .size       = sizeof(glm::mat4)
    };

    vk::PipelineLayoutCreateInfo plci;
    if (isSky) {
        plci = {
            .setLayoutCount         = 1,
            .pSetLayouts            = &*m_globalSetLayout,
            .pushConstantRangeCount = 0
        };
    } else {
        plci = {
            .setLayoutCount         = 2,
            .pSetLayouts            = setLayouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &pcRange
        };
    }
    si.layout = vk::raii::PipelineLayout(m_device, plci);

    // ==================== PipelineRenderingCreateInfo（C） ====================
    VkFormat colorFmt = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFmt = static_cast<VkFormat>(findDepthFormat());

    VkPipelineRenderingCreateInfo priC{};
    priC.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    priC.pNext                   = nullptr;
    priC.viewMask                = 0;
    priC.colorAttachmentCount    = 1;
    priC.pColorAttachmentFormats = &colorFmt;
    priC.depthAttachmentFormat   = depthFmt;
    priC.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    // ==================== GraphicsPipelineCreateInfo（C） ====================
    VkGraphicsPipelineCreateInfo gpciC{};
    gpciC.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpciC.pNext               = &priC;             // ★ C 指针显式链
    gpciC.flags               = 0;
    gpciC.stageCount          = 2;
    gpciC.pStages             = stages;
    gpciC.pVertexInputState   = &vi;
    gpciC.pInputAssemblyState = &ia;
    gpciC.pTessellationState  = nullptr;
    gpciC.pViewportState      = &vps;
    gpciC.pRasterizationState = &rast;
    gpciC.pMultisampleState   = &ms;
    gpciC.pDepthStencilState  = &ds;
    gpciC.pColorBlendState    = &blend;
    gpciC.pDynamicState       = &dyn;
    gpciC.layout              = *si.layout;
    gpciC.renderPass          = VK_NULL_HANDLE;
    gpciC.subpass             = 0;
    gpciC.basePipelineHandle  = VK_NULL_HANDLE;
    gpciC.basePipelineIndex   = -1;

    // ==================== Raw vkCreateGraphicsPipelines ====================
    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VkResult res = vkCreateGraphicsPipelines(*m_device, VK_NULL_HANDLE, 1,
                                              &gpciC, nullptr, &rawPipeline);
    if (res != VK_SUCCESS) {
        logError(m_logger, "[Vulkan] vkCreateGraphicsPipelines failed: " << (int)res);
        throw std::runtime_error(std::format("vkCreateGraphicsPipelines failed: result={}", (int)res));
    }

    si.pipeline = vk::raii::Pipeline(m_device, rawPipeline);

    ShaderHandle h = m_nextShader++;
    m_shaders.emplace(h, std::move(si));

    logInfo(m_logger, "[RenderAPI] Shader created: " << vertPath);

    logDebug(m_logger, "[Vulkan] shader handle=" << m_nextShader
        << " pipeline=0x" << std::hex << (uint64_t)rawPipeline << std::dec
        << " isSky=" << isSky);

    return h;
}

void VulkanAPI::DestroyShader(ShaderHandle h) {
    if (!*m_device) return;
    vkDeviceWaitIdle(*m_device);
    m_shaders.erase(h);
}

// ============================================================
// DrawMesh
// ============================================================
void VulkanAPI::DrawMesh(MeshHandle mesh, ShaderHandle shader, const Material& mat) {
    auto mit = m_meshes.find(mesh);
    auto sit = m_shaders.find(shader);
    if (mit == m_meshes.end() || sit == m_shaders.end()) return;

    ensureRenderPassActive();

    // 找纹理 set；找不到用默认（白色）
    TextureHandle texH = mat.diffuse;
    if (texH == 0) texH = m_defaultTexture;

    vk::DescriptorSet texSet = nullptr;
    auto texIt = m_textures.find(texH);
    if (texIt != m_textures.end()) texSet = *texIt->second.set;
    if (!texSet) return;

    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *sit->second.pipeline);

    std::array<vk::DescriptorSet, 2> sets = {
        *m_globalSets[m_frameIndex],
        texSet
    };
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            *sit->second.layout, 0, sets, {});

    cmd.pushConstants<glm::mat4>(*sit->second.layout,
        vk::ShaderStageFlagBits::eVertex, 0, m_modelMatrix);

    cmd.bindVertexBuffers(0, vk::Buffer(mit->second.vertexBuffer), {0});
    cmd.bindIndexBuffer(vk::Buffer(mit->second.indexBuffer), 0, vk::IndexType::eUint32);
    cmd.drawIndexed(mit->second.indexCount, 1, 0, 0, 0);
}

// ============================================================
// Swapchain 重建
// ============================================================
void VulkanAPI::cleanupSwapChain() {
    m_swapChainImageViews.clear();
    m_swapChain = nullptr;
    m_depthImageView = nullptr;
    m_depthImage = nullptr;
    m_depthImageMemory = nullptr;
}

void VulkanAPI::recreateSwapChain() {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
    while (w == 0 || h == 0) {
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) return;
        if (e.type == SDL_EVENT_QUIT) return;
        SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
    }

    m_device.waitIdle();
    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createDepthResources();

    m_renderFinishedSemaphores.clear();
    m_renderFinishedSemaphores.reserve(m_swapChainImages.size());
    for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
        m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
    }
}

// ============================================================
// Shutdown
// ============================================================
void VulkanAPI::Shutdown() {
    if (!*m_device) return;
    m_device.waitIdle();

    // 释放 sky cube
    if (m_skyCubeAlloc != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_skyCubeBuffer, m_skyCubeAlloc);
        m_skyCubeBuffer = VK_NULL_HANDLE;
        m_skyCubeAlloc  = VK_NULL_HANDLE;
    }

    for (auto& [h, fb] : m_framebuffers) {
        if (fb.colorHandle) m_textures.erase(fb.colorHandle);
        fb.colorView = nullptr;
        fb.depthView = nullptr;
        if (fb.colorAlloc) vmaDestroyImage(m_allocator, fb.colorImage, fb.colorAlloc);
        if (fb.depthAlloc) vmaDestroyImage(m_allocator, fb.depthImage, fb.depthAlloc);
    }
    m_framebuffers.clear();

    for (auto& [h, m] : m_meshes) {
        if (m.vertexAlloc) vmaDestroyBuffer(m_allocator, m.vertexBuffer, m.vertexAlloc);
        if (m.indexAlloc)  vmaDestroyBuffer(m_allocator, m.indexBuffer,  m.indexAlloc);
        if (m.instanceAlloc) vmaDestroyBuffer(m_allocator, m.instanceBuffer, m.instanceAlloc);
    }
    m_meshes.clear();

    for (auto& [h, t] : m_textures) {
        t.view = nullptr;
        t.sampler = nullptr;
        t.set = nullptr;
        if (t.alloc != VK_NULL_HANDLE) {
            vmaDestroyImage(m_allocator, t.image, t.alloc);
        }
    }
    m_textures.clear();
    m_textureCache.clear();

    m_shaders.clear();   // vk::raii 自己管 pipeline / layout / module

    // ★ 释放 UBO
    for (size_t i = 0; i < m_globalUBOs.size(); ++i) {
        if (m_globalUBOAllocs[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_globalUBOs[i], m_globalUBOAllocs[i]);
        }
    }
    m_globalUBOs.clear();
    m_globalUBOAllocs.clear();
    m_globalUBOMapped.clear();
    m_globalSets.clear();

    // ... 剩下的清理（command pool / swapchain / descriptor pool / allocator）...
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);   // ★ 现在安全了
        m_allocator = VK_NULL_HANDLE;
    }
    logInfo(m_logger, "[RenderAPI] Shutdown");
}
TextureHandle VulkanAPI::CreateTexture(const std::string& path) {
    // 缓存
    auto it = m_textureCache.find(path);
    if (it != m_textureCache.end()) return it->second;

    // 用 SDL_image 加载
    SDL_Surface* loaded = IMG_Load(path.c_str());
    if (!loaded) {
        std::cerr << "[Vulkan] IMG_Load failed: " << path << " - "
                  << SDL_GetError() << "\n";
        return 0;
    }
    SDL_Surface* surf = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!surf) return 0;

    TextureHandle h = CreateTextureFromPixels(
        static_cast<const uint8_t*>(surf->pixels), surf->w, surf->h);

    SDL_DestroySurface(surf);

    m_textureCache[path] = h;
    return h;
}

void VulkanAPI::DestroyTexture(TextureHandle h) {
    auto it = m_textures.find(h);
    if (it == m_textures.end()) return;

    it->second.view = nullptr;
    it->second.sampler = nullptr;
    it->second.set = nullptr;
    if (it->second.ownsImage && it->second.alloc != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, it->second.image, it->second.alloc);
    }
    m_textures.erase(it);

    // ★ 清缓存里的悬空映射
    std::erase_if(m_textureCache, [h](auto& kv) { return kv.second == h; });
}

TextureHandle VulkanAPI::CreateTextureFromPixels(const uint8_t* rgba, int w, int h) {
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    // ---------- 1. Staging buffer ----------
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = imageSize;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo info{};
        if (vmaCreateBuffer(m_allocator, &bci, &ai,
                             &stagingBuf, &stagingAlloc, &info) != VK_SUCCESS)
            throw std::runtime_error("staging buffer failed");
        std::memcpy(info.pMappedData, rgba, imageSize);
        vmaFlushAllocation(m_allocator, stagingAlloc, 0, VK_WHOLE_SIZE);
    }

    // ---------- 2. Image ----------
    VkImage rawImage = VK_NULL_HANDLE;
    VmaAllocation imageAlloc = VK_NULL_HANDLE;
    {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (vmaCreateImage(m_allocator, &ici, &ai,
                            &rawImage, &imageAlloc, nullptr) != VK_SUCCESS)
            throw std::runtime_error("vmaCreateImage failed");
    }

    // ---------- 3. 上传 ----------
    {
        auto cmd = beginSingleTimeCommands();

        transition_image_layout(cmd, rawImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            {}, vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::ImageAspectFlagBits::eColor);

        vk::BufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 }
        };
        cmd.copyBufferToImage(stagingBuf, rawImage,
                                vk::ImageLayout::eTransferDstOptimal, region);

        transition_image_layout(cmd, rawImage,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::AccessFlagBits2::eTransferWrite, vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::ImageAspectFlagBits::eColor);

        endSingleTimeCommands(std::move(cmd));
    }

    vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);

    // ---------- 4. View ----------
    vk::raii::ImageView view = createImageView(
        rawImage, vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor);

    // ---------- 5. Sampler ----------
    vk::SamplerCreateInfo sci{
        .magFilter = vk::Filter::eNearest,   // 图集要 NEAREST
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .anisotropyEnable = vk::False,
        .compareEnable = vk::False,
        .minLod = 0.0f, .maxLod = 0.0f,
        .borderColor = vk::BorderColor::eIntOpaqueBlack,
        .unnormalizedCoordinates = vk::False
    };
    vk::raii::Sampler sampler(m_device, sci);

    // ---------- 6. Descriptor set ----------
    vk::DescriptorSetAllocateInfo dsai{
        .descriptorPool = m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &*m_textureSetLayout
    };
    auto sets = m_device.allocateDescriptorSets(dsai);
    vk::raii::DescriptorSet texSet = std::move(sets[0]);

    vk::DescriptorImageInfo imgInfo{
        .sampler = *sampler,
        .imageView = *view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    vk::WriteDescriptorSet write{
        .dstSet = *texSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &imgInfo
    };
    m_device.updateDescriptorSets(write, {});

    // ---------- 7. 存表 ----------
    TextureInternal ti;
    ti.image = rawImage;
    ti.alloc = imageAlloc;
    ti.view = std::move(view);
    ti.sampler = std::move(sampler);
    ti.set = std::move(texSet);

    TextureHandle handle = m_nextTexture++;
    m_textures.emplace(handle, std::move(ti));
    logInfo(m_logger, "[Texture] from pixels: " << w << "x" << h);
    return handle;
}

Framebuffer VulkanAPI::CreateFramebuffer(int width, int height) {
    FramebufferInternal fbi;
    fbi.width  = width;
    fbi.height = height;

    // ---------- Color image ----------
    {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;   // 存线性值
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (vmaCreateImage(m_allocator, &ici, &ai,
                            &fbi.colorImage, &fbi.colorAlloc, nullptr) != VK_SUCCESS)
            throw std::runtime_error("FBO color image failed");
    }

    // ---------- Depth image ----------
    {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.format = VK_FORMAT_D32_SFLOAT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (vmaCreateImage(m_allocator, &ici, &ai,
                            &fbi.depthImage, &fbi.depthAlloc, nullptr) != VK_SUCCESS)
            throw std::runtime_error("FBO depth image failed");
    }

    // ---------- Views ----------
    fbi.colorView = createImageView(fbi.colorImage,
        vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor);
    fbi.depthView = createImageView(fbi.depthImage,
        vk::Format::eD32Sfloat, vk::ImageAspectFlagBits::eDepth);

    // ---------- 一次性 layout 转换 ----------
    {
        auto cmd = beginSingleTimeCommands();

        transition_image_layout(cmd, fbi.colorImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
            {}, vk::AccessFlagBits2::eShaderRead,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::ImageAspectFlagBits::eColor);

        transition_image_layout(cmd, fbi.depthImage,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
            {}, vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::ImageAspectFlagBits::eDepth);

        endSingleTimeCommands(std::move(cmd));
    }

    // ---------- 建 sampler + set 供 DrawFullscreenQuad 用 ----------
    vk::SamplerCreateInfo sci{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .anisotropyEnable = vk::False,
        .compareEnable = vk::False,
        .minLod = 0.0f, .maxLod = 0.0f,
        .borderColor = vk::BorderColor::eFloatOpaqueBlack,
        .unnormalizedCoordinates = vk::False
    };
    fbi.colorSampler = vk::raii::Sampler(m_device, sci);   // ★ 存 FBO 自己

    // Descriptor set
    vk::DescriptorSetAllocateInfo dsai{
        .descriptorPool = m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &*m_textureSetLayout
    };
    auto sets = m_device.allocateDescriptorSets(dsai);
    vk::raii::DescriptorSet set = std::move(sets[0]);

    vk::DescriptorImageInfo imgInfo{
        .sampler = *fbi.colorSampler,
        .imageView = *fbi.colorView,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    vk::WriteDescriptorSet write{
        .dstSet = *set, .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &imgInfo
    };
    m_device.updateDescriptorSets(write, {});

    // 注册纹理 entry —— 只存 set，不存 view/sampler
    TextureInternal ti;
    ti.image     = fbi.colorImage;
    ti.alloc     = VK_NULL_HANDLE;
    ti.view      = nullptr;            // ★
    ti.sampler   = nullptr;            // ★
    ti.set       = std::move(set);
    ti.ownsImage = false;

    TextureHandle texHandle = m_nextTexture++;
    m_textures.emplace(texHandle, std::move(ti));
    fbi.colorHandle = texHandle;

    FramebufferHandle fh = m_nextFramebuffer++;
    m_framebuffers.emplace(fh, std::move(fbi));

    Framebuffer fb;
    fb.handle = fh;
    fb.width  = width;
    fb.height = height;

    logDebug(m_logger, "[RenderAPI] Framebuffer created: " << width << "x" << height);

    return fb;
}

void VulkanAPI::BindFramebuffer(const Framebuffer& fb) {
    if (!m_frameStarted) return;   // ★
    m_pendingFramebuffer = fb;
    if (m_renderPassActive) {
        m_commandBuffers[m_frameIndex].endRendering();
        m_renderPassActive = false;
    }
}

void VulkanAPI::UnbindFramebuffer() {
    if (m_renderPassActive) {
        m_commandBuffers[m_frameIndex].endRendering();
        m_renderPassActive = false;
    }

    // ★ FBO color: ColorAttachment → ShaderReadOnly（供后处理采样）
    if (m_pendingFramebuffer.handle) {
        auto fit = m_framebuffers.find(m_pendingFramebuffer.handle);
        if (fit != m_framebuffers.end()) {
            transition_image_layout(m_commandBuffers[m_frameIndex],
                fit->second.colorImage,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::ImageAspectFlagBits::eColor);
        }
    }

    m_pendingFramebuffer = {};
}

TextureHandle VulkanAPI::GetFramebufferTexture(const Framebuffer& fb) const {
    auto it = m_framebuffers.find(fb.handle);
    return it != m_framebuffers.end() ? it->second.colorHandle : 0;
}

void VulkanAPI::DestroyFramebuffer(const Framebuffer& fb) {
    if (m_pendingFramebuffer.handle == fb.handle) {
        m_pendingFramebuffer = {};
    }

    auto it = m_framebuffers.find(fb.handle);
    if (it == m_framebuffers.end()) return;

    if (it->second.colorHandle) {
        m_textures.erase(it->second.colorHandle);
    }

    it->second.colorView = nullptr;
    it->second.colorSampler = nullptr;
    it->second.depthView = nullptr;

    if (it->second.colorAlloc) vmaDestroyImage(m_allocator, it->second.colorImage, it->second.colorAlloc);
    if (it->second.depthAlloc) vmaDestroyImage(m_allocator, it->second.depthImage, it->second.depthAlloc);

    m_framebuffers.erase(it);
}

TextureHandle VulkanAPI::CreateTextureFromMemory(const aiTexture* /*embedded*/) {
    // TODO: 从 assimp 内嵌纹理解出像素
    return 0;
}

void VulkanAPI::DrawFullscreenQuad(TextureHandle texHandle) {
    if (!*m_fullscreenPipeline) return;   // ★

    auto texIt = m_textures.find(texHandle);
    if (texIt == m_textures.end()) return;

    ensureRenderPassActive();

    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_fullscreenPipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        *m_fullscreenLayout, 0, *texIt->second.set, {});
    cmd.draw(3, 1, 0, 0);
}

void VulkanAPI::createSkyboxMesh() {
    const float s = 1.0f;

    // 一个 V 的 helper —— 只有 position 有意义，其他字段填默认
    auto V = [](float x, float y, float z) -> Vertex {
        Vertex v;
        v.position = { x, y, z };
        v.normal   = { 0.0f, 0.0f, 0.0f };
        v.uv       = { 0.0f, 0.0f };
        v.ao       = 1.0f;
        return v;
    };

    std::vector<Vertex> verts = {
        // +X
        V( s,-s,-s), V( s,-s, s), V( s, s, s),
        V( s, s, s), V( s, s,-s), V( s,-s,-s),
        // -X
        V(-s,-s, s), V(-s,-s,-s), V(-s, s,-s),
        V(-s, s,-s), V(-s, s, s), V(-s,-s, s),
        // +Y
        V(-s, s,-s), V( s, s,-s), V( s, s, s),
        V( s, s, s), V(-s, s, s), V(-s, s,-s),
        // -Y
        V(-s,-s, s), V( s,-s, s), V( s,-s,-s),
        V( s,-s,-s), V(-s,-s,-s), V(-s,-s, s),
        // +Z
        V(-s,-s, s), V( s,-s, s), V( s, s, s),
        V( s, s, s), V(-s, s, s), V(-s,-s, s),
        // -Z
        V( s,-s,-s), V(-s,-s,-s), V(-s, s,-s),
        V(-s, s,-s), V( s, s,-s), V( s,-s,-s),
    };

    VkDeviceSize size = verts.size() * sizeof(Vertex);

    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = size;
    bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo info{};
    if (vmaCreateBuffer(m_allocator, &bci, &ai,
                        &m_skyCubeBuffer, &m_skyCubeAlloc, &info) != VK_SUCCESS) {
        throw std::runtime_error("sky cube buffer creation failed");
    }
    std::memcpy(info.pMappedData, verts.data(), size);
    vmaFlushAllocation(m_allocator, m_skyCubeAlloc, 0, VK_WHOLE_SIZE);   // ★
}
void VulkanAPI::DrawSkybox(ShaderHandle shader) {
    auto sit = m_shaders.find(shader);
    if (sit == m_shaders.end()) return;
    if (m_skyCubeBuffer == VK_NULL_HANDLE) return;

    ensureRenderPassActive();

    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *sit->second.pipeline);

    // ★ sky pipeline layout 只含 m_globalSetLayout —— 只绑 1 个 set
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        *sit->second.layout, 0, *m_globalSets[m_frameIndex], {});

    cmd.bindVertexBuffers(0, vk::Buffer(m_skyCubeBuffer), {0});
    cmd.draw(36, 1, 0, 0);
}
void VulkanAPI::SetUniform(ShaderHandle, const std::string& name, float value) {
    if      (name == "uAOStrength") m_globalUBOData.aoStrength = value;
    else if (name == "uTimeOfDay")  m_globalUBOData.timeOfDay  = value;
    // 其他忽略
}

// ============================================================
// Stub —— 以后实现
// ============================================================

MeshHandle VulkanAPI::CreateMeshInstance(const MeshData& data) { return CreateMesh(data); }

Model VulkanAPI::LoadModel(const std::string&, bool) { return {}; }

void VulkanAPI::DrawMeshInstanced(MeshHandle, ShaderHandle, const Material&,
                                  const std::vector<glm::mat4>&) {}

void VulkanAPI::SetUniform(ShaderHandle, const std::string&, const glm::mat4&) {}
void VulkanAPI::SetUniform(ShaderHandle, const std::string&, const glm::vec3&) {}
void VulkanAPI::SetUniform(ShaderHandle, const std::string&, int) {}

// ============================================================
// ImGui 后端（动态渲染）
// ============================================================

bool VulkanAPI::InitImGuiBackend()
{
    if (m_imguiInitialized)
        return true;

    // raii → 裸 handle
    VkInstance       vkInstance = *m_instance;
    VkPhysicalDevice vkPhysical = *m_physicalDevice;
    VkDevice         vkDevice   = *m_device;
    VkQueue          vkQueue    = *m_queue;
    const uint32_t   imageCount = static_cast<uint32_t>(m_swapChainImages.size());
    const VkFormat   colorFmt   = static_cast<VkFormat>(m_swapChainSurfaceFormat.format);

    // --- ImGui 上下文 ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // --- SDL3 平台后端 ---
    if (!ImGui_ImplSDL3_InitForVulkan(m_window->GetSDLWindow())) {
        logError(m_logger, "ImGui_ImplSDL3_InitForVulkan failed");
        ImGui::DestroyContext();
        return false;
    }

    // --- ImGui 专用描述符池 ---
    // 新版 ImGui 只需 COMBINED_IMAGE_SAMPLER，但为了兼容 AddTexture 等扩展功能，
    // 把常见的都放进去，池子大一点不会痛
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER,                 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,           1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,           1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          1000 },
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets       = 1000 * IM_ARRAYSIZE(poolSizes);
    poolInfo.poolSizeCount = IM_ARRAYSIZE(poolSizes);
    poolInfo.pPoolSizes    = poolSizes;

    if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr,
                               &m_imguiDescriptorPool) != VK_SUCCESS) {
        logError(m_logger, "ImGui descriptor pool creation failed");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    // --- ImGui Vulkan 渲染器后端 ---
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion     = VK_API_VERSION_1_4;   // 你 instance 用的版本
    initInfo.Instance       = vkInstance;
    initInfo.PhysicalDevice = vkPhysical;
    initInfo.Device         = vkDevice;
    initInfo.QueueFamily    = m_queueIndex;
    initInfo.Queue          = vkQueue;
    initInfo.DescriptorPool = m_imguiDescriptorPool;
    initInfo.MinImageCount  = imageCount;
    initInfo.ImageCount     = imageCount;

    // 动态渲染
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFmt;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        logError(m_logger, "[ImGui] ImGui_ImplVulkan_Init failed");
        vkDestroyDescriptorPool(vkDevice, m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    m_imguiInitialized = true;
    logInfo(m_logger, "[RenderAPI] ImGui backend initialized");
    return true;
}

void VulkanAPI::ShutdownImGuiBackend() {
    if (ImGui::GetCurrentContext() == nullptr) return;

    VkDevice vkDevice = *m_device;
    vkDeviceWaitIdle(vkDevice);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();

    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkDevice, m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }

    ImGui::DestroyContext();
    m_imguiInitialized = false;
    logInfo(m_logger, "[RenderAPI] ImGui backend shutdown");
}

void VulkanAPI::ImGuiNewFrame()
{
    if (!m_imguiInitialized)
        return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

void VulkanAPI::ImGuiRenderDrawData() {
    if (!m_imguiInitialized) return;

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->CmdListsCount == 0) return;

    VkCommandBuffer cmd = *m_commandBuffers[m_frameIndex];

    // ★ 1. 如果当前有 render pass，先关掉
    if (m_renderPassActive) {
        vkCmdEndRendering(cmd);
        m_renderPassActive = false;
    }

    // ★ 2. 开一个只含 swapchain color、无 depth 的 render pass
    VkRenderingAttachmentInfo colorAtt{};
    colorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAtt.pNext       = nullptr;
    colorAtt.imageView   = static_cast<VkImageView>(*m_swapChainImageViews[m_imageIndex]);
    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.resolveMode = VK_RESOLVE_MODE_NONE;
    colorAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD;   // 保留后处理结果
    colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo ri{};
    ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.pNext                = nullptr;
    ri.flags                = 0;
    ri.renderArea.offset    = { 0, 0 };
    ri.renderArea.extent    = { m_swapChainExtent.width, m_swapChainExtent.height };
    ri.layerCount           = 1;
    ri.viewMask             = 0;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &colorAtt;
    ri.pDepthAttachment     = nullptr;    // ★ 无 depth
    ri.pStencilAttachment   = nullptr;

    vkCmdBeginRendering(cmd, &ri);
    ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    vkCmdEndRendering(cmd);
}

[[nodiscard]] DeviceInfo VulkanAPI::GetDeviceInfo() const {
    DeviceInfo info;
    info.backend = "Vulkan";

    if (!*m_physicalDevice) {
        info.deviceName = "(uninitialized)";
        return info;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(*m_physicalDevice, &props);

    VkPhysicalDeviceDriverProperties drv{};
    drv.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    drv.pNext = nullptr;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &drv;
    vkGetPhysicalDeviceProperties2(*m_physicalDevice, &props2);

    info.deviceName = props.deviceName;

    // vendorID 转可读厂商名
    switch (props.vendorID) {
        case 0x10DE: info.vendor = "NVIDIA";    break;
        case 0x1002: info.vendor = "AMD";       break;
        case 0x8086: info.vendor = "Intel";     break;
        case 0x13B5: info.vendor = "ARM";       break;
        case 0x5143: info.vendor = "Qualcomm";  break;
        default:     info.vendor = "0x" + [&]{
            char buf[16]; std::snprintf(buf, sizeof(buf), "%04X", props.vendorID);
            return std::string(buf);
        }();
    }

    // apiVersion：major.minor.patch
    uint32_t api = props.apiVersion;
    info.apiVersion = std::to_string(VK_API_VERSION_MAJOR(api)) + "."
                    + std::to_string(VK_API_VERSION_MINOR(api)) + "."
                    + std::to_string(VK_API_VERSION_PATCH(api));

    // driverVersion：厂商私有编码，直接给 raw + 解析
    info.driverVersion = drv.driverInfo;

    // Vulkan 的 SPIR-V 版本没法从 device props 拿，写死
    info.shadingLanguage = "SPIR-V 1.4";

    info.extra = std::string(drv.driverName) + " / " + drv.driverInfo;

    return info;
}

void VulkanAPI::ApplySettings(const WindowConfig& win, const RenderConfig& render) {
    // 只有 VSync 走这里 —— 全屏由 MyGame 直接调 SetFullscreen
    if (m_windowConfig.vsync != win.vsync) {
        m_windowConfig.vsync = win.vsync;
        recreateSwapChain();
    }

    m_globalUBOData.aoStrength = render.m_aoStrength;
}

void VulkanAPI::WaitIdle() {
    if (*m_device) {              // ← 成员名按你实际的改
        m_device.waitIdle();     // vk::raii::Device 写法
        // 裸 VkDevice 用：vkDeviceWaitIdle(m_device);
    }
}

}