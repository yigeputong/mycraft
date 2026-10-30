#include "client/Render/VulkanAPI.h"
#include "client/Window.h"
#include "core/Log.h"

#include <SDL3/SDL_vulkan.h>
#include <SDL3_image/SDL_image.h>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <iostream>
#include <stdexcept>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <array>
#include <limits>
#include <algorithm>

namespace Eng::client {

namespace {
    std::vector<uint8_t> GenerateMissingTexturePixels() {
        constexpr int kSize = 16;
        std::vector<uint8_t> pixels(static_cast<size_t>(kSize * kSize) * 4);
        const uint8_t magenta[4] = {248, 0, 248, 255}; // #F800F8
        const uint8_t black[4]   = {0, 0, 0, 255};
        
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                int idx = (y * kSize + x) * 4;
                // 8x8 的大格子棋盘格（视觉更醒目）
                bool isMagenta = ((x / 8) + (y / 8)) % 2 == 0;
                const uint8_t* color = isMagenta ? magenta : black;
                pixels[idx + 0] = color[0];
                pixels[idx + 1] = color[1];
                pixels[idx + 2] = color[2];
                pixels[idx + 3] = color[3];
            }
        }
        return pixels;
    }
}

VulkanAPI::VulkanAPI() = default;

VulkanAPI::~VulkanAPI() {
    try { Shutdown(); }
    catch (...) { std::fputs("[Vulkan] Shutdown threw exception\n", stderr); }
}

// ============================================================
// 工具
// ============================================================
std::vector<char> VulkanAPI::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        logError(m_logger, "[Vulkan] Failed to open file: " << filename);
        return {};
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
// 生命周期
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

    // 创建默认紫黑棋盘格无效纹理（16x16）防止空绑定
    auto missingPixels = GenerateMissingTexturePixels();
    m_defaultTexturePtr = CreateTexture(missingPixels.data(), 16, 16);
    if (m_defaultTexturePtr) {
        m_defaultTexture = m_defaultTexturePtr->GetHandle();
    } else {
        throw std::runtime_error("Failed to create default missing texture");
    }
}

void VulkanAPI::Shutdown() {
    if (!*m_device || m_isShutdown) return;
    m_device.waitIdle();
    m_isShutdown = true;

    // 0. 清空外部引用
    m_pendingFramebuffer = nullptr;
    m_imageInAttachment  = false;
    m_defaultTexturePtr.reset();
    m_defaultTexture = 0;

    // 1. 释放 VMA 分配的资源（Mesh、Texture、FBO、UBO 等）
    for (const auto& item : m_deletionQueue) {
        const auto& p = item.p;
        if (p.vertexAlloc) vmaDestroyBuffer(m_allocator, p.vertexBuffer, p.vertexAlloc);
        if (p.indexAlloc) vmaDestroyBuffer(m_allocator, p.indexBuffer, p.indexAlloc);
        if (p.instanceAlloc) vmaDestroyBuffer(m_allocator, p.instanceBuffer, p.instanceAlloc);
    }
    m_deletionQueue.clear();
    if (m_skyCubeBuffer != VK_NULL_HANDLE && m_skyCubeAlloc != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_skyCubeBuffer, m_skyCubeAlloc);
        m_skyCubeBuffer = VK_NULL_HANDLE;
        m_skyCubeAlloc  = VK_NULL_HANDLE;
    }

    for (auto& [h, fb] : m_framebuffers) {
        if (fb.colorAlloc != VK_NULL_HANDLE) vmaDestroyImage(m_allocator, fb.colorImage, fb.colorAlloc);
        if (fb.depthAlloc != VK_NULL_HANDLE) vmaDestroyImage(m_allocator, fb.depthImage, fb.depthAlloc);
    }
    m_framebuffers.clear();
    m_fbColorHandles.clear();

    for (auto& [h, tex] : m_textures) {
        if (tex.ownsImage && tex.alloc != VK_NULL_HANDLE) {
            vmaDestroyImage(m_allocator, tex.image, tex.alloc);
        }
    }
    m_textures.clear();

    for (auto& [h, mi] : m_meshes) {
        if (mi.vertexAlloc != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, mi.vertexBuffer, mi.vertexAlloc);
        if (mi.indexAlloc != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, mi.indexBuffer, mi.indexAlloc);
        if (mi.instanceAlloc != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, mi.instanceBuffer, mi.instanceAlloc);
    }
    m_meshes.clear();
    m_shaders.clear();

    for (size_t i = 0; i < m_globalUBOs.size(); ++i) {
        if (m_globalUBOAllocs[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_globalUBOs[i], m_globalUBOAllocs[i]);
        }
    }
    m_globalUBOs.clear();
    m_globalUBOAllocs.clear();
    m_globalUBOMapped.clear();
    m_globalSets.clear();
    m_postProcessSets.clear();

    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }

    // ==========================================
    // 2. 严格按顺序清理所有 Vulkan RAII 对象
    // ==========================================
    
    // 清理图形管线
    m_fullscreenPipeline = nullptr;
    m_fullscreenLayout = nullptr;
    m_fullscreenVert = nullptr;
    m_fullscreenFrag = nullptr;

    // 清理命令池和同步对象
    m_commandBuffers.clear();
    m_presentCompleteSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_commandPool = nullptr;

    // 清理 Swapchain 和深度资源
    m_swapChainImageViews.clear();
    m_swapChain = nullptr;
    m_depthImageView = nullptr;
    m_depthImage = nullptr;
    m_depthImageMemory = nullptr;

    // 清理描述符池和布局
    m_descriptorPool = nullptr;
    m_globalSetLayout = nullptr;
    m_textureSetLayout = nullptr;

    // 清理 Surface 和 Debug
    m_surface = nullptr;
    m_debugMessenger = nullptr;

    // 最后清空 Device、PhysicalDevice 和 Instance
    m_device = nullptr;
    m_physicalDevice = nullptr;
    m_instance = nullptr;

    logInfo(m_logger, "[RenderAPI] Shutdown");
}

// ============================================================
// 内部销毁逻辑（供智能指针调用）
// ============================================================
void VulkanAPI::DestroyMeshInternal(MeshHandle h) {
    if (m_isShutdown) return;
    auto it = m_meshes.find(h);
    if (it == m_meshes.end()) return;

    PendingDestroy p;
    p.vertexBuffer = it->second.vertexBuffer;
    p.vertexAlloc = it->second.vertexAlloc;
    p.indexBuffer = it->second.indexBuffer;
    p.indexAlloc = it->second.indexAlloc;
    p.instanceBuffer = it->second.instanceBuffer;
    p.instanceAlloc = it->second.instanceAlloc;

    // 退休帧 = 当前帧 + 在途帧数
    m_deletionQueue.push_back({ m_frameCounter + MAX_FRAMES_IN_FLIGHT, p });
    
    m_meshes.erase(it);
}

void VulkanAPI::DestroyTextureInternal(TextureHandle h) {
    if (m_isShutdown) return;
    auto it = m_textures.find(h);
    if (it == m_textures.end()) return;
    it->second.view = nullptr;
    it->second.sampler = nullptr;
    it->second.set = nullptr;
    if (it->second.ownsImage && it->second.alloc != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, it->second.image, it->second.alloc);
    }
    m_textures.erase(it);
}

void VulkanAPI::DestroyShaderInternal(ShaderHandle h) {
    if (m_isShutdown) return;
    if (!*m_device) return;
    vkDeviceWaitIdle(*m_device);
    m_shaders.erase(h);
}

void VulkanAPI::DestroyFramebufferInternal(FramebufferHandle h) {
    if (m_isShutdown) return;
    if (m_device != nullptr) vkDeviceWaitIdle(*m_device);
    if (m_pendingFramebuffer && m_pendingFramebuffer->GetHandle() == h) m_pendingFramebuffer = nullptr;

    auto it = m_framebuffers.find(h);
    if (it == m_framebuffers.end()) return;

    if (it->second.colorHandle) m_textures.erase(it->second.colorHandle);
    it->second.depthView = nullptr;

    if (it->second.colorAlloc) vmaDestroyImage(m_allocator, it->second.colorImage, it->second.colorAlloc);
    if (it->second.depthAlloc) vmaDestroyImage(m_allocator, it->second.depthImage, it->second.depthAlloc);
    m_framebuffers.erase(it);
}

// ============================================================
// 资源创建（带智能指针包装）
// ============================================================
MeshPtr VulkanAPI::CreateMesh(const MeshData& data) {
    MeshHandle h = CreateMeshInternal(data);
    if (h == 0) return nullptr;
    return MeshPtr(new Mesh(h), [this](Mesh* m) {
        this->DestroyMeshInternal(m->GetHandle());
        delete m;
    });
}

