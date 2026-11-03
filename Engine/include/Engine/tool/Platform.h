#pragma once
#include <cstdint>

namespace Eng::tool {

// ==========================================
// 控制台（Windows 有效，其他平台 no-op）
// ==========================================
void EnableAnsiConsole();
void SetConsoleUTF8Output();

// ==========================================
// 平台信息 / 调度建议（底层走 SDL）
// ==========================================

// 逻辑核数（含超线程）。SDL_GetCPUCount() 在极端容器环境可能返回 0，
// 这里兜底为 1，保证调用方拿到有意义的值。
uint32_t GetHardwareConcurrency();

// Android / iOS / iPadOS 为 true。底层走 SDL_GetPlatform()。
bool IsMobilePlatform();

// Worker 线程推荐数：
//   桌面端: max(1, hw - 2)  （给主线程和渲染线程预留 2 核）
//   移动端: min(hw, 4)      （大小核异构，限制上限防 E-core 抖动）
uint32_t GetRecommendedWorkerThreads();

// ==========================================
// CPU 指令集探测（供 SIMD 运行时分发使用）
// ==========================================
// 不需要 SDL_Init() 即可调用；值在首次探测后缓存。
// 用途示例：
//   在 TerrainGenerator 构造函数里：
//     若 HasAVX2() → 选 AVX2 版本的 ComputeDensityBatch
//     否则          → 标量回退
bool HasAVX2();
bool HasAVX512F();
bool HasSSE42();
bool HasNEON();       // ARM64 强开，返回 true；x86 恒为 false
bool HasARMSIMD();    // AArch32 NEON 扩展

} // namespace Eng::tool