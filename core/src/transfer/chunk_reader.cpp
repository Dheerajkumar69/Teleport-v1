/**
 * @file chunk_reader.cpp
 * @brief Chunk reader implementation
 */

#include "chunk_reader.hpp"

namespace teleport {

ChunkReader::ChunkReader(const std::string& path, uint32_t chunk_size)
    : m_size(0)
    , m_chunk_size(chunk_size)
    , m_chunk_count(0)
    , m_current_chunk(0) {
    
    auto result = pal::open_file(path, pal::FileMode::Read);
    if (result) {
        m_file = std::move(*result);
        m_size = m_file->size();
        m_chunk_count = static_cast<uint32_t>((m_size + chunk_size - 1) / chunk_size);
    }
}

Result<size_t> ChunkReader::read_chunk(uint32_t chunk_id, uint8_t* buffer) {
    if (!is_open()) {
        return make_error(TELEPORT_ERROR_FILE_OPEN, "File not open");
    }
    
    if (chunk_id >= m_chunk_count) {
        return make_error(TELEPORT_ERROR_INVALID_ARGUMENT, "Chunk ID out of range");
    }
    
    // Seek to chunk position
    uint64_t offset = static_cast<uint64_t>(chunk_id) * m_chunk_size;
    auto seek_result = m_file->seek(offset);
    if (!seek_result) {
        return seek_result.error();
    }
    
    // Calculate bytes to read
    uint64_t remaining = m_size - offset;
    size_t to_read = static_cast<size_t>(std::min(
        static_cast<uint64_t>(m_chunk_size), 
        remaining
    ));
    
    // Read data
    auto read_result = m_file->read(buffer, to_read);
    if (!read_result) {
        return read_result.error();
    }
    
    m_current_chunk = chunk_id + 1;
    return *read_result;
}

Result<size_t> ChunkReader::read_next(uint8_t* buffer) {
    if (m_current_chunk >= m_chunk_count) {
        return static_cast<size_t>(0);  // EOF
    }
    return read_chunk(m_current_chunk, buffer);
}

Result<void> ChunkReader::reset() {
    if (!is_open()) {
        return make_error(TELEPORT_ERROR_FILE_OPEN, "File not open");
    }
    
    auto result = m_file->seek(0);
    if (!result) {
        return result.error();
    }
    
    m_current_chunk = 0;
    return ok();
}

} // namespace teleport
