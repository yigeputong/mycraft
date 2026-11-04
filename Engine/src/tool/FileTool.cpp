#include "Engine/tool/FileTool.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_log.h>
#include <string>
#include "Engine/tool/Platform.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace Eng::tool {

    std::vector<uint8_t> ReadFileToMemory(const std::string& path) {
        // SDL3 的 SDL_IOFromFile 在 iOS 下会自动去 .app 的 bundle 里找 assets 目录
        SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
        if (!io) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[File] Failed to open: %s - %s", path.c_str(), SDL_GetError());
            return {};
        }

        Sint64 size = SDL_GetIOSize(io);
        if (size <= 0) {
            SDL_CloseIO(io);
            return {};
        }

        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        size_t readBytes = SDL_ReadIO(io, buffer.data(), static_cast<size_t>(size));
        SDL_CloseIO(io);

        if (readBytes != static_cast<size_t>(size)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[File] Read incomplete for: %s", path.c_str());
            return {};
        }

        return buffer;
    }

    bool WriteMemoryToFile(const std::string& path, const std::vector<uint8_t>& data) {
        SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "wb");
        if (!io) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[File] Failed to open for writing: %s - %s", path.c_str(), SDL_GetError());
            return false;
        }

        size_t written = SDL_WriteIO(io, data.data(), data.size());
        SDL_CloseIO(io);
        
        if (written != data.size()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[File] Write incomplete for: %s", path.c_str());
            return false;
        }
        return true;
    }

    bool WriteStringToFile(const std::string& path, const std::string& content) {
        std::vector<uint8_t> data(content.begin(), content.end());
        return WriteMemoryToFile(path, data);
    }

    void EnsureDirectoryExists(const std::string& dirPath) {
        std::string fullPath = GetWritablePath(dirPath);
        // SDL_CreateDirectory 默认只能创建一层，这里为了防错，暴力递归调用一下
        // 或者直接用它自带的 SDL_EnumerateDirectory 行为，如果存在就返回 true
        SDL_CreateDirectory(fullPath.c_str()); 
    }

    std::string GetWritablePath(const std::string& filename) {
        if (Eng::tool::IsMobilePlatform()) {
            // 移动端必须写入 App 的内部沙盒路径
            // SDL_GetPrefPath 在 iOS 上会返回 App 的 Documents 目录，在 Android 上返回 /data/data/包名/files/
            char* prefPath = SDL_GetPrefPath("mycraft", "mycraft");
            if (prefPath) {
                std::string fullPath = std::string(prefPath) + filename;
                SDL_free(prefPath);
                return fullPath;
            }
            return filename;
        } else {
            // Windows / macOS / Linux 直接在当前工作目录读写
            return filename;
        }
    }

} // namespace Eng::tool