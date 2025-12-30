/**
 * @file transfer_manager.cpp
 * @brief Transfer manager implementation
 */

#include "transfer_manager.hpp"
#include "utils/logger.hpp"

namespace teleport {

TransferManager::TransferManager(const Config& config)
    : m_config(config)
    , m_paused(false)
    , m_cancelled(false) {
}

TransferManager::~TransferManager() {
    cancel();
}

Result<void> TransferManager::send_files(
    pal::TcpSocket& socket,
    const std::vector<FileInfo>& files,
    std::function<void(const TransferStats&)> on_progress
) {
    TransferStats stats;
    stats.files_total = static_cast<uint32_t>(files.size());
    stats.start_time = Clock::now();
    
    for (const auto& file : files) {
        stats.bytes_total += file.size;
    }
    
    std::vector<uint8_t> buffer(m_config.chunk_size);
    
    for (const auto& file : files) {
        if (m_cancelled.load()) {
            return make_error(TELEPORT_ERROR_CANCELLED, "Transfer cancelled");
        }
        
        LOG_INFO("Sending: ", file.name);
        ChunkReader reader(file.path, m_config.chunk_size);
        
        if (!reader.is_open()) {
            return make_error(TELEPORT_ERROR_FILE_OPEN, "Failed to open: " + file.path);
        }
        
        while (true) {
            // Handle pause
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                while (m_paused.load() && !m_cancelled.load()) {
                    m_cv.wait(lock);
                }
            }
            
            if (m_cancelled.load()) {
                return make_error(TELEPORT_ERROR_CANCELLED, "Transfer cancelled");
            }
            
            auto read_result = reader.read_next(buffer.data());
            if (!read_result) {
                return read_result.error();
            }
            
            size_t bytes_read = *read_result;
            if (bytes_read == 0) break;  // EOF
            
            // Build chunk header
            ChunkHeader header;
            header.file_id = file.id;
            header.chunk_id = reader.current_chunk() - 1;
            header.offset = static_cast<uint32_t>((header.chunk_id) * m_config.chunk_size);
            header.size = static_cast<uint32_t>(bytes_read);
            
            // Send header
            uint8_t header_buf[ChunkHeader::HEADER_SIZE];
            header.serialize(header_buf);
            
            auto send_result = socket.send_all(header_buf, ChunkHeader::HEADER_SIZE);
            if (!send_result) {
                return send_result.error();
            }
            
            // Send data
            send_result = socket.send_all(buffer.data(), bytes_read);
            if (!send_result) {
                return send_result.error();
            }
            
            stats.bytes_transferred += bytes_read;
            
            // Update progress
            auto now = Clock::now();
            auto elapsed_ms = std::chrono::duration_cast<Milliseconds>(
                now - stats.start_time
            ).count();
            
            if (elapsed_ms > 0) {
                stats.speed_bps = (stats.bytes_transferred * 1000.0) / elapsed_ms;
                stats.eta_seconds = static_cast<int32_t>(
                    (stats.bytes_total - stats.bytes_transferred) / stats.speed_bps
                );
            }
            
            stats.last_update = now;
            
            if (on_progress) {
                on_progress(stats);
            }
        }
        
        stats.files_completed++;
        LOG_INFO("Sent: ", file.name);
    }
    
    return ok();
}

Result<void> TransferManager::receive_files(
    pal::TcpSocket& socket,
    const std::vector<FileInfo>& files,
    const std::string& output_dir,
    std::function<void(const TransferStats&)> on_progress
) {
    TransferStats stats;
    stats.files_total = static_cast<uint32_t>(files.size());
    stats.start_time = Clock::now();
    
    for (const auto& file : files) {
        stats.bytes_total += file.size;
    }
    
    pal::create_directory(output_dir);
    
    for (const auto& file : files) {
        if (m_cancelled.load()) {
            return make_error(TELEPORT_ERROR_CANCELLED, "Transfer cancelled");
        }
        
        std::string output_path = output_dir + "/" + file.name;
        LOG_INFO("Receiving: ", file.name, " -> ", output_path);
        
        ChunkWriter writer(output_path, file.size, m_config.chunk_size);
        if (!writer.is_open()) {
            return make_error(TELEPORT_ERROR_FILE_OPEN, "Failed to create: " + output_path);
        }
        
        uint64_t bytes_remaining = file.size;
        std::vector<uint8_t> buffer(m_config.chunk_size);
        
        while (bytes_remaining > 0) {
            // Handle pause
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                while (m_paused.load() && !m_cancelled.load()) {
                    m_cv.wait(lock);
                }
            }
            
            if (m_cancelled.load()) {
                return make_error(TELEPORT_ERROR_CANCELLED, "Transfer cancelled");
            }
            
            // Receive chunk header
            uint8_t header_buf[ChunkHeader::HEADER_SIZE];
            auto recv_result = socket.recv_all(header_buf, ChunkHeader::HEADER_SIZE);
            if (!recv_result) {
                return recv_result.error();
            }
            
            auto header = ChunkHeader::deserialize(header_buf);
            
            if (header.file_id != file.id) {
                return make_error(TELEPORT_ERROR_PROTOCOL, "Unexpected file ID in chunk");
            }
            
            // Receive chunk data
            size_t to_read = std::min(static_cast<size_t>(header.size), buffer.size());
            recv_result = socket.recv_all(buffer.data(), to_read);
            if (!recv_result) {
                return recv_result.error();
            }
            
            // Write to file
            auto write_result = writer.write_chunk(header.chunk_id, buffer.data(), to_read);
            if (!write_result) {
                return write_result.error();
            }
            
            bytes_remaining -= to_read;
            stats.bytes_transferred += to_read;
            
            // Update progress
            auto now = Clock::now();
            auto elapsed_ms = std::chrono::duration_cast<Milliseconds>(
                now - stats.start_time
            ).count();
            
            if (elapsed_ms > 0) {
                stats.speed_bps = (stats.bytes_transferred * 1000.0) / elapsed_ms;
                stats.eta_seconds = static_cast<int32_t>(
                    (stats.bytes_total - stats.bytes_transferred) / stats.speed_bps
                );
            }
            
            stats.last_update = now;
            
            if (on_progress) {
                on_progress(stats);
            }
        }
        
        writer.finalize();
        stats.files_completed++;
        LOG_INFO("Received: ", file.name);
    }
    
    return ok();
}

void TransferManager::pause() {
    m_paused.store(true);
    LOG_INFO("Transfer paused");
}

void TransferManager::resume() {
    m_paused.store(false);
    m_cv.notify_all();
    LOG_INFO("Transfer resumed");
}

void TransferManager::cancel() {
    m_cancelled.store(true);
    m_paused.store(false);
    m_cv.notify_all();
    LOG_INFO("Transfer cancelled");
}

} // namespace teleport
