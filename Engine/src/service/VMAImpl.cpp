// 唯一一个生成 VMA 实现的翻译单元
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include <volk.h>
#if defined(_WIN32)
#include <vma/vk_mem_alloc.h>
#else
#include <vk_mem_alloc.h>
#endif