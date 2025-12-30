/**
 * @file chunk_writer.hpp
 * @brief Efficient file chunk writer with resume support
 */

#ifndef TELEPORT_CHUNK_WRITER_HPP
#define TELEPORT_CHUNK_WRITER_HPP

#include "teleport/types.h"
#include "teleport/errors.h"
#include "platform/pal.hpp"
#include <memory>
#include <vector>
#include <set>

namespace teleport {

/**
 * @brief Writes file data in chunks with resume tracking
 */
class ChunkWriter {
public:
    ChunkWriter(const std::string& path, uint64_t expected_size, uint32_t chunk_size);
    ~ChunkWriter();
    
    /**
     * @brief Check if file is open
     */
    bool is_open() const { return m_file && m_file->is_open(); }
    
    /**
     * @brief Get expected file size
     */
    uint64_t expected_size() const { return m_expected_size; }
    
    /**
     * @brief Get bytes written so far
     */
    uint64_t bytes_written() const { return m_bytes_written; }
    
    /**
     * @brief Write a chunk at specified position
     * @param chunk_id Chunk index
     * @param data Chunk data
     * @param len Data length
     * @return Success result
     */
    Result<void> write_chunk(uint32_t chunk_id, const uint8_t* data, size_t len);
    
    /**
     * @brief Write next sequential chunk
     * @param data Chunk data
     * @param len Data length
     * @return Success result
     */
    Result<void> write_next(const uint8_t* data, size_t len);
    
    /**
     * @brief Get list of received chunk IDs (for resume)
     */
    std::vector<uint32_t> received_chunks() const;
    
    /**
     * @brief Get list of missing chunk IDs (for resume)
     */
    std::vector<uint32_t> missing_chunks() const;
    
    /**
     * @brief Check if all chunks are received
     */
    bool is_complete() const;
    
    /**
     * @brief Finalize the file (flush and close)
     */
    Result<void> finalize();
    
private:
    std::unique_ptr<pal::File> m_file;
    std::string m_path;
    uint64_t m_expected_size;
    uint32_t m_chunk_size;
    uint32_t m_total_chunks;
    uint64_t m_bytes_written;
    uint32_t m_next_chunk;
    std::set<uint32_t> m_received_chunks;
};

} // namespace teleport

#endif // TELEPORT_CHUNK_WRITER_HPP
