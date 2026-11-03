#include "Engine/tool/Platform.h"

// ==========================================
// SDL 头文件（按你项目版本选一条）
//   SDL2: #include <SDL.h>
//   SDL3: #include <SDL3/SDL.h>
// ==========================================
#include <SDL3/SDL.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <algorithm>
#include <cstring>

namespace Eng::tool {

// ==========================================
// 控制台（保留平台隔离的 <windows.h>）
// ==========================================
void EnableAnsiConsole() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif
}

void SetConsoleUTF8Output() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

// ==========================================
// CPU / 平台
// ==========================================
uint32_t GetHardwareConcurrency() {
    // SDL_GetCPUCount 返回 SDL_GetSystemRAM 那套系统探测结果：
    //   Windows 走 GetSystemInfo.dwNumberOfProcessors
    //   Linux/Android 走 sysconf(_SC_NPROCESSORS_ONLN)
    //   macOS/iOS 走 sysctl
    // 说明：Windows 在 >64 核（多处理器组）场景下 SDL2 会被截断到当前组。
    // 若你的目标包含 Win11 工作站级多路 CPU，可在此处覆盖成
    // GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)；普通工作站无需关心。
    static const uint32_t kCached = []() -> uint32_t {
        int n = SDL_GetNumLogicalCPUCores();   // SDL3 新名称
        return (n > 0) ? static_cast<uint32_t>(n) : 1u;
    }();
    return kCached;
}

bool IsMobilePlatform() {
    static const bool kCached = []() -> bool {
        const char* p = SDL_GetPlatform();
        if (!p) return false;
        return std::strcmp(p, "Android") == 0
            || std::strcmp(p, "iOS")     == 0
            || std::strcmp(p, "tvOS")    == 0;
    }();
    return kCached;
}

uint32_t GetRecommendedWorkerThreads() {
    const uint32_t hw = GetHardwareConcurrency();

    if (IsMobilePlatform()) {
        // 大小核异构：限 4 防止把 worker 撒到 E-core，
        // 也让 UI / 主线程有核可用。
        return std::min(hw, 4u);
    }

    // 桌面端：main + render 各留一核。
    return hw >= 3u ? hw - 2u : 1u;
}

// ==========================================
// CPU 指令集（薄包装，便于将来切换探测后端）
// ==========================================
// SDL_Has* 系列不需要 SDL_Init()。首次调用后缓存到 static。
bool HasAVX2()     { static const bool v = SDL_HasAVX2();     return v; }
bool HasAVX512F()  { static const bool v = SDL_HasAVX512F();  return v; }
bool HasSSE42()    { static const bool v = SDL_HasSSE42();    return v; }
bool HasNEON()     { static const bool v = SDL_HasNEON();     return v; }
bool HasARMSIMD()  { static const bool v = SDL_HasARMSIMD();  return v; }

} // namespace Eng::tool