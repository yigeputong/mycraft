#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Eng::tool {

    // 读取整个文件到内存（适合读 JSON、SPV 等）
    // 返回空 vector 表示读取失败
    std::vector<uint8_t> ReadFileToMemory(const std::string& path);

    // 把内存数据写入文件（适合保存 settings.json 等）
    bool WriteMemoryToFile(const std::string& path, const std::vector<uint8_t>& data);
    bool WriteStringToFile(const std::string& path, const std::string& content);

    // 获取 Android 可写的沙盒路径（桌面端直接返回空字符串，意味着用当前路径）
    std::string GetWritablePath(const std::string& filename);

} // namespace Eng::tool