MeshHandle VulkanAPI::CreateMeshInternal(const MeshData& data) {
    MeshInternal mi;

    auto createHostBuffer = [this](VkDeviceSize size, VkBufferUsageFlags usage,
                                   VkBuffer& outBuf, VmaAllocation& outAlloc,
                                   VmaAllocationInfo& outInfo) {
        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = size;
        bci.usage       = usage;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                 | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ai.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(m_allocator, &bci, &ai,
                            &outBuf, &outAlloc, &outInfo) != VK_SUCCESS)
            throw std::runtime_error("vmaCreateBuffer failed");
    };

    if (!data.vertices.empty()) {
        VkDeviceSize size = data.vertices.size() * sizeof(Vertex);
        VmaAllocationInfo info{};
        createHostBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         mi.vertexBuffer, mi.vertexAlloc, info);
        std::memcpy(info.pMappedData, data.vertices.data(), size);
        vmaFlushAllocation(m_allocator, mi.vertexAlloc, 0, VK_WHOLE_SIZE);
    }

    if (!data.indices.empty()) {
        VkDeviceSize size = data.indices.size() * sizeof(uint32_t);
        VmaAllocationInfo info{};
        createHostBuffer(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         mi.indexBuffer, mi.indexAlloc, info);
        std::memcpy(info.pMappedData, data.indices.data(), size);
        vmaFlushAllocation(m_allocator, mi.indexAlloc, 0, VK_WHOLE_SIZE);
        mi.indexCount = static_cast<uint32_t>(data.indices.size());
    }

    {
        VkDeviceSize size = sizeof(glm::mat4);
        glm::mat4 identity(1.0f);
        VmaAllocationInfo info{};
        createHostBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         mi.instanceBuffer, mi.instanceAlloc, info);
        std::memcpy(info.pMappedData, &identity, size);
        vmaFlushAllocation(m_allocator, mi.instanceAlloc, 0, VK_WHOLE_SIZE);
        mi.instanceCapacity = 1;
    }

    MeshHandle h = m_nextMesh++;
    m_meshes.emplace(h, mi);
    return h;
}

TexturePtr VulkanAPI::CreateTexture(const uint8_t* rgba, int w, int h) {
    TextureHandle th = CreateTextureInternal(rgba, w, h);
    if (th == 0) return nullptr;
    return TexturePtr(new Texture(th), [this](Texture* t) {
        this->DestroyTextureInternal(t->GetHandle());
        delete t;
    });
}

TextureHandle VulkanAPI::CreateTextureInternal(const uint8_t* rgba, int w, int h) {
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = imageSize;
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                 | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ai.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo info{};
        vmaCreateBuffer(m_allocator, &bci, &ai, &stagingBuf, &stagingAlloc, &info);
        std::memcpy(info.pMappedData, rgba, imageSize);
        vmaFlushAllocation(m_allocator, stagingAlloc, 0, VK_WHOLE_SIZE);
    }

    VkImage rawImage = VK_NULL_HANDLE;
    VmaAllocation imageAlloc = VK_NULL_HANDLE;
    {
        VkImageCreateInfo ici{};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent        = { (uint32_t)w, (uint32_t)h, 1 };
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        vmaCreateImage(m_allocator, &ici, &ai, &rawImage, &imageAlloc, nullptr);
    }

    {
        auto cmd = beginSingleTimeCommands();
        transition_image_layout(cmd, rawImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, {}, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eTransfer, vk::ImageAspectFlagBits::eColor);
        vk::BufferImageCopy region{ .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0, .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, .imageOffset = { 0, 0, 0 }, .imageExtent = { (uint32_t)w, (uint32_t)h, 1 } };
        cmd.copyBufferToImage(stagingBuf, rawImage, vk::ImageLayout::eTransferDstOptimal, region);
        transition_image_layout(cmd, rawImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eTransferWrite, vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eColor);
        endSingleTimeCommands(std::move(cmd));
    }
    vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);

    vk::raii::ImageView view = createImageView(rawImage, vk::Format::eR8G8B8A8Unorm, vk::ImageAspectFlagBits::eColor);
    vk::SamplerCreateInfo sci{ .magFilter = vk::Filter::eNearest, .minFilter = vk::Filter::eNearest, .mipmapMode = vk::SamplerMipmapMode::eNearest, .addressModeU = vk::SamplerAddressMode::eRepeat, .addressModeV = vk::SamplerAddressMode::eRepeat, .addressModeW = vk::SamplerAddressMode::eRepeat, .anisotropyEnable = vk::False, .compareEnable = vk::False, .minLod = 0.0f, .maxLod = 0.0f, .borderColor = vk::BorderColor::eIntOpaqueBlack, .unnormalizedCoordinates = vk::False };
    vk::raii::Sampler sampler(m_device, sci);

    vk::DescriptorSetAllocateInfo dsai{ .descriptorPool = m_descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &*m_textureSetLayout };
    auto sets = m_device.allocateDescriptorSets(dsai);
    vk::raii::DescriptorSet texSet = std::move(sets[0]);

    vk::DescriptorImageInfo imgInfo{ .sampler = *sampler, .imageView = *view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
    vk::WriteDescriptorSet write{ .dstSet = *texSet, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imgInfo };
    m_device.updateDescriptorSets(write, {});

    TextureInternal ti{ .image = rawImage, .alloc = imageAlloc, .view = std::move(view), .sampler = std::move(sampler), .set = std::move(texSet) };
    TextureHandle handle = m_nextTexture++;
    m_textures.emplace(handle, std::move(ti));
    return handle;
}

ShaderPtr VulkanAPI::CreateShader(const ShaderCreateInfo& info) {
    ShaderHandle h = CreateShaderInternal(info);
    if (h == 0) return nullptr;
    return ShaderPtr(new Shader(h), [this](Shader* s) {
        this->DestroyShaderInternal(s->GetHandle());
        delete s;
    });
}

ShaderHandle VulkanAPI::CreateShaderInternal(const ShaderCreateInfo& info) {
    auto vertCode = readFile(info.vertPath);
    auto fragCode = readFile(info.fragPath);
    
    if (vertCode.empty() || fragCode.empty()) {
        logError(m_logger, "[Vulkan] Shader source empty or not found!");
        return 0;
    }

    ShaderInternal si;
    si.vertModule = createShaderModule(vertCode);
    si.fragModule = createShaderModule(fragCode);

    // ==== 将自定义枚举映射为 Vulkan 状态 ====
    // 面剔除
    vk::CullModeFlagBits vkCullMode = vk::CullModeFlagBits::eBack;
    if (info.cullMode == rhi::CullMode::None)  vkCullMode = vk::CullModeFlagBits::eNone;
    if (info.cullMode == rhi::CullMode::Front) vkCullMode = vk::CullModeFlagBits::eFront;

    // 深度测试
    bool vkDepthTest = (info.depthMode != rhi::DepthMode::Disabled);
    bool vkDepthWrite = (info.depthMode == rhi::DepthMode::Normal);
    vk::CompareOp vkDepthOp = (info.cullMode == rhi::CullMode::None && info.depthMode == rhi::DepthMode::TestOnly) ? vk::CompareOp::eLessOrEqual : vk::CompareOp::eLess; // 简易推断天空盒

    // 混合模式
    bool vkBlendEnable = (info.blendMode != rhi::BlendMode::None);
    vk::BlendFactor srcColor = vk::BlendFactor::eSrcAlpha;
    vk::BlendFactor dstColor = vk::BlendFactor::eOneMinusSrcAlpha;
    if (info.blendMode == rhi::BlendMode::Additive) {
        srcColor = vk::BlendFactor::eSrcAlpha;
        dstColor = vk::BlendFactor::eOne;
    }

    vk::Format colorFmt = m_swapChainSurfaceFormat.format; // 默认对齐屏幕
    if (info.colorFormat == rhi::TextureFormat::RGBA16_SFLOAT) {
        colorFmt = vk::Format::eR16G16B16A16Sfloat;
    } else if (info.colorFormat == rhi::TextureFormat::R8_UNORM) {
        colorFmt = vk::Format::eR8Unorm;
    }

    // ==== Shader stages ====
    vk::PipelineShaderStageCreateInfo stages[2] = {
        vk::PipelineShaderStageCreateInfo{}.setStage(vk::ShaderStageFlagBits::eVertex).setModule(*si.vertModule).setPName(info.vertEntry.c_str()),
        vk::PipelineShaderStageCreateInfo{}.setStage(vk::ShaderStageFlagBits::eFragment).setModule(*si.fragModule).setPName(info.fragEntry.c_str())
    };

    // ==== Vertex input ====
    vk::VertexInputBindingDescription bindings[2] = {
        vk::VertexInputBindingDescription{ 0, sizeof(Vertex), vk::VertexInputRate::eVertex },
        vk::VertexInputBindingDescription{ 1, sizeof(glm::mat4), vk::VertexInputRate::eInstance }
    };

    vk::VertexInputAttributeDescription attrs[9] = {
        { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position) },
        { 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal)   },
        { 2, 0, vk::Format::eR32G32Sfloat,    offsetof(Vertex, uv)       },
        { 3, 0, vk::Format::eR32Sfloat,       offsetof(Vertex, ao)       },
        { 4, 1, vk::Format::eR32G32B32A32Sfloat, 0                       },
        { 5, 1, vk::Format::eR32G32B32A32Sfloat, 16                      },
        { 6, 1, vk::Format::eR32G32B32A32Sfloat, 32                      },
        { 7, 1, vk::Format::eR32G32B32A32Sfloat, 48                      },
        { 8, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, light) }
    };

    vk::PipelineVertexInputStateCreateInfo vi{};
    
    // ★ 修复顶点输入未消耗警告
    if (info.vertPath.find("sky") != std::string::npos) {
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = bindings;
        vi.vertexAttributeDescriptionCount = 1;
        vi.pVertexAttributeDescriptions    = attrs;
    } else if (info.vertPath.find("lod") != std::string::npos) {
        static vk::VertexInputAttributeDescription lodAttrs[5] = {
            attrs[0], attrs[1], attrs[2], attrs[3], attrs[8]
        };
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = bindings;
        vi.vertexAttributeDescriptionCount = 5;
        vi.pVertexAttributeDescriptions    = lodAttrs;
    } else {
        vi.vertexBindingDescriptionCount   = 2;
        vi.pVertexBindingDescriptions      = bindings;
        vi.vertexAttributeDescriptionCount = 9;
        vi.pVertexAttributeDescriptions    = attrs;
    }

    vk::PipelineInputAssemblyStateCreateInfo ia{ .topology = vk::PrimitiveTopology::eTriangleList };
    vk::DynamicState dynStates[2] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dyn{};
    dyn.setDynamicStates(dynStates);

    vk::PipelineViewportStateCreateInfo vps{};
    vps.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rast{};
    rast.setPolygonMode(vk::PolygonMode::eFill).setCullMode(vkCullMode).setFrontFace(vk::FrontFace::eCounterClockwise).setLineWidth(1.0f);

    vk::PipelineMultisampleStateCreateInfo ms{};
    ms.setRasterizationSamples(vk::SampleCountFlagBits::e1);

    vk::PipelineDepthStencilStateCreateInfo ds{};
    ds.setDepthTestEnable(vkDepthTest).setDepthWriteEnable(vkDepthWrite).setDepthCompareOp(vkDepthOp);

    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.setBlendEnable(vkBlendEnable).setSrcColorBlendFactor(srcColor).setDstColorBlendFactor(dstColor).setColorBlendOp(vk::BlendOp::eAdd)
            .setSrcAlphaBlendFactor(vk::BlendFactor::eOne).setDstAlphaBlendFactor(vk::BlendFactor::eZero).setAlphaBlendOp(vk::BlendOp::eAdd)
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.setAttachments(blendAtt);

    vk::PipelineLayoutCreateInfo plci{};
    vk::DescriptorSetLayout layouts[2] = { *m_globalSetLayout, *m_textureSetLayout };
    plci.setSetLayouts(layouts);
    si.layout = vk::raii::PipelineLayout(m_device, plci);

    vk::Format depthFmt = findDepthFormat();
    vk::PipelineRenderingCreateInfo priC{};
    priC.setColorAttachmentCount(1).setPColorAttachmentFormats(&colorFmt).setDepthAttachmentFormat(depthFmt);

    vk::GraphicsPipelineCreateInfo gpciC{};
    gpciC.setStages(stages).setPVertexInputState(&vi).setPInputAssemblyState(&ia).setPViewportState(&vps)
         .setPRasterizationState(&rast).setPMultisampleState(&ms).setPDepthStencilState(&ds).setPColorBlendState(&blend)
         .setPDynamicState(&dyn).setLayout(*si.layout).setRenderPass(nullptr).setPNext(&priC);

    si.pipeline = vk::raii::Pipeline(m_device, nullptr, gpciC);

    ShaderHandle h = m_nextShader++;
    m_shaders.emplace(h, std::move(si));
    return h;
}

