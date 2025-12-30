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
 * @return 32-character hex token
 */
inline std::string generate_session_token() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    static thread_local std::uniform_int_distribution<uint64_t> dist;
    
    uint64_t part1 = dist(gen);
    uint64_t part2 = dist(gen);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(16) << part1;
    oss << std::setw(16) << part2;
    
    return oss.str();
}

/**
 * @brief Validate a session token format
 */
inline bool validate_token_format(const std::string& token) {
    if (token.size() != 32) return false;
    
    for (char c : token) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    
    return true;
}

} // namespace teleport

#endif // TELEPORT_TOKEN_HPP
