/**
 * @file chunk.hpp
 * @brief Chunk header for parallel file transfer protocol
 */

#ifndef TELEPORT_CHUNK_HPP
#define TELEPORT_CHUNK_HPP

#include <cstdint>
#include <cstring>

namespace teleport {

/**
 * @brief Header for each chunk in parallel transfer
 * 
 * Each chunk sent over the wire has this 16-byte header:
 * - file_id:   4 bytes - Identifies which file this chunk belongs to
 * - chunk_id:  4 bytes - Sequential chunk number
 * - offset:    4 bytes - Offset within file (lower 32 bits)
 * - size:      4 bytes - Size of chunk data following this header
 */
struct ChunkHeader {
    static constexpr size_t HEADER_SIZE = 16;
    
    uint32_t file_id;
    uint32_t chunk_id;
    uint32_t offset;
    uint32_t size;
    
    /**
     * @brief Serialize header to bytes (network byte order)
     */
    void serialize(uint8_t* buf) const {
        // Use big-endian for network
        buf[0] = (file_id >> 24) & 0xFF;
        buf[1] = (file_id >> 16) & 0xFF;
        buf[2] = (file_id >> 8) & 0xFF;
        buf[3] = file_id & 0xFF;
        
        buf[4] = (chunk_id >> 24) & 0xFF;
        buf[5] = (chunk_id >> 16) & 0xFF;
        buf[6] = (chunk_id >> 8) & 0xFF;
        buf[7] = chunk_id & 0xFF;
        
        buf[8] = (offset >> 24) & 0xFF;
        buf[9] = (offset >> 16) & 0xFF;
        buf[10] = (offset >> 8) & 0xFF;
        buf[11] = offset & 0xFF;
        
        buf[12] = (size >> 24) & 0xFF;
        buf[13] = (size >> 16) & 0xFF;
        buf[14] = (size >> 8) & 0xFF;
        buf[15] = size & 0xFF;
    }
    
    /**
     * @brief Deserialize header from bytes
     */
    static ChunkHeader deserialize(const uint8_t* buf) {
        ChunkHeader h;
        h.file_id = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
        h.chunk_id = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
        h.offset = (buf[8] << 24) | (buf[9] << 16) | (buf[10] << 8) | buf[11];
        h.size = (buf[12] << 24) | (buf[13] << 16) | (buf[14] << 8) | buf[15];
        return h;
    }
};

} // namespace teleport

#endif // TELEPORT_CHUNK_HPP