FramebufferPtr VulkanAPI::CreateFramebuffer(const FramebufferCreateInfo& info) {
    FramebufferHandle h = CreateFramebufferInternal(info);
    if (h == 0) return nullptr;

    TextureHandle texH = m_fbColorHandles[h]; // 已注册
    TexturePtr colorTex = TexturePtr(new Texture(texH), [this](Texture* t) {
        this->DestroyTextureInternal(t->GetHandle());
        delete t;
    });
    return {new Framebuffer(h, colorTex, info.width, info.height),
        [this](Framebuffer* f) {
            this->DestroyFramebufferInternal(f->GetHandle());
            delete f;
        }};
}

FramebufferHandle VulkanAPI::CreateFramebufferInternal(const FramebufferCreateInfo& info) {
    FramebufferInternal fbi{ .width = info.width, .height = info.height, .hasDepth = info.hasDepth };
    
    vk::Format colorFormat = vk::Format::eR8G8B8A8Unorm;
    if (info.colorFormat == rhi::TextureFormat::RGBA16_SFLOAT) colorFormat = vk::Format::eR16G16B16A16Sfloat;
    if (info.colorFormat == rhi::TextureFormat::R8_UNORM)      colorFormat = vk::Format::eR8Unorm;

    {
        VkImageCreateInfo ici{};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = static_cast<VkFormat>(colorFormat);
        ici.extent        = { (uint32_t)info.width, (uint32_t)info.height, 1 };
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ici.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        vmaCreateImage(m_allocator, &ici, &ai, &fbi.colorImage, &fbi.colorAlloc, nullptr);
    }
    
    if (info.hasDepth) {
        VkImageCreateInfo ici{};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = VK_FORMAT_D32_SFLOAT;
        ici.extent        = { (uint32_t)info.width, (uint32_t)info.height, 1 };
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        vmaCreateImage(m_allocator, &ici, &ai, &fbi.depthImage, &fbi.depthAlloc, nullptr);
    }

    auto colorView = createImageView(fbi.colorImage, colorFormat, vk::ImageAspectFlagBits::eColor);
    if (info.hasDepth) {
        fbi.depthView = createImageView(fbi.depthImage, vk::Format::eD32Sfloat, vk::ImageAspectFlagBits::eDepth);
    }

    {
        auto cmd = beginSingleTimeCommands();
        transition_image_layout(cmd, fbi.colorImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal, {}, vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eColor);
        if (info.hasDepth) {
            transition_image_layout(cmd, fbi.depthImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal, {}, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::ImageAspectFlagBits::eDepth);
        }
        endSingleTimeCommands(std::move(cmd));
    }

    vk::SamplerCreateInfo sci{ .magFilter = vk::Filter::eLinear, .minFilter = vk::Filter::eLinear, .mipmapMode = vk::SamplerMipmapMode::eNearest, .addressModeU = vk::SamplerAddressMode::eClampToEdge, .addressModeV = vk::SamplerAddressMode::eClampToEdge, .addressModeW = vk::SamplerAddressMode::eClampToEdge, .anisotropyEnable = vk::False, .compareEnable = vk::False, .minLod = 0.0f, .maxLod = 0.0f, .borderColor = vk::BorderColor::eFloatOpaqueBlack, .unnormalizedCoordinates = vk::False };
    auto colorSampler = vk::raii::Sampler(m_device, sci);
    
    vk::DescriptorSetAllocateInfo dsai{ .descriptorPool = m_descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &*m_textureSetLayout };
    auto sets = m_device.allocateDescriptorSets(dsai);
    vk::raii::DescriptorSet set = std::move(sets[0]);
    
    vk::DescriptorImageInfo imgInfo{ .sampler = *colorSampler, .imageView = *colorView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
    vk::WriteDescriptorSet write{ .dstSet = *set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imgInfo };
    m_device.updateDescriptorSets(write, {});

    TextureInternal ti{
        .image = fbi.colorImage,
        .alloc = VK_NULL_HANDLE,
        .view = std::move(colorView),
        .sampler = std::move(colorSampler),
        .set = std::move(set),
        .ownsImage = false
    };
    TextureHandle texHandle = m_nextTexture++;
    m_textures.emplace(texHandle, std::move(ti));
    fbi.colorHandle = texHandle;

    FramebufferHandle fh = m_nextFramebuffer++;
    m_framebuffers.emplace(fh, std::move(fbi));
    m_fbColorHandles[fh] = texHandle;
    return fh;
}

// ============================================================
// 状态与帧循环
// ============================================================
void VulkanAPI::SetGlobalState(const GlobalState& state) {
    m_globalState = state;
}

void VulkanAPI::BeginFrame() {
    auto fenceRes = m_device.waitForFences(*m_inFlightFences[m_frameIndex], vk::True, UINT64_MAX);
    if (fenceRes != vk::Result::eSuccess) throw std::runtime_error("waitForFences failed");

    bool acquired = false;
    while (!acquired) {
        try {
            auto [res, idx] = m_swapChain.acquireNextImage(UINT64_MAX, *m_presentCompleteSemaphores[m_frameIndex], nullptr);
            m_imageIndex = idx;
            if (res == vk::Result::eSuboptimalKHR) { recreateSwapChain(); continue; }
            acquired = true;
        } catch (const vk::OutOfDateKHRError&) { recreateSwapChain(); }
    }

    m_device.resetFences(*m_inFlightFences[m_frameIndex]);

    // 1. 递增帧计数器
    ++m_frameCounter;
    // 2. 安全清理到期的资源
    // 此时所有可能引用该 Buffer 的 CommandBuffer 都已经经历了一次 Reset，绝对安全
    for (auto it = m_deletionQueue.begin(); it != m_deletionQueue.end(); ) {
        if (it->retireFrame <= m_frameCounter) {
            const auto& p = it->p;
            if (p.vertexAlloc) vmaDestroyBuffer(m_allocator, p.vertexBuffer, p.vertexAlloc);
            if (p.indexAlloc) vmaDestroyBuffer(m_allocator, p.indexBuffer, p.indexAlloc);
            if (p.instanceAlloc) vmaDestroyBuffer(m_allocator, p.instanceBuffer, p.instanceAlloc);
            it = m_deletionQueue.erase(it);
        } else {
            ++it;
        }
    }
    // 3. 重置并开始录制当前帧的命令缓冲
    m_commandBuffers[m_frameIndex].reset();
    m_commandBuffers[m_frameIndex].begin({});

    m_frameStarted = true;
    m_renderPassActive = false;
    m_imageInAttachment = false;

    // 提交 UBO 数据
    GlobalState uboData{};
    uboData.view = m_globalState.view;
    uboData.projection = m_globalState.projection;
    uboData.projection[1][1] *= -1.0f; // Vulkan Y 轴翻转
    uboData.viewPos = m_globalState.viewPos;
    uboData.aoStrength = m_globalState.aoStrength;
    uboData.lightDir = glm::normalize(m_globalState.lightDir);
    uboData.lightIntensity = m_globalState.lightIntensity;
    uboData.lightColor = m_globalState.lightColor;
    uboData.timeOfDay = m_globalState.timeOfDay;
    uboData.lightAmbient = m_globalState.lightAmbient;
    std::memcpy(m_globalUBOMapped[m_frameIndex], &uboData, sizeof(uboData));
    vmaFlushAllocation(m_allocator, m_globalUBOAllocs[m_frameIndex], 0, VK_WHOLE_SIZE);
}

void VulkanAPI::EndFrame() {
    if (!m_frameStarted) return;

    if (m_imguiInitialized) {
        ImGui::Render();
        ImGuiRenderDrawData();
    }

    if (m_renderPassActive) { m_commandBuffers[m_frameIndex].endRendering(); m_renderPassActive = false; }

    // FBO 兜底：用户没 UnbindFramebuffer 就 EndFrame
    if (m_pendingFramebuffer && m_imageInAttachment) {
        auto fit = m_framebuffers.find(m_pendingFramebuffer->GetHandle());
        if (fit != m_framebuffers.end()) {
            transition_image_layout(m_commandBuffers[m_frameIndex], fit->second.colorImage,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader,
                vk::ImageAspectFlagBits::eColor);
        }
        m_imageInAttachment = false;
    }
    m_pendingFramebuffer = nullptr;

    // swapchain：本帧进入过 attachment 则转 PRESENT；否则首帧也要从 UNDEFINED 转 PRESENT
    if (m_imageInAttachment) {
        transition_image_layout(m_commandBuffers[m_frameIndex], m_swapChainImages[m_imageIndex],
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eBottomOfPipe,
            vk::ImageAspectFlagBits::eColor);
        m_imageInAttachment = false;
    } else if (m_swapChainFirstUse[m_imageIndex]) {
        m_swapChainFirstUse[m_imageIndex] = false;
        transition_image_layout(m_commandBuffers[m_frameIndex], m_swapChainImages[m_imageIndex],
            vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR,
            {}, {},
            vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eBottomOfPipe,
            vk::ImageAspectFlagBits::eColor);
    } else {
        // 如果被 ImGui 偷偷改了 Layout，这里安全地转回 PRESENT_SRC
        transition_image_layout(m_commandBuffers[m_frameIndex], m_swapChainImages[m_imageIndex],
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eBottomOfPipe,
            vk::ImageAspectFlagBits::eColor);
    }

    m_commandBuffers[m_frameIndex].end();

    vk::PipelineStageFlags waitStage(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    vk::SubmitInfo si{ .waitSemaphoreCount = 1, .pWaitSemaphores = &*m_presentCompleteSemaphores[m_frameIndex],
        .pWaitDstStageMask = &waitStage, .commandBufferCount = 1,
        .pCommandBuffers = &*m_commandBuffers[m_frameIndex], .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*m_renderFinishedSemaphores[m_imageIndex] };
    m_queue.submit(si, *m_inFlightFences[m_frameIndex]);

    vk::PresentInfoKHR pi{ .waitSemaphoreCount = 1, .pWaitSemaphores = &*m_renderFinishedSemaphores[m_imageIndex],
        .swapchainCount = 1, .pSwapchains = &*m_swapChain, .pImageIndices = &m_imageIndex };

    bool needRecreate = m_framebufferResized;
    try {
        auto r = m_queue.presentKHR(pi);
        if (r == vk::Result::eSuboptimalKHR) needRecreate = true;
    } catch (const vk::OutOfDateKHRError&) { needRecreate = true; }

    if (needRecreate) { m_framebufferResized = false; recreateSwapChain(); }

    m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    m_frameStarted = false;
}

void VulkanAPI::SetViewport(int x, int y, int w, int h) {
    if (!m_frameStarted || !m_renderPassActive) return;
    m_commandBuffers[m_frameIndex].setViewport(
        0, vk::Viewport(static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(w), static_cast<float>(h),
                        0.0f, 1.0f));
    m_commandBuffers[m_frameIndex].setScissor(
        0, vk::Rect2D({x, y}, {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}));
}

void VulkanAPI::SetClearColor(float r, float g, float b, float a) {
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}

void VulkanAPI::Clear(rhi::ClearFlag flags) {
    m_clearPendingFlags |= static_cast<uint32_t>(flags);

    if (m_frameStarted) {
        if (m_renderPassActive) {
            m_commandBuffers[m_frameIndex].endRendering();
            m_renderPassActive = false;
        }
        ensureRenderPassActive();
    }
}

// ============================================================
// 渲染绘制
// ============================================================
void VulkanAPI::DrawMesh(const MeshPtr& mesh, const ShaderPtr& shader, const Material& material, const glm::mat4& model) {
    if (!mesh || !shader) return;
    auto mit = m_meshes.find(mesh->GetHandle());
    auto sit = m_shaders.find(shader->GetHandle());
    if (mit == m_meshes.end() || sit == m_shaders.end()) return;

    ensureRenderPassActive();

    TextureHandle texH = material.diffuse ? material.diffuse->GetHandle() : m_defaultTexture;
    vk::DescriptorSet texSet = nullptr;
    auto texIt = m_textures.find(texH);
    if (texIt != m_textures.end()) texSet = *texIt->second.set;
    if (!texSet) return;

    {
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(m_allocator, mit->second.instanceAlloc, &info);
        std::memcpy(info.pMappedData, &model, sizeof(glm::mat4));
        vmaFlushAllocation(m_allocator, mit->second.instanceAlloc, 0, sizeof(glm::mat4));
    }

    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *sit->second.pipeline);
    std::array<vk::DescriptorSet, 2> sets = { *m_globalSets[m_frameIndex], texSet };
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sit->second.layout, 0, sets, {});
    std::array<vk::Buffer, 2> vbs = { vk::Buffer(mit->second.vertexBuffer), vk::Buffer(mit->second.instanceBuffer) };
    std::array<vk::DeviceSize, 2> offsets = { 0, 0 };
    cmd.bindVertexBuffers(0, vbs, offsets);
    cmd.bindIndexBuffer(vk::Buffer(mit->second.indexBuffer), 0, vk::IndexType::eUint32);
    cmd.drawIndexed(mit->second.indexCount, 1, 0, 0, 0);
}

