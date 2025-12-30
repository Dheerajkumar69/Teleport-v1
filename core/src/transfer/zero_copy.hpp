/**
 * @file zero_copy.hpp
 * @brief Zero-copy file transfer using OS-native APIs
 * 
 * Uses TransmitFile (Windows) or sendfile (Linux) for efficient
 * kernel-to-network data transfer without user-space copying.
 */

#ifndef TELEPORT_ZERO_COPY_HPP
#define TELEPORT_ZERO_COPY_HPP

#include <cstdint>
#include <string>
#include "platform/pal.hpp"
#include "teleport/types.h"

namespace teleport {

/**
 * @brief Zero-copy file sending
 * 
 * Sends file data directly from file handle to socket without
 * copying through user-space buffers.
 * 
 * @param socket Target socket
 * @param file Source file handle
 * @param offset Starting offset in file
 * @param length Number of bytes to send (0 = rest of file)
 * @return Number of bytes sent, or error
 */
Result<uint64_t> send_file_zero_copy(
    pal::TcpSocket& socket,
    pal::File& file,
    uint64_t offset = 0,
    uint64_t length = 0
);

/**
 * @brief Zero-copy chunk sending with header
 * 
 * Sends chunk header + file data in one syscall where possible.
 */
Result<uint64_t> send_chunk_zero_copy(
    pal::TcpSocket& socket,
    pal::File& file,
    uint32_t file_id,
    uint32_t chunk_id,
    uint64_t offset,
    uint32_t size
);

/**
 * @brief Check if zero-copy is available on this platform
 */
bool is_zero_copy_available();

/**
 * @brief Get maximum transfer size for zero-copy
 */
uint64_t get_zero_copy_max_size();

} // namespace teleport

#endif // TELEPORT_ZERO_COPY_HPP
