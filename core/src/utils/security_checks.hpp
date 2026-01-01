/**
 * @file security_checks.hpp
 * @brief Production runtime security checks
 * 
 * Provides safe arithmetic, bounds checking, and allocation guards
 * to prevent integer overflow, buffer overrun, and DoS attacks.
 */

#ifndef TELEPORT_SECURITY_CHECKS_HPP
#define TELEPORT_SECURITY_CHECKS_HPP

#include <cstdint>
#include <cstddef>
#include <limits>

namespace teleport {
namespace security {

/* ============================================================================
 * Safe Arithmetic Operations
 * ============================================================================ */

/**
 * @brief Safe addition with overflow detection
 * @return true if addition succeeded without overflow
 */
constexpr bool safe_add_u64(uint64_t a, uint64_t b, uint64_t& result) noexcept {
    if (a > std::numeric_limits<uint64_t>::max() - b) {
        result = 0;
        return false;
    }
    result = a + b;
    return true;
}

/**
 * @brief Safe multiplication with overflow detection
 * @return true if multiplication succeeded without overflow
 */
constexpr bool safe_multiply_u64(uint64_t a, uint64_t b, uint64_t& result) noexcept {
    if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a) {
        result = 0;
        return false;
    }
    result = a * b;
    return true;
}

/**
 * @brief Safe cast from uint64_t to uint32_t with truncation check
 * @return true if value fits in uint32_t
 */
constexpr bool safe_cast_to_u32(uint64_t value, uint32_t& result) noexcept {
    if (value > std::numeric_limits<uint32_t>::max()) {
        result = static_cast<uint32_t>(value);  // Truncate but signal overflow
        return false;
    }
    result = static_cast<uint32_t>(value);
    return true;
}

/* ============================================================================
 * Buffer Bounds Checking
 * ============================================================================ */

/**
 * @brief Validate that [offset, offset + length) is within buffer bounds
 * @return true if access is within bounds
 */
inline bool check_bounds(size_t offset, size_t length, size_t buffer_size) noexcept {
    // Check for overflow in offset + length
    if (offset > buffer_size) return false;
    if (length > buffer_size - offset) return false;
    return true;
}

/**
 * @brief Validate array index is within bounds
 */
inline bool check_index(size_t index, size_t array_size) noexcept {
    return index < array_size;
}

/* ============================================================================
 * Memory Allocation Guards
 * ============================================================================ */

/// Maximum allowed single allocation (100 MB) to prevent DoS
constexpr size_t MAX_SINGLE_ALLOCATION = 100ULL * 1024 * 1024;

/// Maximum number of files in a single transfer (prevent DoS)
constexpr size_t MAX_FILES_PER_TRANSFER = 10000;

/// Maximum total transfer size (100 GB)
constexpr uint64_t MAX_TRANSFER_SIZE = 100ULL * 1024 * 1024 * 1024;

/**
 * @brief Validate allocation request size
 * @return true if allocation is within safe limits
 */
inline bool validate_allocation_size(size_t requested) noexcept {
    return requested <= MAX_SINGLE_ALLOCATION;
}

/**
 * @brief Validate chunk size is reasonable
 */
inline bool validate_chunk_size(uint32_t chunk_size) noexcept {
    // Minimum 1KB, maximum 16MB
    return chunk_size >= 1024 && chunk_size <= 16 * 1024 * 1024;
}

/* ============================================================================
 * Division Safety
 * ============================================================================ */

/**
 * @brief Safe division with zero check
 * @return quotient or 0 if divisor is 0
 */
template<typename T>
constexpr T safe_divide(T dividend, T divisor, T default_value = 0) noexcept {
    return (divisor != 0) ? (dividend / divisor) : default_value;
}

/**
 * @brief Safe floating point division
 */
inline double safe_divide_d(double dividend, double divisor, double default_value = 0.0) noexcept {
    // Check for zero and very small divisors
    if (divisor == 0.0 || (divisor > -1e-10 && divisor < 1e-10)) {
        return default_value;
    }
    return dividend / divisor;
}

} // namespace security
} // namespace teleport

#endif // TELEPORT_SECURITY_CHECKS_HPP