void VulkanAPI::DrawMeshInstanced(const MeshPtr& mesh, const ShaderPtr& shader, const Material& material, const std::vector<glm::mat4>& transforms) {
    if (transforms.empty() || !mesh || !shader) return;
    auto mit = m_meshes.find(mesh->GetHandle());
    auto sit = m_shaders.find(shader->GetHandle());
    if (mit == m_meshes.end() || sit == m_shaders.end()) return;

    ensureRenderPassActive();
    auto& mi = mit->second;
    const uint32_t count = static_cast<uint32_t>(transforms.size());

    if (mi.instanceCapacity < count) {
        if (mi.instanceBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, mi.instanceBuffer, mi.instanceAlloc);
        uint32_t newCap = 1; while (newCap < count) newCap <<= 1;
        VkDeviceSize size = newCap * sizeof(glm::mat4);

        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = size;
        bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                 | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        ai.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(m_allocator, &bci, &ai, &mi.instanceBuffer, &mi.instanceAlloc, nullptr) != VK_SUCCESS) return;
        mi.instanceCapacity = newCap;
    }
    VmaAllocationInfo info{};
    vmaGetAllocationInfo(m_allocator, mi.instanceAlloc, &info);
    std::memcpy(info.pMappedData, transforms.data(), count * sizeof(glm::mat4));
    vmaFlushAllocation(m_allocator, mi.instanceAlloc, 0, count * sizeof(glm::mat4));

    TextureHandle texH = material.diffuse ? material.diffuse->GetHandle() : m_defaultTexture;
    vk::DescriptorSet texSet = nullptr;
    auto texIt = m_textures.find(texH);
    if (texIt != m_textures.end()) texSet = *texIt->second.set;
    if (!texSet) return;

    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *sit->second.pipeline);
    std::array<vk::DescriptorSet, 2> sets = { *m_globalSets[m_frameIndex], texSet };
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sit->second.layout, 0, sets, {});
    std::array<vk::Buffer, 2> vbs = { vk::Buffer(mi.vertexBuffer), vk::Buffer(mi.instanceBuffer) };
    std::array<vk::DeviceSize, 2> offsets = { 0, 0 };
    cmd.bindVertexBuffers(0, vbs, offsets);
    cmd.bindIndexBuffer(vk::Buffer(mi.indexBuffer), 0, vk::IndexType::eUint32);
    cmd.drawIndexed(mi.indexCount, count, 0, 0, 0);
}

