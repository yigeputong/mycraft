#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <iomanip>
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


#define logDebug(logger, message) \
    do { \
        std::stringstream ss; \
        ss << message; \
        logger->log(Eng::LogLevel::DEBUG, ss.str()); \
    } while(0)

#define logInfo(logger, message) \
    do { \
        std::stringstream ss; \
        ss << message; \
        logger->log(Eng::LogLevel::INFO, ss.str()); \
    } while(0)

#define logWarning(logger, message) \
    do { \
        std::stringstream ss; \
        ss << message; \
        logger->log(Eng::LogLevel::WARNING, ss.str()); \
    } while(0)

#define logError(logger, message) \
    do { \
        std::stringstream ss; \
        ss << message; \
        logger->log(Eng::LogLevel::ERROR, ss.str()); \
    } while(0)

#define logFatal(logger, message) \
    do { \
        std::stringstream ss; \
        ss << message; \
        logger->log(Eng::LogLevel::FATAL, ss.str()); \
    } while(0)

}