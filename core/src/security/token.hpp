/**
 * @file token.hpp
 * @brief Session token generation and validation
 */

#ifndef TELEPORT_TOKEN_HPP
#define TELEPORT_TOKEN_HPP

#include <string>
#include <random>
#include <sstream>
#include <iomanip>

namespace teleport {

/**
 * @brief Generate a random session token
 * @param length Token length in hex characters (default: 32 = 128 bits)
 * @return Hex string token
 */
inline std::string generate_session_token(size_t length = 32) {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    static thread_local std::uniform_int_distribution<uint64_t> dist;
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    // Generate 16 hex chars (64 bits) at a time
    size_t remaining = length;
    while (remaining > 0) {
        uint64_t val = dist(gen);
        size_t chars = std::min(remaining, size_t(16));
        oss << std::setw(static_cast<int>(chars)) << (val >> (64 - chars * 4));
        remaining -= chars;
    }
    
    std::string result = oss.str();
    return result.substr(0, length);  // Ensure exact length
}

/**
 * @brief Validate a session token format
 * @param token Token to validate
 * @param expected_length Expected length (default: 32)
 */
inline bool validate_token_format(const std::string& token, size_t expected_length = 32) {
    if (token.size() != expected_length) return false;
    
    for (char c : token) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    
    return true;
}

} // namespace teleport

#endif // TELEPORT_TOKEN_HPP