void VulkanAPI::DrawMeshBatch(const std::vector<MeshPtr>& meshes, const ShaderPtr& shader, const Material& material) {
    if (meshes.empty() || !shader) return;
    auto sit = m_shaders.find(shader->GetHandle());
    if (sit == m_shaders.end()) return;

    ensureRenderPassActive();
    TextureHandle texH = material.diffuse ? material.diffuse->GetHandle() : m_defaultTexture;
    vk::DescriptorSet texSet = nullptr;
    auto texIt = m_textures.find(texH);
    if (texIt != m_textures.end()) texSet = *texIt->second.set;
    if (!texSet) return;

    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *sit->second.pipeline);
    std::array<vk::DescriptorSet, 2> sets = { *m_globalSets[m_frameIndex], texSet };
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sit->second.layout, 0, sets, {});

    for (const auto& meshPtr : meshes) {
        if (!meshPtr) continue;
        auto mit = m_meshes.find(meshPtr->GetHandle());
        if (mit == m_meshes.end() || mit->second.indexCount == 0 || mit->second.instanceBuffer == VK_NULL_HANDLE) continue;
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(m_allocator, mit->second.instanceAlloc, &info);
        static const glm::mat4 kIdentity(1.0f);
        std::memcpy(info.pMappedData, &kIdentity, sizeof(glm::mat4));
        vmaFlushAllocation(m_allocator, mit->second.instanceAlloc, 0, sizeof(glm::mat4));
        std::array<vk::Buffer, 2> vbs = { vk::Buffer(mit->second.vertexBuffer), vk::Buffer(mit->second.instanceBuffer) };
        std::array<vk::DeviceSize, 2> offsets = { 0, 0 };
        cmd.bindVertexBuffers(0, vbs, offsets);
        cmd.bindIndexBuffer(vk::Buffer(mit->second.indexBuffer), 0, vk::IndexType::eUint32);
        cmd.drawIndexed(mit->second.indexCount, 1, 0, 0, 0);
    }
}

void VulkanAPI::DrawSkybox(const ShaderPtr& shader) {
    if (!shader) return;
    auto sit = m_shaders.find(shader->GetHandle());
    if (sit == m_shaders.end() || m_skyCubeBuffer == VK_NULL_HANDLE) return;

    ensureRenderPassActive();
    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *sit->second.pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sit->second.layout, 0, *m_globalSets[m_frameIndex], {});
    cmd.bindVertexBuffers(0, vk::Buffer(m_skyCubeBuffer), {0});
    cmd.draw(36, 1, 0, 0);
}

void VulkanAPI::DrawFullscreenQuad(const TexturePtr& texture) {
    if (!texture || !*m_fullscreenPipeline) return;
    auto texIt = m_textures.find(texture->GetHandle());
    if (texIt == m_textures.end()) return;

    ensureRenderPassActive();
    auto& cmd = m_commandBuffers[m_frameIndex];
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_fullscreenPipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_fullscreenLayout, 0, *texIt->second.set, {});
    cmd.draw(3, 1, 0, 0);
}

void VulkanAPI::SetUniform(const ShaderPtr&  /*shader*/, const std::string& name, float value) {
    if (name == "uAOStrength") m_globalState.aoStrength = value;
    else if (name == "uTimeOfDay") m_globalState.timeOfDay = value;
}
void VulkanAPI::SetUniform(const ShaderPtr&, const std::string&, const glm::mat4&) {}
void VulkanAPI::SetUniform(const ShaderPtr&, const std::string&, const glm::vec3&) {}
void VulkanAPI::SetUniform(const ShaderPtr&, const std::string&, int) {}


// ============================================================
// 底层 Vulkan 实现（初始化、Swapchain 等）
// ============================================================
void VulkanAPI::createInstance() {
    if (volkInitialize() != VK_SUCCESS) throw std::runtime_error("volkInitialize failed");

    vk::ApplicationInfo appInfo{ .pApplicationName = "Mycraft", .applicationVersion = VK_MAKE_VERSION(1, 0, 0), .pEngineName = "MycraftEngine", .engineVersion = VK_MAKE_VERSION(1, 0, 0), .apiVersion = vk::makeApiVersion(0, api_major, api_minor, 0) };

    Uint32 extCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    if (!sdlExts) throw std::runtime_error("SDL_Vulkan_GetInstanceExtensions failed");

    std::vector<const char*> exts(sdlExts, sdlExts + extCount);
    if (enableValidationLayers) exts.push_back(vk::EXTDebugUtilsExtensionName);

    std::vector<const char*> layers;
    if (enableValidationLayers) layers.assign(validationLayers.begin(), validationLayers.end());

    vk::InstanceCreateInfo ci{ .pApplicationInfo = &appInfo, .enabledLayerCount = static_cast<uint32_t>(layers.size()), .ppEnabledLayerNames = layers.data(), .enabledExtensionCount = static_cast<uint32_t>(exts.size()), .ppEnabledExtensionNames = exts.data() };

    m_instance = vk::raii::Instance(m_context, ci);
    volkLoadInstance(*m_instance);
}

namespace {
    VKAPI_ATTR vk::Bool32 VKAPI_CALL vkDebugCb(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT, const vk::DebugUtilsMessengerCallbackDataEXT* data, void* userData) {
        auto* api = static_cast<VulkanAPI*>(userData);
        if (api) api->logValidation(static_cast<VkDebugUtilsMessageSeverityFlagBitsEXT>(severity), data->pMessage);
        return vk::False;
    }
}

void VulkanAPI::logValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, const char* message) {
    std::istringstream iss(message);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) logError(m_logger, "[VulkanValidation] " << line);
        else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) logWarning(m_logger, "[VulkanValidation] " << line);
    }
}

void VulkanAPI::setupDebugMessenger() {
    if (!enableValidationLayers) return;
    vk::DebugUtilsMessengerCreateInfoEXT ci{ .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError, .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation, .pfnUserCallback = &vkDebugCb, .pUserData = this };
    m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(ci);
}

void VulkanAPI::createSurface() {
    VkSurfaceKHR raw;
    if (!SDL_Vulkan_CreateSurface(m_window->GetSDLWindow(), *m_instance, nullptr, &raw)) throw std::runtime_error("SDL_Vulkan_CreateSurface failed");
    m_surface = vk::raii::SurfaceKHR(m_instance, raw);
}

bool VulkanAPI::isDeviceSuitable(const vk::raii::PhysicalDevice& pd) {
    if (pd.getProperties().apiVersion < vk::ApiVersion14) return false;
    auto qfs = pd.getQueueFamilyProperties();
    bool hasGfx = std::ranges::any_of(qfs, [](auto& q) { return !!(q.queueFlags & vk::QueueFlagBits::eGraphics); });
    if (!hasGfx) return false;
    auto feats = pd.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    auto f11 = feats.get<vk::PhysicalDeviceVulkan11Features>();
    auto f13 = feats.get<vk::PhysicalDeviceVulkan13Features>();
    auto fEDS = feats.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    return f11.shaderDrawParameters && f13.dynamicRendering && f13.synchronization2 && fEDS.extendedDynamicState;
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
        if ((qfs[i].queueFlags & vk::QueueFlagBits::eGraphics) && m_physicalDevice.getSurfaceSupportKHR(i, *m_surface)) {
            m_queueIndex = i; break;
        }
    }
    if (m_queueIndex == ~0u) throw std::runtime_error("no graphics+present queue");

    vk::PhysicalDeviceFeatures2 f2{}; f2.sType = vk::StructureType::ePhysicalDeviceFeatures2;
    vk::PhysicalDeviceVulkan11Features f11{}; f11.sType = vk::StructureType::ePhysicalDeviceVulkan11Features; f11.shaderDrawParameters = vk::True;
    vk::PhysicalDeviceVulkan13Features f13{}; f13.sType = vk::StructureType::ePhysicalDeviceVulkan13Features; f13.synchronization2 = vk::True; f13.dynamicRendering = vk::True;
    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT fEDS{}; fEDS.sType = vk::StructureType::ePhysicalDeviceExtendedDynamicStateFeaturesEXT; fEDS.extendedDynamicState = vk::True;
    f2.pNext = &f11; f11.pNext = &f13; f13.pNext = &fEDS; fEDS.pNext = nullptr;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo cQci{}; cQci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; cQci.queueFamilyIndex = m_queueIndex; cQci.queueCount = 1; cQci.pQueuePriorities = &prio;
    VkDeviceCreateInfo cDci{}; cDci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; cDci.pNext = &f2; cDci.queueCreateInfoCount = 1; cDci.pQueueCreateInfos = &cQci; cDci.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()); cDci.ppEnabledExtensionNames = requiredDeviceExtension.data();

    VkDevice rawDevice = VK_NULL_HANDLE;
    if (vkCreateDevice(*m_physicalDevice, &cDci, nullptr, &rawDevice) != VK_SUCCESS) throw std::runtime_error("vkCreateDevice failed");
    m_device = vk::raii::Device(m_physicalDevice, rawDevice);
    volkLoadDevice(*m_device);
    m_queue = vk::raii::Queue(m_device, m_queueIndex, 0);
}

