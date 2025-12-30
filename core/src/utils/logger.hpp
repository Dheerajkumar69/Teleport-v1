/**
 * @file logger.hpp
 * @brief Simple thread-safe logging utility with file/line tracking
 * 
 * Features:
 * - Thread-safe logging with mutex protection
 * - Configurable log levels (Debug, Info, Warning, Error)
 * - File and line number tracking for ALL log levels
 * - Timestamp with millisecond precision
 * - Variadic argument support
 */

#ifndef TELEPORT_LOGGER_HPP
#define TELEPORT_LOGGER_HPP

#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <mutex>
#include <functional>

namespace teleport {

enum class LogLevel {
    Debug   = 0,
    Info    = 1,
    Warning = 2,
    Error   = 3
};

/**
 * @brief Log output callback type
 */
using LogCallback = std::function<void(LogLevel, const std::string&)>;

/**
 * @brief Simple thread-safe logger with file/line tracking
 */
class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
    
    void set_level(LogLevel level) { m_level = level; }
    LogLevel level() const { return m_level; }
    
    void set_prefix(const std::string& prefix) { m_prefix = prefix; }
    
    /**
     * @brief Set whether to include file:line in all log messages
     * @param enabled true to include file:line for all levels (default: true)
     */
    void set_source_location_enabled(bool enabled) { m_show_source = enabled; }
    
    /**
     * @brief Set custom log callback for redirecting output
     */
    void set_callback(LogCallback callback) { m_callback = std::move(callback); }
    
    template<typename... Args>
    void log(LogLevel level, const char* file, int line, const char* func, Args&&... args) {
        if (level < m_level) return;
        
        std::lock_guard<std::mutex> lock(m_mutex);
        
        std::ostringstream oss;
        
        // Timestamp with milliseconds
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        
        // Log level
        oss << ' ' << level_to_string(level);
        
        // Prefix (component name)
        if (!m_prefix.empty()) {
            oss << " [" << m_prefix << "]";
        }
        
        // Source location - ALWAYS included for traceability
        if (m_show_source) {
            oss << " (" << extract_filename(file) << ":" << line << ")";
        }
        
        oss << " ";
        
        // Append all message arguments
        ((oss << std::forward<Args>(args)), ...);
        
        std::string message = oss.str();
        
        // Custom callback or default output
        if (m_callback) {
            m_callback(level, message);
        } else {
            if (level >= LogLevel::Error) {
                std::cerr << message << std::endl;
            } else {
                std::cout << message << std::endl;
            }
        }
    }
    
    // Overload without function name for backward compatibility
    template<typename... Args>
    void log(LogLevel level, const char* file, int line, Args&&... args) {
        log(level, file, line, "", std::forward<Args>(args)...);
    }
    
private:
    Logger() : m_level(LogLevel::Info), m_show_source(true) {}
    
    static const char* level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::Debug:   return "[DEBUG]";
            case LogLevel::Info:    return "[INFO ]";
            case LogLevel::Warning: return "[WARN ]";
            case LogLevel::Error:   return "[ERROR]";
            default:                return "[?????]";
        }
    }
    
    static std::string extract_filename(const char* path) {
        if (!path) return "unknown";
        std::string p(path);
        auto pos = p.find_last_of("/\\");
        return pos != std::string::npos ? p.substr(pos + 1) : p;
    }
    
    LogLevel m_level;
    std::string m_prefix;
    std::mutex m_mutex;
    bool m_show_source;
    LogCallback m_callback;
};

// Convenience macros - ALWAYS include file and line
#define LOG_DEBUG(...) teleport::Logger::instance().log(teleport::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  teleport::Logger::instance().log(teleport::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  teleport::Logger::instance().log(teleport::LogLevel::Warning, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) teleport::Logger::instance().log(teleport::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)

// Extended macro with function name
#define LOG_DEBUG_F(...) teleport::Logger::instance().log(teleport::LogLevel::Debug, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO_F(...)  teleport::Logger::instance().log(teleport::LogLevel::Info, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN_F(...)  teleport::Logger::instance().log(teleport::LogLevel::Warning, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR_F(...) teleport::Logger::instance().log(teleport::LogLevel::Error, __FILE__, __LINE__, __func__, __VA_ARGS__)

} // namespace teleport

#endif // TELEPORT_LOGGER_HPP
