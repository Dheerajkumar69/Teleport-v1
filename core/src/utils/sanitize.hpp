/**
 * @file sanitize.hpp
 * @brief Input sanitization utilities for security
 * 
 * Provides functions to sanitize user and remote input to prevent:
 * - Path traversal attacks (../, ..\)
 * - Null byte injection
 * - Invalid filename characters
 */

#ifndef TELEPORT_SANITIZE_HPP
#define TELEPORT_SANITIZE_HPP

#include <string>
#include <algorithm>
#include <cctype>
#include <cstdint>

namespace teleport {

/**
 * @brief Sanitize a filename received from remote source
 * 
 * Removes or replaces:
 * - Path separators (/, \)
 * - Parent directory references (..)
 * - Null bytes
 * - Leading/trailing spaces and dots
 * - Invalid characters for Windows/Unix
 * 
 * @param filename Raw filename from remote source
 * @return Sanitized safe filename
 */
inline std::string sanitize_filename(const std::string& filename) {
    if (filename.empty()) {
        return "unnamed";
    }
    
    std::string result;
    result.reserve(filename.size());
    
    // Characters invalid on Windows/Unix filesystems
    const std::string invalid_chars = "<>:\"/\\|?*\0";
    
    for (size_t i = 0; i < filename.size(); ++i) {
        char c = filename[i];
        
        // Skip null bytes and control characters
        if (c == '\0' || (c >= 0 && c < 32)) {
            continue;
        }
        
        // Replace path separators with underscore
        if (c == '/' || c == '\\') {
            result += '_';
            continue;
        }
        
        // Skip other invalid characters
        if (invalid_chars.find(c) != std::string::npos) {
            result += '_';
            continue;
        }
        
        result += c;
    }
    
    // Remove leading dots and spaces (hidden files, relative paths)
    size_t start = 0;
    while (start < result.size() && (result[start] == '.' || result[start] == ' ')) {
        start++;
    }
    if (start > 0) {
        result = result.substr(start);
    }
    
    // Remove trailing dots and spaces (Windows issue)
    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) {
        result.pop_back();
    }
    
    // Check for reserved Windows names
    std::string upper = result;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    const std::string reserved[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    
    for (const auto& name : reserved) {
        if (upper == name || upper.substr(0, name.size() + 1) == name + ".") {
            result = "_" + result;
            break;
        }
    }
    
    // Enforce maximum length (255 bytes for most filesystems)
    if (result.size() > 240) {
        // Preserve extension if present
        auto dot_pos = result.rfind('.');
        if (dot_pos != std::string::npos && result.size() - dot_pos <= 10) {
            std::string ext = result.substr(dot_pos);
            result = result.substr(0, 240 - ext.size()) + ext;
        } else {
            result = result.substr(0, 240);
        }
    }
    
    // Final check
    if (result.empty() || result == "." || result == "..") {
        return "unnamed";
    }
    
    return result;
}

/**
 * @brief Validate and sanitize an IP address string
 * @param ip Raw IP string
 * @return true if valid IPv4 format
 */
inline bool validate_ipv4(const std::string& ip) {
    if (ip.empty() || ip.size() > 15) return false;
    
    int dots = 0;
    int num = 0;
    bool in_number = false;
    
    for (char c : ip) {
        if (c == '.') {
            if (!in_number || num > 255) return false;
            dots++;
            num = 0;
            in_number = false;
        } else if (c >= '0' && c <= '9') {
            num = num * 10 + (c - '0');
            in_number = true;
            if (num > 255) return false;
        } else {
            return false;
        }
    }
    
    return dots == 3 && in_number && num <= 255;
}

/**
 * @brief Validate a port number
 */
inline bool validate_port(uint16_t port) {
    return port > 0 && port <= 65535;
}

/**
 * @brief Sanitize a device name for display
 */
inline std::string sanitize_device_name(const std::string& name) {
    if (name.empty()) {
        return "Unknown Device";
    }
    
    std::string result;
    result.reserve(std::min(name.size(), size_t(64)));
    
    for (char c : name) {
        // Allow alphanumeric, spaces, dashes, underscores
        if (std::isalnum(static_cast<unsigned char>(c)) || 
            c == ' ' || c == '-' || c == '_' || c == '.') {
            result += c;
        }
        
        if (result.size() >= 64) break;
    }
    
    // Trim spaces
    while (!result.empty() && result.front() == ' ') result.erase(0, 1);
    while (!result.empty() && result.back() == ' ') result.pop_back();
    
    return result.empty() ? "Unknown Device" : result;
}

} // namespace teleport

#endif // TELEPORT_SANITIZE_HPP
