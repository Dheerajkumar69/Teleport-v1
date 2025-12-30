/**
 * @file uuid.hpp
 * @brief UUID v4 generation utility
 */

#ifndef TELEPORT_UUID_HPP
#define TELEPORT_UUID_HPP

#include <string>
#include <random>
#include <sstream>
#include <iomanip>

namespace teleport {

/**
 * @brief Generate a random UUID v4 string
 * @return UUID string in format "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
 */
inline std::string generate_uuid() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    static thread_local std::uniform_int_distribution<uint64_t> dist;
    
    uint64_t data1 = dist(gen);
    uint64_t data2 = dist(gen);
    
    // Set version to 4 (random)
    data1 = (data1 & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    // Set variant to RFC4122
    data2 = (data2 & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    // xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    oss << std::setw(8) << ((data1 >> 32) & 0xFFFFFFFF) << '-';
    oss << std::setw(4) << ((data1 >> 16) & 0xFFFF) << '-';
    oss << std::setw(4) << (data1 & 0xFFFF) << '-';
    oss << std::setw(4) << ((data2 >> 48) & 0xFFFF) << '-';
    oss << std::setw(12) << (data2 & 0xFFFFFFFFFFFFULL);
    
    return oss.str();
}

} // namespace teleport

#endif // TELEPORT_UUID_HPP
