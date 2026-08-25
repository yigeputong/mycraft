#pragma once

#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_vulkan.h>
#include <optional>
#include "client/Render/RenderAPI.h"

namespace mycraft {

class VulkanAPI final : public IRenderAPI, public APITraits<VulkanAPI> {
public:
    VulkanAPI();
    ~VulkanAPI();
    bool Init(SDL_Window* window, int width, int height) override;
    bool HandleEvents(SDL_Event& event) override {return false;};
    void render() override {}

    struct QueueFamilyIndices final {
        std::optional<uint32_t> graphicsQueue;
    };

    struct SwapchainInfo {
        vk::Extent2D imageExtent;
        uint32_t imageCount;
        vk::SurfaceFormatKHR format;
        vk::SurfaceTransformFlagsKHR transform;
        vk::PresentModeKHR present;
    };

    vk::Instance instance;
    vk::PhysicalDevice phyDevice;
    vk::Device device;
    vk::Queue graphicsQueue;
    VkSurfaceKHR surface;
    QueueFamilyIndices queueFamilyIndices;
    std::vector<const char*> extensions;
    vk::SwapchainKHR swapchain;
    SwapchainInfo swapchainInfo;


private:

    static constexpr int vulkan_api_major_version = 1;
    static constexpr int vulkan_api_minor_version = 4;

    int m_width = 0;
    int m_height = 0;

    void getExtensions();
    void createInstance();
    void pickupPhysicalDevice();
    void queryQueueFamilyIndices();
    void createSurface(SDL_Window* window);
    void createDevice();
    void getQueues();
    void querySwapchainInfo();
    void createSwapchain();

};

}