#include <vulkan/vulkan.hpp>
#include "client/Render/VulkanAPI.h"
#include <iostream>
namespace mycraft {

VulkanAPI::VulkanAPI() {}

bool VulkanAPI::Init(SDL_Window* window, int width, int height) {
    m_width = width;
    m_height = height;
    getExtensions();
    createInstance();
    pickupPhysicalDevice();
    queryQueueFamilyIndices();
    createSurface(window);
    createDevice();
    getQueues();
    createSwapchain();
    return true;
}

VulkanAPI::~VulkanAPI() {
    device.destroySwapchainKHR(swapchain);
    device.destroy();
    instance.destroy();
}

void VulkanAPI::getExtensions() {
    unsigned int extCount;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&extCount);
    if (!sdlExtensions) {
        throw std::runtime_error("SDL_Vulkan_GetInstanceExtensions failed");
    }
    extensions.reserve(extCount);
    for (Uint32 i = 0; i < extCount; ++i) {
        extensions.push_back(sdlExtensions[i]);
    }


    this->extensions = extensions;
}

void VulkanAPI::createInstance() {
    vk::ApplicationInfo appInfo;
    appInfo.setApiVersion(vk::makeApiVersion(0, vulkan_api_major_version, vulkan_api_minor_version, 0));

    std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};

    for (auto& extension : extensions) {
        std::cout << extension << "\n";
    }

    vk::InstanceCreateInfo createInfo;
    createInfo.setPApplicationInfo(&appInfo)
              .setPEnabledLayerNames(layers)
              .setPEnabledExtensionNames(extensions);

    this->instance = vk::createInstance(createInfo);
}

void VulkanAPI::pickupPhysicalDevice() {
    auto devices = instance.enumeratePhysicalDevices();
    phyDevice = devices[0];
    std::cout << phyDevice.getProperties().deviceName << "\n";
}

void VulkanAPI::queryQueueFamilyIndices() {
    auto properties = phyDevice.getQueueFamilyProperties();
    for (int i = 0; i < properties.size(); ++i) {
        if (properties[i].queueFlags | vk::QueueFlagBits::eGraphics) {
            queueFamilyIndices.graphicsQueue = i;
            break;
        }
    }
}

void VulkanAPI::createSurface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
        throw std::runtime_error("Can not create surface"); 
}

void VulkanAPI::createDevice() {
    vk::DeviceCreateInfo createInfo;

    vk::DeviceQueueCreateInfo queueCreateInfo;
    float priorities = 1.0;
    queueCreateInfo.setPQueuePriorities(&priorities)
                   .setQueueCount(1)
                   .setQueueFamilyIndex(queueFamilyIndices.graphicsQueue.value());

    std::vector<const char*> extension = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    createInfo.setQueueCreateInfos(queueCreateInfo)
              .setPEnabledExtensionNames(extension);

    this->device = phyDevice.createDevice(createInfo);
}

void VulkanAPI::getQueues() {
    graphicsQueue = device.getQueue(queueFamilyIndices.graphicsQueue.value(), 0);
}

void VulkanAPI:: querySwapchainInfo() {
    auto formats = phyDevice.getSurfaceFormatsKHR();
    swapchainInfo.format = formats[0];
    for (const auto& format : formats) {
        if (format.format == vk::Format::eR8G8B8A8Srgb &&
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            swapchainInfo.format = format;
            break;
        }
    }

    auto capabilities = phyDevice.getSurfaceCapabilitiesKHR(surface);
    swapchainInfo.imageCount = std::clamp<uint32_t>(2, capabilities.minImageCount, capabilities.maxImageCount);
    swapchainInfo.imageExtent.width = std::clamp<uint32_t>(m_width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    swapchainInfo.imageExtent.height = std::clamp<uint32_t>(m_height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    swapchainInfo.transform = capabilities.currentTransform;

    auto presents = phyDevice.getSurfacePresentModesKHR(surface);
    swapchainInfo.present = vk::PresentModeKHR::eFifo;
    for (const auto& present : presents) {
        if (present == vk::PresentModeKHR::eMailbox) {
            swapchainInfo.present = present;
            break;
        }
    }
}

void VulkanAPI::createSwapchain() {
    vk::SwapchainCreateInfoKHR createInfo;
    createInfo.setClipped(true)
              .setImageArrayLayers(1)
              .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
              .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
              .setSurface(surface)
              .setImageColorSpace(swapchainInfo.format.colorSpace)
              .setImageFormat(swapchainInfo.format.format)
              .setMinImageCount(swapchainInfo.imageCount)
              .setPresentMode(swapchainInfo.present)
              .setQueueFamilyIndices(queueFamilyIndices.graphicsQueue.value())
              .setImageSharingMode(vk::SharingMode::eExclusive);

    swapchain = device.createSwapchainKHR(createInfo);
}

}