void VulkanAPI::createVmaAllocator() {
    VmaVulkanFunctions vkFuncs{}; vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr; vkFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo ci{}; ci.physicalDevice = *m_physicalDevice; ci.device = *m_device; ci.instance = *m_instance; ci.vulkanApiVersion = vk::ApiVersion14; ci.pVulkanFunctions = &vkFuncs;
    if (vmaCreateAllocator(&ci, &m_allocator) != VK_SUCCESS) throw std::runtime_error("vmaCreateAllocator failed");
}

void VulkanAPI::createFullscreenPipeline() {
    m_fullscreenVert = createShaderModule(readFile("assets/shaders/build/post/vert.vk.spv"));
    m_fullscreenFrag = createShaderModule(readFile("assets/shaders/build/post/frag.vk.spv"));

    vk::PipelineShaderStageCreateInfo stages[] = {
        {.stage = vk::ShaderStageFlagBits::eVertex,   .module = *m_fullscreenVert, .pName = "main"},
        {.stage = vk::ShaderStageFlagBits::eFragment, .module = *m_fullscreenFrag, .pName = "main"}
    };
    vk::PipelineVertexInputStateCreateInfo   vi{};
    vk::PipelineInputAssemblyStateCreateInfo ia{.topology = vk::PrimitiveTopology::eTriangleList};

    vk::DynamicState dynStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dyn{};
    dyn.setDynamicStates(dynStates);

    vk::PipelineViewportStateCreateInfo vps{};
    vps.setViewportCount(1).setScissorCount(1);

    vk::PipelineRasterizationStateCreateInfo rast{};
    rast.setDepthClampEnable(vk::False)
        .setRasterizerDiscardEnable(vk::False)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setLineWidth(1.0f);

    vk::PipelineMultisampleStateCreateInfo ms{};
    ms.setRasterizationSamples(vk::SampleCountFlagBits::e1);

    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.setBlendEnable(vk::False)
            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.setLogicOpEnable(vk::False).setAttachments(blendAtt);

    vk::PipelineDepthStencilStateCreateInfo ds{};
    ds.setDepthTestEnable(vk::False)
      .setDepthWriteEnable(vk::False)
      .setDepthCompareOp(vk::CompareOp::eAlways)
      .setDepthBoundsTestEnable(vk::False)
      .setStencilTestEnable(vk::False);

    vk::PipelineLayoutCreateInfo plci{};
    plci.setSetLayouts(*m_textureSetLayout);
    m_fullscreenLayout = vk::raii::PipelineLayout(m_device, plci);

    vk::Format colorFmt = m_swapChainSurfaceFormat.format;
    vk::Format depthFmt = findDepthFormat();

    vk::PipelineRenderingCreateInfo priC{};
    priC.setColorAttachmentCount(1)
        .setPColorAttachmentFormats(&colorFmt)
        .setDepthAttachmentFormat(depthFmt);

    vk::GraphicsPipelineCreateInfo gpciC{};
    gpciC.setStages(stages)
         .setPVertexInputState(&vi)
         .setPInputAssemblyState(&ia)
         .setPViewportState(&vps)
         .setPRasterizationState(&rast)
         .setPMultisampleState(&ms)
         .setPDepthStencilState(&ds)
         .setPColorBlendState(&blend)
         .setPDynamicState(&dyn)
         .setLayout(*m_fullscreenLayout)
         .setRenderPass(nullptr)
         .setPNext(&priC);

    m_fullscreenPipeline = vk::raii::Pipeline(m_device, nullptr, gpciC);
}

vk::SurfaceFormatKHR VulkanAPI::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats) {
    auto it = std::ranges::find_if(formats, [](auto& f) { return f.format == vk::Format::eB8G8R8A8Unorm && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });
    return it != formats.end() ? *it : formats[0];
}

vk::PresentModeKHR VulkanAPI::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& modes) {
    if (m_windowConfig.vsync) return vk::PresentModeKHR::eFifo;
    for (auto m : modes) if (m == vk::PresentModeKHR::eMailbox) return m;
    return vk::PresentModeKHR::eImmediate;
}

vk::Extent2D VulkanAPI::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) return caps.currentExtent;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
    return { std::clamp<uint32_t>(w, caps.minImageExtent.width, caps.maxImageExtent.width), std::clamp<uint32_t>(h, caps.minImageExtent.height, caps.maxImageExtent.height) };
}

uint32_t VulkanAPI::chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& caps) {
    uint32_t n = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount > 0 && caps.maxImageCount < n) n = caps.maxImageCount;
    return n;
}

void VulkanAPI::createSwapChain() {
    auto caps = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);
    auto formats = m_physicalDevice.getSurfaceFormatsKHR(*m_surface);
    auto modes = m_physicalDevice.getSurfacePresentModesKHR(*m_surface);
    m_swapChainSurfaceFormat = chooseSwapSurfaceFormat(formats);
    m_swapChainExtent = chooseSwapExtent(caps);
    vk::SwapchainCreateInfoKHR ci{ .surface = *m_surface, .minImageCount = chooseSwapMinImageCount(caps), .imageFormat = m_swapChainSurfaceFormat.format, .imageColorSpace = m_swapChainSurfaceFormat.colorSpace, .imageExtent = m_swapChainExtent, .imageArrayLayers = 1, .imageUsage = vk::ImageUsageFlagBits::eColorAttachment, .imageSharingMode = vk::SharingMode::eExclusive, .preTransform = caps.currentTransform, .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque, .presentMode = chooseSwapPresentMode(modes), .clipped = true, .oldSwapchain = nullptr };
    m_swapChain = vk::raii::SwapchainKHR(m_device, ci);
    m_swapChainImages = m_swapChain.getImages();
}

vk::raii::ImageView VulkanAPI::createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspect) {
    vk::ImageViewCreateInfo ci{ .image = image, .viewType = vk::ImageViewType::e2D, .format = format, .subresourceRange = { aspect, 0, 1, 0, 1 } };
    return vk::raii::ImageView(m_device, ci);
}

void VulkanAPI::createImageViews() {
    m_swapChainImageViews.clear();
    m_swapChainImageViews.reserve(m_swapChainImages.size());
    for (auto& img : m_swapChainImages) m_swapChainImageViews.emplace_back(createImageView(img, m_swapChainSurfaceFormat.format, vk::ImageAspectFlagBits::eColor));
    m_swapChainFirstUse.assign(m_swapChainImages.size(), true);
}

vk::Format VulkanAPI::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
    for (auto f : candidates) {
        auto props = m_physicalDevice.getFormatProperties(f);
        if ((tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)) return f;
    }
    throw std::runtime_error("no supported format");
}

