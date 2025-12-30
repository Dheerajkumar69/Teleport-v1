/**
 * @file chunk_reader.hpp
 * @brief Efficient file chunk reader
 */

#ifndef TELEPORT_CHUNK_READER_HPP
#define TELEPORT_CHUNK_READER_HPP

#include "teleport/types.h"
#include "teleport/errors.h"
#include "platform/pal.hpp"
#include <memory>
#include <vector>

namespace teleport {

/**
 * @brief Reads file data in chunks for efficient transfer
 */
class ChunkReader {
public:
    ChunkReader(const std::string& path, uint32_t chunk_size);
    ~ChunkReader() = default;
    
    /**
     * @brief Check if file is open
     */
    bool is_open() const { return m_file && m_file->is_open(); }
    
    /**
     * @brief Get file size
     */
    uint64_t size() const { return m_size; }
    
    /**
     * @brief Get total number of chunks
     */
    uint32_t chunk_count() const { return m_chunk_count; }
    
    /**
     * @brief Read a specific chunk
     * @param chunk_id Chunk index (0-based)
     * @param buffer Output buffer (must be at least chunk_size bytes)
     * @return Number of bytes read, or error
     */
    Result<size_t> read_chunk(uint32_t chunk_id, uint8_t* buffer);
    
    /**
     * @brief Read the next sequential chunk
     * @param buffer Output buffer
     * @return Number of bytes read (0 at EOF), or error
     */
    Result<size_t> read_next(uint8_t* buffer);
    
    /**
     * @brief Reset to beginning of file
     */
    Result<void> reset();
    
    /**
     * @brief Get current chunk ID
     */
    uint32_t current_chunk() const { return m_current_chunk; }
    
private:
    std::unique_ptr<pal::File> m_file;
    uint64_t m_size;
    uint32_t m_chunk_size;
    uint32_t m_chunk_count;
    uint32_t m_current_chunk;
};

} // namespace teleport

#endif // TELEPORT_CHUNK_READER_HPP
