#pragma once
#include <string>
#include <cstdint>

namespace Eng::tool {

enum class ErrorCode : uint8_t {
    // 文件 / IO
    FileNotFound,
    FileReadFailed,
    // Shader
    ShaderCompileFailed,
    ShaderLinkFailed,
    // 纹理 / 模型
    TextureLoadFailed,
    // Vulkan / GL
    ApiInitFailed,
    // 通用
    InvalidArgument,
    Unknown,
};

struct Error {
    ErrorCode   code;
    std::string message;

    static Error Make(ErrorCode c, std::string msg) {
        return { c, std::move(msg) };
    }
};

} // namespace Eng