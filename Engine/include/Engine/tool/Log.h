#pragma once
#include <string>
#include <mutex>
#include <sstream> // IWYU pragma: keep
#include <cstdint>
#include <SDL3/SDL_iostream.h>

namespace Eng::tool {

enum class LogLevel : std::uint8_t {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

class Log {
public:
    Log(bool console = true);
    Log(const std::string& filepath, bool console = true);
    ~Log();

    void SetConsoleOutput(bool enable);
    void SetFileOutput(const std::string& filepath);
    void SetMinLevel(LogLevel level);
    bool isEnabled(LogLevel level) const;

    void log(LogLevel level, const std::string& message);

    static void StaticLog(LogLevel level, const std::string& message);

private:
    void out(LogLevel level, const std::string& message);
    
    SDL_IOStream* m_file = nullptr; 
    std::string m_currentFilePath;  
    std::mutex m_mutex;
    bool m_consoleOutput = true;
#ifdef _DEBUG
    LogLevel m_minLevel = LogLevel::DEBUG;
#else
    LogLevel m_minLevel = LogLevel::INFO;
#endif
};

// ==========================================
// 实例日志宏 (依赖 Logger 实例)
// ==========================================
#define LOG_IMPL(logger, level, message) do { \
        auto&& _log_logger = (logger); \
        if (_log_logger->isEnabled(level)) { \
            std::ostringstream _log_oss; \
            _log_oss << message; \
            _log_logger->log(level, _log_oss.str()); \
        } \
    } while(0)

#define logDebug(logger, message)   LOG_IMPL(logger, Eng::tool::LogLevel::DEBUG,   message)
#define logInfo(logger, message)    LOG_IMPL(logger, Eng::tool::LogLevel::INFO,    message)
#define logWarning(logger, message) LOG_IMPL(logger, Eng::tool::LogLevel::WARNING, message)
#define logError(logger, message)   LOG_IMPL(logger, Eng::tool::LogLevel::ERROR,   message)
#define logFatal(logger, message)   LOG_IMPL(logger, Eng::tool::LogLevel::FATAL,   message)


// ==========================================
// 静态日志宏 (无需 Logger 实例)
// ==========================================
#define STATIC_LOG_IMPL(level, message) do { \
        std::ostringstream _log_oss; \
        _log_oss << message; \
        Eng::tool::Log::StaticLog(level, _log_oss.str()); \
    } while(0)

#define stLogDebug(message)   STATIC_LOG_IMPL(Eng::tool::LogLevel::DEBUG,   message)
#define stLogInfo(message)    STATIC_LOG_IMPL(Eng::tool::LogLevel::INFO,    message)
#define stLogWarning(message) STATIC_LOG_IMPL(Eng::tool::LogLevel::WARNING, message)
#define stLogError(message)   STATIC_LOG_IMPL(Eng::tool::LogLevel::ERROR,   message)
#define stLogFatal(message)   STATIC_LOG_IMPL(Eng::tool::LogLevel::FATAL,   message)

}