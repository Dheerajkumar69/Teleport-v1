/**
 * @file chunk_writer.cpp
 * @brief Chunk writer implementation
 */

#include "chunk_writer.hpp"
#include "utils/logger.hpp"

namespace teleport {

ChunkWriter::ChunkWriter(const std::string& path, uint64_t expected_size, uint32_t chunk_size)
    : m_path(path)
    , m_expected_size(expected_size)
    , m_chunk_size(chunk_size)
    , m_bytes_written(0)
    , m_next_chunk(0) {
    
    m_total_chunks = static_cast<uint32_t>((expected_size + chunk_size - 1) / chunk_size);
    
    auto result = pal::open_file(path, pal::FileMode::Write);
    if (result) {
        m_file = std::move(*result);
    }
}

ChunkWriter::~ChunkWriter() {
    if (m_file) {
        m_file->flush();
        m_file->close();
    }
}

Result<void> ChunkWriter::write_chunk(uint32_t chunk_id, const uint8_t* data, size_t len) {
    if (!is_open()) {
        return make_error(TELEPORT_ERROR_FILE_OPEN, "File not open");
    }
    
    if (chunk_id >= m_total_chunks) {
        return make_error(TELEPORT_ERROR_INVALID_ARGUMENT, "Chunk ID out of range");
    }
    
    // Seek to chunk position
    uint64_t offset = static_cast<uint64_t>(chunk_id) * m_chunk_size;
    auto seek_result = m_file->seek(offset);
    if (!seek_result) {
        return seek_result.error();
    }
    
    // Write data
    auto write_result = m_file->write(data, len);
    if (!write_result) {
        return write_result.error();
    }
    
    // Track received chunk
    if (m_received_chunks.find(chunk_id) == m_received_chunks.end()) {
        m_received_chunks.insert(chunk_id);
        m_bytes_written += len;
    }
    
    m_next_chunk = chunk_id + 1;
    return ok();
}

Result<void> ChunkWriter::write_next(const uint8_t* data, size_t len) {
    return write_chunk(m_next_chunk, data, len);
}

std::vector<uint32_t> ChunkWriter::received_chunks() const {
    return std::vector<uint32_t>(m_received_chunks.begin(), m_received_chunks.end());
}

std::vector<uint32_t> ChunkWriter::missing_chunks() const {
    std::vector<uint32_t> missing;
    for (uint32_t i = 0; i < m_total_chunks; ++i) {
        if (m_received_chunks.find(i) == m_received_chunks.end()) {
            missing.push_back(i);
        }
    }
    return missing;
}

bool ChunkWriter::is_complete() const {
    return m_received_chunks.size() == m_total_chunks;
}

Result<void> ChunkWriter::finalize() {
    if (!m_file) {
        return make_error(TELEPORT_ERROR_FILE_OPEN, "File not open");
    }
    
    auto flush_result = m_file->flush();
    if (!flush_result) {
        return flush_result.error();
    }
    
    m_file->close();
    
    if (!is_complete()) {
        LOG_WARN("File finalized with missing chunks: ", missing_chunks().size());
    }
    
    return ok();
}

} // namespace teleport