vk::Format VulkanAPI::findDepthFormat() {
    return findSupportedFormat({ vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }, vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

uint32_t VulkanAPI::findMemoryType(uint32_t filter, vk::MemoryPropertyFlags props) {
    auto mp = m_physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((filter & (1 << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    throw std::runtime_error("no suitable memory type");
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory> VulkanAPI::createImage(uint32_t w, uint32_t h, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags props) {
    vk::ImageCreateInfo ci{ .imageType = vk::ImageType::e2D, .format = format, .extent = { w, h, 1 }, .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1, .tiling = tiling, .usage = usage, .sharingMode = vk::SharingMode::eExclusive };
    vk::raii::Image image(m_device, ci);
    auto req = image.getMemoryRequirements();
    vk::MemoryAllocateInfo ai{ .allocationSize = req.size, .memoryTypeIndex = findMemoryType(req.memoryTypeBits, props) };
    vk::raii::DeviceMemory mem(m_device, ai);
    image.bindMemory(*mem, 0);
    return { std::move(image), std::move(mem) };
}

void VulkanAPI::createDepthResources() {
    auto fmt = findDepthFormat();
    std::tie(m_depthImage, m_depthImageMemory) = createImage(m_swapChainExtent.width, m_swapChainExtent.height, fmt, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_depthImageView = createImageView(*m_depthImage, fmt, vk::ImageAspectFlagBits::eDepth);
    {
        auto cmd = beginSingleTimeCommands();
        transition_image_layout(cmd, *m_depthImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal, {}, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::ImageAspectFlagBits::eDepth);
        endSingleTimeCommands(std::move(cmd));
    }
}

void VulkanAPI::transition_image_layout(const vk::raii::CommandBuffer& cmd, vk::Image image, vk::ImageLayout oldL, vk::ImageLayout newL, vk::AccessFlags2 srcA, vk::AccessFlags2 dstA, vk::PipelineStageFlags2 srcS, vk::PipelineStageFlags2 dstS, vk::ImageAspectFlags aspect) {
    vk::ImageMemoryBarrier2 b{ .srcStageMask = srcS, .srcAccessMask = srcA, .dstStageMask = dstS, .dstAccessMask = dstA, .oldLayout = oldL, .newLayout = newL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = { aspect, 0, 1, 0, 1 } };
    cmd.pipelineBarrier2({ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b });
}

void VulkanAPI::createDescriptorPool() {
    std::array<vk::DescriptorPoolSize, 2> sizes{{ { .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT }, { .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 128 } }};
    vk::DescriptorPoolCreateInfo ci{ .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, .maxSets = 1024, .poolSizeCount = static_cast<uint32_t>(sizes.size()), .pPoolSizes = sizes.data() };
    m_descriptorPool = vk::raii::DescriptorPool(m_device, ci);
}

void VulkanAPI::createGlobalDescriptors() {
    vk::DescriptorSetLayoutBinding uboBinding{ .binding = 0, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment };
    vk::DescriptorSetLayoutCreateInfo slci{ .bindingCount = 1, .pBindings = &uboBinding };
    m_globalSetLayout = vk::raii::DescriptorSetLayout(m_device, slci);
    vk::DescriptorSetLayoutBinding texBindings[2] = {
        { .binding = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment },
        { .binding = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment }
    };
    vk::DescriptorSetLayoutCreateInfo texSlci{ .bindingCount = 2, .pBindings = texBindings };
    m_textureSetLayout = vk::raii::DescriptorSetLayout(m_device, texSlci);

    m_globalUBOs.clear(); m_globalUBOAllocs.clear(); m_globalUBOMapped.clear(); m_globalSets.clear();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkBufferCreateInfo bci{}; bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bci.size = sizeof(GlobalState); bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{}; ai.usage = VMA_MEMORY_USAGE_AUTO; ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VkBuffer raw = VK_NULL_HANDLE; VmaAllocation alloc = VK_NULL_HANDLE; VmaAllocationInfo info{};
        if (vmaCreateBuffer(m_allocator, &bci, &ai, &raw, &alloc, &info) != VK_SUCCESS) throw std::runtime_error("vmaCreateBuffer (UBO) failed");
        m_globalUBOs.push_back(raw); m_globalUBOAllocs.push_back(alloc); m_globalUBOMapped.push_back(info.pMappedData);
        vk::DescriptorSetAllocateInfo dsai{ .descriptorPool = m_descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &*m_globalSetLayout };
        auto sets = m_device.allocateDescriptorSets(dsai);
        m_globalSets.push_back(std::move(sets[0]));
        vk::DescriptorBufferInfo dbi{ raw, 0, sizeof(GlobalState) };
        vk::WriteDescriptorSet write{ .dstSet = *m_globalSets[i], .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eUniformBuffer, .pBufferInfo = &dbi };
        m_device.updateDescriptorSets(write, {});
    }
}

void VulkanAPI::createCommandPool() {
    vk::CommandPoolCreateInfo ci{ .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = m_queueIndex };
    m_commandPool = vk::raii::CommandPool(m_device, ci);
}

void VulkanAPI::createCommandBuffers() {
    vk::CommandBufferAllocateInfo ai{ .commandPool = m_commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = MAX_FRAMES_IN_FLIGHT };
    m_commandBuffers = vk::raii::CommandBuffers(m_device, ai);
}

void VulkanAPI::createSyncObjects() {
    m_presentCompleteSemaphores.clear(); m_renderFinishedSemaphores.clear(); m_inFlightFences.clear();
    for (size_t i = 0; i < m_swapChainImages.size(); ++i) m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_presentCompleteSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
        m_inFlightFences.emplace_back(m_device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
    }
}

vk::raii::CommandBuffer VulkanAPI::beginSingleTimeCommands() {
    vk::CommandBufferAllocateInfo ai{ .commandPool = m_commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 };
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

void VulkanAPI::createSkyboxMesh() {
    static const glm::vec3 kCorner[8] = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1},
    };
    static const uint32_t kTri[36] = {
        0,1,2, 2,3,0,   // -Z
        4,5,6, 6,7,4,   // +Z
        0,4,7, 7,3,0,   // -X
        1,5,6, 6,2,1,   // +X
        3,2,6, 6,7,3,   // +Y
        0,1,5, 5,4,0,   // -Y
    };

    std::array<Vertex, 36> verts{};
    for (int i = 0; i < 36; ++i) {
        verts[i].position = kCorner[kTri[i]];
        verts[i].normal   = glm::vec3(0, 1, 0);
        verts[i].uv       = glm::vec2(0);
        verts[i].ao       = 1.0f;
        verts[i].light    = glm::vec4(1, 0, 0, 0);
    }

    VkDeviceSize size = sizeof(Vertex) * 36;

    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = size;
    bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
             | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    ai.usage = VMA_MEMORY_USAGE_AUTO;

    VmaAllocationInfo info{};
    if (vmaCreateBuffer(m_allocator, &bci, &ai, &m_skyCubeBuffer, &m_skyCubeAlloc, &info) != VK_SUCCESS) {
        throw std::runtime_error("vmaCreateBuffer (skybox) failed");
    }
    std::memcpy(info.pMappedData, verts.data(), size);
    vmaFlushAllocation(m_allocator, m_skyCubeAlloc, 0, VK_WHOLE_SIZE);
}

vk::Extent2D VulkanAPI::getCurrentRenderExtent() const {
    if (m_pendingFramebuffer) {
        auto fit = m_framebuffers.find(m_pendingFramebuffer->GetHandle());
        if (fit != m_framebuffers.end()) return vk::Extent2D{ static_cast<uint32_t>(fit->second.width), static_cast<uint32_t>(fit->second.height) };
    }
    return m_swapChainExtent;
}

void VulkanAPI::ensureRenderPassActive() {
    if (m_renderPassActive) return;
    auto& cmd = m_commandBuffers[m_frameIndex];
    vk::ImageView colorView; vk::ImageView depthView; vk::Extent2D extent;
    bool toFBO = (m_pendingFramebuffer != nullptr);

    if (toFBO) {
        auto fit = m_framebuffers.find(m_pendingFramebuffer->GetHandle());
        if (fit == m_framebuffers.end()) return;
        
        auto texIt = m_textures.find(fit->second.colorHandle);
        if (texIt == m_textures.end() || texIt->second.view == nullptr) return;
        colorView = *texIt->second.view;
        
        if (fit->second.hasDepth && fit->second.depthView != nullptr) {
            depthView = *fit->second.depthView;
        }
        extent = vk::Extent2D{ static_cast<uint32_t>(fit->second.width), static_cast<uint32_t>(fit->second.height) };

        if (!m_imageInAttachment) {
            transition_image_layout(cmd, fit->second.colorImage,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
                vk::AccessFlagBits2::eShaderRead, vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eFragmentShader, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::ImageAspectFlagBits::eColor);
            m_imageInAttachment = true;
        }
    } else {
        colorView = m_swapChainImageViews[m_imageIndex];
        if (m_depthImageView != nullptr) depthView = *m_depthImageView;
        extent = m_swapChainExtent;

        if (!m_imageInAttachment) {
            vk::ImageLayout oldLayout = m_swapChainFirstUse[m_imageIndex]
                ? vk::ImageLayout::eUndefined : vk::ImageLayout::ePresentSrcKHR;
            m_swapChainFirstUse[m_imageIndex] = false;
            transition_image_layout(cmd, m_swapChainImages[m_imageIndex],
                oldLayout, vk::ImageLayout::eColorAttachmentOptimal,
                {}, vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::ImageAspectFlagBits::eColor);
            m_imageInAttachment = true;
        }
    }

    bool clearColor = (m_clearPendingFlags & static_cast<uint32_t>(rhi::ClearFlag::Color)) != 0;
    bool clearDepth = (m_clearPendingFlags & static_cast<uint32_t>(rhi::ClearFlag::Depth)) != 0;
    vk::ClearValue clearColorValue{};
    clearColorValue.color = vk::ClearColorValue(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
    vk::ClearValue clearDepthValue = vk::ClearDepthStencilValue(1.0f, 0);
    
    vk::RenderingAttachmentInfo colorAtt{
        .imageView = colorView, .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = clearColor ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore, .clearValue = clearColorValue };
        
    vk::RenderingAttachmentInfo depthAtt{
        .imageView = depthView, .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = clearDepth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eDontCare, .clearValue = clearDepthValue };
        
    bool useDepth = (depthView != nullptr);

    vk::RenderingInfo ri{ .renderArea = { {0,0}, extent }, .layerCount = 1,
        .colorAttachmentCount = 1, .pColorAttachments = &colorAtt, 
        .pDepthAttachment = useDepth ? &depthAtt : nullptr };
        
    cmd.beginRendering(ri);
    cmd.setViewport(0, vk::Viewport(0, 0, (float)extent.width, (float)extent.height, 0, 1));
    cmd.setScissor(0, vk::Rect2D({0,0}, extent));
    m_renderPassActive = true;
    m_clearPendingFlags &= ~static_cast<uint32_t>(rhi::ClearFlag::Color);
    m_clearPendingFlags &= ~static_cast<uint32_t>(rhi::ClearFlag::Depth);
}

void VulkanAPI::BindFramebuffer(const FramebufferPtr& fb) {
    if (!m_frameStarted) return;

    if (m_renderPassActive) {
        m_commandBuffers[m_frameIndex].endRendering();
        m_renderPassActive = false;
    }
    // 旧 target 还在 attachment → 转回去
    if (m_imageInAttachment) {
        if (m_pendingFramebuffer) {
            auto old = m_framebuffers.find(m_pendingFramebuffer->GetHandle());
            if (old != m_framebuffers.end()) {
                transition_image_layout(m_commandBuffers[m_frameIndex], old->second.colorImage,
                    vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderRead,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader,
                    vk::ImageAspectFlagBits::eColor);
            }
        } else {
            // 旧 target 是 swapchain
            transition_image_layout(m_commandBuffers[m_frameIndex], m_swapChainImages[m_imageIndex],
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                vk::AccessFlagBits2::eColorAttachmentWrite, {},
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eBottomOfPipe,
                vk::ImageAspectFlagBits::eColor);
        }
        m_imageInAttachment = false;
    }
    m_pendingFramebuffer = fb;
}

void VulkanAPI::UnbindFramebuffer() {
    if (m_renderPassActive) {
        m_commandBuffers[m_frameIndex].endRendering();
        m_renderPassActive = false;
    }
    if (m_pendingFramebuffer && m_imageInAttachment) {
        auto fit = m_framebuffers.find(m_pendingFramebuffer->GetHandle());
        if (fit != m_framebuffers.end()) {
            transition_image_layout(m_commandBuffers[m_frameIndex], fit->second.colorImage,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits2::eColorAttachmentWrite, vk::AccessFlagBits2::eShaderRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eFragmentShader,
                vk::ImageAspectFlagBits::eColor);
        }
        m_imageInAttachment = false;
    }
    m_pendingFramebuffer = nullptr;
}

void VulkanAPI::cleanupSwapChain() {
    m_swapChainImageViews.clear();
    m_swapChain = nullptr;
    m_depthImageView = nullptr;
    m_depthImage = nullptr;
    m_depthImageMemory = nullptr;
}

void VulkanAPI::recreateSwapChain() {
    int w = 0, h = 0; SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
    while (w == 0 || h == 0) {
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) return;
        if (e.type == SDL_EVENT_QUIT) return;
        SDL_GetWindowSizeInPixels(m_window->GetSDLWindow(), &w, &h);
    }
    m_device.waitIdle(); cleanupSwapChain(); createSwapChain(); createImageViews(); createDepthResources();
    m_renderFinishedSemaphores.clear();
    m_renderFinishedSemaphores.reserve(m_swapChainImages.size());
    for (size_t i = 0; i < m_swapChainImages.size(); ++i) m_renderFinishedSemaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
    m_imageInAttachment = false;
}

bool VulkanAPI::InitImGuiBackend() {
    if (m_imguiInitialized) return true;
    VkInstance vkInstance = *m_instance; VkPhysicalDevice vkPhysical = *m_physicalDevice; VkDevice vkDevice = *m_device; VkQueue vkQueue = *m_queue;
    const uint32_t imageCount = static_cast<uint32_t>(m_swapChainImages.size());
    const VkFormat colorFmt = static_cast<VkFormat>(m_swapChainSurfaceFormat.format);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForVulkan(m_window->GetSDLWindow())) { logError(m_logger, "ImGui_ImplSDL3_InitForVulkan failed"); ImGui::DestroyContext(); return false; }
    VkDescriptorPoolSize poolSizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 }, { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 } };
    VkDescriptorPoolCreateInfo poolInfo{}; poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; poolInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes); poolInfo.poolSizeCount = IM_ARRAYSIZE(poolSizes); poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &m_imguiDescriptorPool) != VK_SUCCESS) { logError(m_logger, "ImGui descriptor pool creation failed"); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext(); return false; }
    ImGui_ImplVulkan_InitInfo initInfo{}; initInfo.ApiVersion = VK_API_VERSION_1_4; initInfo.Instance = vkInstance; initInfo.PhysicalDevice = vkPhysical; initInfo.Device = vkDevice; initInfo.QueueFamily = m_queueIndex; initInfo.Queue = vkQueue; initInfo.DescriptorPool = m_imguiDescriptorPool; initInfo.MinImageCount = imageCount; initInfo.ImageCount = imageCount; initInfo.UseDynamicRendering = true; initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT; initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO; initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1; initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFmt;
    if (!ImGui_ImplVulkan_Init(&initInfo)) { logError(m_logger, "[ImGui] ImGui_ImplVulkan_Init failed"); vkDestroyDescriptorPool(vkDevice, m_imguiDescriptorPool, nullptr); m_imguiDescriptorPool = VK_NULL_HANDLE; ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext(); return false; }
    m_imguiInitialized = true; logInfo(m_logger, "[RenderAPI] ImGui backend initialized"); return true;
}

