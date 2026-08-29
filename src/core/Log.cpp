#include "core/Log.h"
#include <iostream>
#include <chrono>
#include <format>
#include <filesystem>

namespace Eng {

std::string GetTime() {
    using namespace std::chrono;

    auto now = system_clock::now();

    auto secs = floor<seconds>(now);

    auto ms = duration_cast<milliseconds>(now - secs);

    return std::format("{:%m-%d %H:%M:%S}.{:03d}", secs, ms.count());
}

Log::Log(bool console) {
    m_consoleOutput = console;
}
Log::Log(std::string filepath, bool console) {
    m_consoleOutput = console;
    std::lock_guard<std::mutex> lock(m_mutex);
    std::filesystem::create_directories("./logs/");
    if (m_file.is_open()) {
        m_file.close();
    }
    m_file.open("./logs/" + filepath, std::ios::out | std::ios::app);
    if (!m_file.is_open()) {
        std::cerr << "Failed to open log file: " << filepath << std::endl;
    }
}
Log::~Log() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

void Log::SetConsoleOutput(bool enable) {
    m_consoleOutput = enable;
}
void Log::SetFileOutput(const std::string filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::filesystem::create_directories("./logs/");
    if (m_file.is_open()) {
        m_file.close();
    }
    m_file.open("./logs/" + filepath, std::ios::out | std::ios::app);
    if (!m_file.is_open()) {
        std::cerr << "Failed to open log file: " << filepath << std::endl;
    }
}
void Log::SetMinLevel(LogLevel level) {
    m_minLevel = level;
}

void Log::log(LogLevel level, std::string message) {
    switch (level) {
#ifdef _DEBUG
    case LogLevel::DEBUG:
        if (level >= m_minLevel) {
            out(level, message);
        }
        break;
#endif
    case LogLevel::INFO:
        if (level >= m_minLevel) {
            out(level, message);
        }
        break;
    case LogLevel::WARNING:
        if (level >= m_minLevel) {
            out(level, message);
        }
        break;
    case LogLevel::ERROR:
        if (level >= m_minLevel) {
            out(level, message);
        }
        break;
    case LogLevel::FATAL:
        if (level >= m_minLevel) {
            out(level, message);
        }
        break;
    }
}

void Log::out(LogLevel level, std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) {
        m_file << "[" << GetTime() << " " << LevelToString(level) << "]: " << message << std::endl;
    }
    if (m_consoleOutput) {
        std::cout << "[" << GetTime() << " " << LevelToString(level) << "]: " << message << std::endl;
    }
}

std::string Log::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default: return "UNKNOWN";
    }
}
    
} // namespace Eng
