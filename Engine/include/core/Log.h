#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <sstream>

namespace Eng {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

class Log {
public:
    Log(bool console = true);
    Log(std::string filepath, bool console = true);
    ~Log();

    void SetConsoleOutput(bool enable);
    void SetFileOutput(const std::string filepath);
    void SetMinLevel(LogLevel level);
    bool isEnabled(LogLevel level) const ;

    void log(LogLevel level, std::string message);
private:
    void out(LogLevel level, std::string& message);
    std::string LevelToString(LogLevel level);
    std::ofstream m_file;
    std::mutex m_mutex;
    bool m_consoleOutput = true;
#ifdef _DEBUG
    LogLevel m_minLevel = LogLevel::DEBUG;
#else
    LogLevel m_minLevel = LogLevel::INFO;
#endif
};


#define LOG_IMPL(logger, level, message) do { \
        auto&& _log_logger = (logger); \
        if (_log_logger->isEnabled(level)) { \
            std::ostringstream _log_oss; \
            _log_oss << message; \
            _log_logger->log(level, _log_oss.str()); \
        } \
    } while(0)

#define logDebug(logger, message)   LOG_IMPL(logger, Eng::LogLevel::DEBUG,   message)
#define logInfo(logger, message)    LOG_IMPL(logger, Eng::LogLevel::INFO,    message)
#define logWarning(logger, message) LOG_IMPL(logger, Eng::LogLevel::WARNING, message)
#define logError(logger, message)   LOG_IMPL(logger, Eng::LogLevel::ERROR,   message)
#define logFatal(logger, message)   LOG_IMPL(logger, Eng::LogLevel::FATAL,   message)

}