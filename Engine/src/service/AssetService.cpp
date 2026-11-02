#include "Engine/service/AssetService.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3_image/SDL_image.h>

#include <cstdint>
#include <cstddef>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Eng::service {

namespace {

    uint64_t HashPipelineState(const rhi::GraphicsPipelineState& s) noexcept {
        uint64_t h = 14695981039346656037ull;
        auto mix = [&h](uint64_t v) noexcept {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(s.depthStencil.depthTestEnable  ? 1u : 0u);
        mix(s.depthStencil.depthWriteEnable ? 1u : 0u);
        mix(static_cast<uint64_t>(s.depthStencil.depthCompareOp));
        mix(s.depthStencil.stencilTestEnable ? 1u : 0u);
        mix(static_cast<uint64_t>(s.rasterizer.cullMode));
        mix(static_cast<uint64_t>(s.rasterizer.frontFace));
        mix(s.blend.blendEnable ? 1u : 0u);
        mix(static_cast<uint64_t>(s.blend.srcColorBlendFactor));
        mix(static_cast<uint64_t>(s.blend.dstColorBlendFactor));
        mix(static_cast<uint64_t>(s.blend.colorBlendOp));
        mix(static_cast<uint64_t>(s.blend.srcAlphaBlendFactor));
        mix(static_cast<uint64_t>(s.blend.dstAlphaBlendFactor));
        mix(static_cast<uint64_t>(s.blend.alphaBlendOp));
        mix(static_cast<uint64_t>(s.blend.colorWriteMask));
        return h;
    }

} // anonymous namespace

AssetService::AssetService(IRenderService* rhi) : m_rhi(rhi) {}

// ============================================================
// 纹理
// ============================================================
TexturePtr AssetService::LoadTexture(const std::string& path) {
    if (!m_rhi) {
        logError(m_logger, "[AssetService] RHI is null, cannot load texture.");
        return nullptr;
    }

    auto it = m_textureCache.find(path);
    if (it != m_textureCache.end()) return it->second;

    SDL_Surface* loaded = IMG_Load(path.c_str());
    if (!loaded) {
        logError(m_logger, "[AssetService] IMG_Load failed: " << path << " - " << SDL_GetError());
        return nullptr;
    }

    SDL_Surface* surf = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!surf) {
        logError(m_logger, "[AssetService] SDL_ConvertSurface failed: " << SDL_GetError());
        return nullptr;
    }

    const size_t byteCount = static_cast<size_t>(surf->w) * surf->h * 4;
    rhi::TextureCreateInfo tci;
    tci.pixels = std::as_bytes(std::span<const uint8_t>(
        static_cast<const uint8_t*>(surf->pixels), byteCount));
    tci.width         = surf->w;
    tci.height        = surf->h;
    tci.requestedMips = 0;
    tci.format        = rhi::TextureFormat::RGBA8_UNORM;

    TexturePtr tex = m_rhi->CreateTexture(tci);
    SDL_DestroySurface(surf);

    if (tex) m_textureCache[path] = tex;
    else     logError(m_logger, "[AssetService] Failed to create texture: " << path);
    return tex;
}

// ============================================================
// 着色器
// ============================================================
ShaderPtr AssetService::LoadShader(const rhi::ShaderCreateInfo& info) {
    if (!m_rhi) {
        logError(m_logger, "[AssetService] RHI is null, cannot load shader.");
        return nullptr;
    }
    if (!info.IsValid()) {
        logError(m_logger, "[AssetService] ShaderCreateInfo invalid (empty SPIR-V). name="
                           << info.debugName);
        return nullptr;
    }

    const auto effectiveState = info.pipelineState.value_or(
        rhi::GraphicsPipelineState::PresetOpaque());
    const uint64_t stateHash = HashPipelineState(effectiveState);

    // ★ std::format 取代字符串加号拼 + std::to_string
    const std::string cacheKey = std::format("{}|{}|{}",
        info.debugName, stateHash, info.pushConstantSize);

    auto it = m_shaderCache.find(cacheKey);
    if (it != m_shaderCache.end()) return it->second;

    auto shader = m_rhi->CreateShader(info);
    if (shader) m_shaderCache[cacheKey] = shader;
    else        logError(m_logger, "[AssetService] Failed to load shader: " << info.debugName);
    return shader;
}

// ============================================================
// ReadFile —— SDL_IOStream 读盘去重
// ============================================================
std::shared_ptr<const std::vector<std::byte>>
AssetService::ReadFile(const std::string& path) {
    if (auto it = m_fileReadCache.find(path); it != m_fileReadCache.end()) {
        if (auto locked = it->second.lock()) return locked;
    }

    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) {
        logError(m_logger, "[AssetService] ReadFile: SDL_IOFromFile failed: "
                           << path << " - " << SDL_GetError());
        m_fileReadCache.erase(path);
        return nullptr;
    }

    const Sint64 size = SDL_GetIOSize(io);
    if (size <= 0) {
        logError(m_logger, "[AssetService] ReadFile: SDL_GetIOSize failed or empty: "
                           << path << " - " << SDL_GetError());
        SDL_CloseIO(io);
        m_fileReadCache.erase(path);
        return nullptr;
    }

    auto buf = std::make_shared<std::vector<std::byte>>(static_cast<size_t>(size));
    size_t total = 0;
    const size_t expected = static_cast<size_t>(size);
    while (total < expected) {
        const size_t got = SDL_ReadIO(io, buf->data() + total, expected - total);
        if (got == 0) {
            logError(m_logger, "[AssetService] ReadFile: short read at "
                               << total << "/" << expected << " : " << path);
            SDL_CloseIO(io);
            m_fileReadCache.erase(path);
            return nullptr;
        }
        total += got;
    }
    SDL_CloseIO(io);

    std::shared_ptr<const std::vector<std::byte>> frozen = buf;
    m_fileReadCache[path] = frozen;
    return frozen;
}

// ============================================================
// MakeShaderCreateInfo
// ============================================================
rhi::ShaderCreateInfo AssetService::MakeShaderCreateInfo(
    const std::string& name,
    rhi::GraphicsPipelineState pipelineState,
    rhi::VertexLayout vertexLayout,
    uint32_t          pushConstantSize) {

    const bool isVulkan = (m_rhi && m_rhi->GetDeviceInfo().backend == "Vulkan");
    const std::string ext  = isVulkan ? ".vk.spv" : ".gl.spv";
    const std::string base = "assets/shaders/build/" + name + "/";

    rhi::ShaderCreateInfo info;
    info.debugName        = name;
    info.vertexLayout     = vertexLayout;
    info.pipelineState    = pipelineState;
    info.pushConstantSize = pushConstantSize;

    if (auto v = ReadFile(base + "vert" + ext)) {
        info.vertSpirv = rhi::ShaderBytecode(std::move(v));
    } else {
        logError(m_logger, "[AssetService] MakeShaderCreateInfo: missing vert SPIR-V for '"
                           << name << "'");
    }
    if (auto fr = ReadFile(base + "frag" + ext)) {
        info.fragSpirv = rhi::ShaderBytecode(std::move(fr));
    } else {
        logError(m_logger, "[AssetService] MakeShaderCreateInfo: missing frag SPIR-V for '"
                           << name << "'");
    }

    return info;
}

} // namespace Eng::service