void VulkanAPI::ShutdownImGuiBackend() {
    if (ImGui::GetCurrentContext() == nullptr) return;
    VkDevice vkDevice = *m_device; vkDeviceWaitIdle(vkDevice); ImGui_ImplVulkan_Shutdown(); ImGui_ImplSDL3_Shutdown();
    if (m_imguiDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(vkDevice, m_imguiDescriptorPool, nullptr); m_imguiDescriptorPool = VK_NULL_HANDLE; }
    ImGui::DestroyContext(); m_imguiInitialized = false; logInfo(m_logger, "[RenderAPI] ImGui backend shutdown");
}

void VulkanAPI::ImGuiNewFrame() {
    if (!m_imguiInitialized) return;
    ImGui_ImplVulkan_NewFrame(); ImGui_ImplSDL3_NewFrame();
}

void VulkanAPI::ImGuiRenderDrawData() {
    if (!m_imguiInitialized) return;
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->CmdListsCount == 0) return;
    VkCommandBuffer cmd = *m_commandBuffers[m_frameIndex];
    if (m_renderPassActive) { vkCmdEndRendering(cmd); m_renderPassActive = false; }
    VkRenderingAttachmentInfo colorAtt{}; colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO; colorAtt.imageView = static_cast<VkImageView>(*m_swapChainImageViews[m_imageIndex]); colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{}; ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO; ri.renderArea.extent = { m_swapChainExtent.width, m_swapChainExtent.height }; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &colorAtt;
    vkCmdBeginRendering(cmd, &ri); ImGui_ImplVulkan_RenderDrawData(drawData, cmd); vkCmdEndRendering(cmd);
}

[[nodiscard]] DeviceInfo VulkanAPI::GetDeviceInfo() const {
    DeviceInfo info; info.backend = "Vulkan";
    if (!*m_physicalDevice) { info.deviceName = "(uninitialized)"; return info; }
    VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(*m_physicalDevice, &props);
    VkPhysicalDeviceDriverProperties drv{}; drv.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 props2{}; props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; props2.pNext = &drv;
    vkGetPhysicalDeviceProperties2(*m_physicalDevice, &props2);
    info.deviceName = props.deviceName;
    switch (props.vendorID) {
        case 0x10DE: info.vendor = "NVIDIA"; break;
        case 0x1002: info.vendor = "AMD"; break;
        case 0x8086: info.vendor = "Intel"; break;
        case 0x13B5: info.vendor = "ARM"; break;
        case 0x5143: info.vendor = "Qualcomm"; break;
        default:     info.vendor = "0x" + [&]{ char buf[16]; std::snprintf(buf, sizeof(buf), "%04X", props.vendorID); return std::string(buf); }();
    }
    uint32_t api = props.apiVersion; info.apiVersion = std::to_string(VK_API_VERSION_MAJOR(api)) + "." + std::to_string(VK_API_VERSION_MINOR(api)) + "." + std::to_string(VK_API_VERSION_PATCH(api));
    info.driverVersion = drv.driverInfo; info.shadingLanguage = "SPIR-V 1.4"; info.extra = std::string(drv.driverName) + " / " + drv.driverInfo;
    return info;
}

void VulkanAPI::ApplySettings(const WindowConfig& win, const RenderConfig&  /*render*/) {
    if (m_windowConfig.vsync != win.vsync) { m_windowConfig.vsync = win.vsync; recreateSwapChain(); }
}

void VulkanAPI::WaitIdle() { if (*m_device) m_device.waitIdle(); }

Eng::client::BackendAvailability VulkanAPI::Probe() {
    Eng::client::BackendAvailability r;
    if (!SDL_Vulkan_LoadLibrary(nullptr)) { r.reason = std::string("Vulkan library not found: ") + SDL_GetError(); return r; }
    auto getInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (!getInstanceProcAddr) { SDL_Vulkan_UnloadLibrary(); r.reason = "Failed to get vkGetInstanceProcAddr"; return r; }
    auto pfnEnum = (PFN_vkEnumerateInstanceVersion)getInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
    uint32_t apiVer = VK_API_VERSION_1_0; if (pfnEnum) pfnEnum(&apiVer);
    if (apiVer < VK_MAKE_API_VERSION(0, api_major, api_minor, 0)) { SDL_Vulkan_UnloadLibrary(); r.reason = std::format("Vulkan Runtime too old ({}.{}.{}), need {}.{}", VK_VERSION_MAJOR(apiVer), VK_VERSION_MINOR(apiVer), VK_VERSION_PATCH(apiVer), api_major, api_minor); return r; }
    SDL_Vulkan_UnloadLibrary(); r.available = true; return r;
}

} // namespace Eng::client