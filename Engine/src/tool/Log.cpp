#include "Engine/tool/Log.h"
#include "Engine/tool/FileTool.h"
#include <chrono>
#include <format>
#include <SDL3/SDL_log.h>

namespace Eng::tool {

    namespace {
        std::string GetTime() {
            using namespace std::chrono;
            auto now = system_clock::now();
            auto secs = floor<seconds>(now);
            auto ms = duration_cast<milliseconds>(now - secs);

            static const auto tz = current_zone();
            auto local = zoned_time{tz, secs};

            return std::format("{:%m-%d %H:%M:%S}.{:03d}", local.get_local_time(), ms.count());
        }

        constexpr const char* ColorCode(LogLevel lv) {
            switch (lv) {
                case LogLevel::FATAL:
                case LogLevel::ERROR:   return "\033[31m";   // 红
                case LogLevel::WARNING: return "\033[33m";   // 黄
                case LogLevel::INFO:    return "\033[90m";   // 灰
                case LogLevel::DEBUG:   return "\033[36m";   // 青
            }
            return "";
        }
        constexpr const char* kReset = "\033[0m";

        constexpr std::string LevelToString(LogLevel level) {
            switch (level) {
                case LogLevel::DEBUG:   return "DEBUG";
                case LogLevel::INFO:    return "INFO";
                case LogLevel::WARNING: return "WARN";
                case LogLevel::ERROR:   return "ERROR";
                case LogLevel::FATAL:   return "FATAL";
                default: return "UNKNOWN";
            }
        }

        // 辅助函数：安全打开日志文件
        static SDL_IOStream* OpenLogFile(const std::string& filename) {
            std::string fullPath = tool::GetWritablePath("logs/" + filename);
#if defined(__ANDROID__) || defined(__APPLE__)
            std::string dirPath = tool::GetWritablePath("logs");
            SDL_CreateDirectory(dirPath.c_str());
#endif
            return SDL_IOFromFile(fullPath.c_str(), "a"); // "a" 代表追加模式
        }

    }


Log::Log(bool console) {
    m_consoleOutput = console;
}

Log::Log(const std::string& filepath, bool console) {
    m_consoleOutput = console;
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_currentFilePath = filepath;
    m_file = OpenLogFile(filepath);
    if (!m_file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open log file: %s", filepath.c_str());
    }
}

Log::~Log() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file) {
        SDL_CloseIO(m_file);
        m_file = nullptr;
    }
}

void Log::SetConsoleOutput(bool enable) {
    m_consoleOutput = enable;
}

void Log::SetFileOutput(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentFilePath = filepath;
    if (m_file) {
        SDL_CloseIO(m_file);
        m_file = nullptr;
    }
    m_file = OpenLogFile(filepath);
}

void Log::SetMinLevel(LogLevel level) {
    m_minLevel = level;
}

bool Log::isEnabled(LogLevel level) const {
    return level >= m_minLevel;
}

void Log::log(LogLevel level, const std::string& message) {
    if (level < m_minLevel) return;
    out(level, message);
}

void Log::out(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::string timeStr = GetTime();
    std::string levelStr = LevelToString(level);

    // 1. 写入文件
    if (m_file) {
        std::string fileLine = "[" + timeStr + " " + levelStr + "]: " + message + "\n";
        SDL_WriteIO(m_file, fileLine.c_str(), fileLine.size());
        SDL_FlushIO(m_file);
    }

    // 2. 输出到控制台
    if (m_consoleOutput) {
        std::string consoleLine = std::string(ColorCode(level)) 
                                + "[" + timeStr + " " + levelStr + "]: " 
                                + message + kReset;
        SDL_Log("%s", consoleLine.c_str());
    }
}

void Log::StaticLog(LogLevel level, const std::string& message) {
    // 静态方法无法访问实例的 m_minLevel，默认 INFO 以上才输出
    if (level < LogLevel::INFO) return;

    std::string timeStr = GetTime();
    std::string levelStr = LevelToString(level);

    std::string consoleLine = std::string(ColorCode(level)) 
                            + "[" + timeStr + " " + levelStr + "]: " 
                            + message + kReset;
    SDL_Log("%s", consoleLine.c_str());
}
    
} // namespace Eng::tool