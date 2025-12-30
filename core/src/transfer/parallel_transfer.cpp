/**
 * @file parallel_transfer.cpp
 * @brief Production parallel transfer implementation
 * 
 * Uses multiple TCP streams for maximum throughput.
 * Chunks are distributed round-robin across streams.
 */

#include "parallel_transfer.hpp"
#include "utils/logger.hpp"

namespace teleport {

ParallelTransfer::ParallelTransfer(const Config& config)
    : m_config(config) {
    m_streams.resize(config.num_streams);
}

ParallelTransfer::~ParallelTransfer() {
    close();
}

Result<void> ParallelTransfer::connect(const std::string& ip, uint16_t port) {
    LOG_INFO("Connecting ", m_config.num_streams, " parallel streams to ", ip, ":", port);
    
    for (size_t i = 0; i < m_config.num_streams; ++i) {
        pal::SocketOptions opts;
        opts.recv_timeout_ms = m_config.transfer_timeout_ms;
        opts.send_timeout_ms = m_config.transfer_timeout_ms;
        opts.nodelay = true;  // Disable Nagle for latency
        
        m_streams[i] = pal::create_tcp_socket(opts);
        if (!m_streams[i] || !m_streams[i]->is_valid()) {
            return make_error(TELEPORT_ERROR_SOCKET_CREATE, 
                "Failed to create stream " + std::to_string(i));
        }
        
        // Set larger buffer for throughput
        m_streams[i]->set_send_buffer(4 * 1024 * 1024);  // 4MB
        m_streams[i]->set_recv_buffer(4 * 1024 * 1024);
        
        auto result = m_streams[i]->connect(ip, port, m_config.connect_timeout_ms);
        if (!result) {
            LOG_ERROR("Stream ", i, " failed to connect: ", result.error().message);
            return result.error();
        }
        
        LOG_DEBUG("Stream ", i, " connected");
    }
    
    m_running.store(true);
    LOG_INFO("All ", m_config.num_streams, " streams connected");
    return ok();
}

Result<void> ParallelTransfer::accept(pal::TcpSocket& listen_socket) {
    LOG_INFO("Accepting ", m_config.num_streams, " parallel streams");
    
    for (size_t i = 0; i < m_config.num_streams; ++i) {
        auto result = listen_socket.accept();
        if (!result) {
            return result.error();
        }
        
        m_streams[i] = std::move(*result);
        m_streams[i]->set_send_buffer(4 * 1024 * 1024);
        m_streams[i]->set_recv_buffer(4 * 1024 * 1024);
        
        LOG_DEBUG("Stream ", i, " accepted");
    }
    
    m_running.store(true);
    LOG_INFO("All streams accepted");
    return ok();
}

Result<void> ParallelTransfer::send_file(
    const std::string& file_path,
    uint32_t file_id,
    const std::vector<uint32_t>& skip_chunks
) {
    // Open file
    auto file_result = pal::open_file(file_path, pal::FileMode::Read);
    if (!file_result) {
        return file_result.error();
    }
    
    auto& file = *file_result;
    uint64_t file_size = file->size();
    uint32_t total_chunks = static_cast<uint32_t>(
        (file_size + m_config.chunk_size - 1) / m_config.chunk_size
    );
    
    // Skip already received chunks (for resume)
    m_skip_chunks.clear();
    for (uint32_t c : skip_chunks) {
        m_skip_chunks.insert(c);
    }
    
    // Update stats
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        m_stats.bytes_total = file_size;
        m_stats.chunks_total = total_chunks;
        m_stats.start_time = Clock::now();
        
        // Account for skipped chunks
        for (uint32_t c : skip_chunks) {
            uint64_t chunk_bytes = (c == total_chunks - 1) 
                ? (file_size - c * m_config.chunk_size)
                : m_config.chunk_size;
            m_stats.bytes_sent += chunk_bytes;
            m_stats.chunks_completed++;
        }
    }
    
    LOG_INFO("Sending ", file_path, " (", file_size, " bytes, ", total_chunks, " chunks)");
    LOG_INFO("Skipping ", skip_chunks.size(), " already-received chunks");
    
    // Queue chunks for workers
    for (uint32_t chunk_id = 0; chunk_id < total_chunks; ++chunk_id) {
        if (m_skip_chunks.count(chunk_id)) {
            continue;  // Skip already received
        }
        
        ParallelWork work;
        work.file_id = file_id;
        work.chunk_id = chunk_id;
        work.offset = static_cast<uint64_t>(chunk_id) * m_config.chunk_size;
        work.size = static_cast<uint32_t>(std::min(
            m_config.chunk_size,
            file_size - work.offset
        ));
        work.file_path = file_path;
        
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_work_queue.push(std::move(work));
    }
    
    // Start worker threads
    m_workers.clear();
    for (size_t i = 0; i < m_config.num_streams; ++i) {
        m_workers.emplace_back(&ParallelTransfer::sender_worker, this, i);
    }
    
    // Wait for completion
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
    
    if (m_cancelled.load()) {
        return make_error(TELEPORT_ERROR_CANCELLED, "Transfer cancelled");
    }
    
    LOG_INFO("File sent successfully");
    return ok();
}

Result<void> ParallelTransfer::receive_file(
    const std::string& output_path,
    uint32_t file_id,
    uint64_t file_size,
    const std::vector<uint32_t>& existing_chunks
) {
    uint32_t total_chunks = static_cast<uint32_t>(
        (file_size + m_config.chunk_size - 1) / m_config.chunk_size
    );
    
    // Create tracker
    auto tracker = std::make_unique<ChunkTracker>(total_chunks);
    for (uint32_t c : existing_chunks) {
        tracker->mark_received(c);
    }
    m_trackers[file_id] = std::move(tracker);
    
    // Open/create output file
    auto file_result = pal::open_file(output_path, pal::FileMode::Write);
    if (!file_result) {
        return file_result.error();
    }
    m_output_file = std::move(*file_result);
    
    // Pre-allocate file for random writes
    m_output_file->truncate(file_size);
    
    // Update stats
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        m_stats.bytes_total = file_size;
        m_stats.chunks_total = total_chunks;
        m_stats.start_time = Clock::now();
        
        // Account for existing chunks
        for (uint32_t c : existing_chunks) {
            uint64_t chunk_bytes = (c == total_chunks - 1)
                ? (file_size - c * m_config.chunk_size)
                : m_config.chunk_size;
            m_stats.bytes_received += chunk_bytes;
            m_stats.chunks_completed++;
        }
    }
    
    LOG_INFO("Receiving to ", output_path, " (", file_size, " bytes, ", total_chunks, " chunks)");
    LOG_INFO("Already have ", existing_chunks.size(), " chunks");
    
    // Start receiver workers
    m_workers.clear();
    for (size_t i = 0; i < m_config.num_streams; ++i) {
        m_workers.emplace_back(&ParallelTransfer::receiver_worker, this, i);
    }
    
    // Wait for completion
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
    
    if (m_cancelled.load()) {
        return make_error(TELEPORT_ERROR_CANCELLED, "Transfer cancelled");
    }
    
    m_output_file->flush();
    m_output_file.reset();
    
    LOG_INFO("File received successfully");
    return ok();
}

void ParallelTransfer::sender_worker(size_t stream_id) {
    auto& socket = m_streams[stream_id];
    std::vector<uint8_t> buffer(m_config.chunk_size);
    
    LOG_DEBUG("Sender worker ", stream_id, " started");
    
    while (m_running.load() && !m_cancelled.load()) {
        // Handle pause
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            while (m_paused.load() && !m_cancelled.load()) {
                m_pause_cv.wait(lock);
            }
        }
        
        if (m_cancelled.load()) break;
        
        // Get work
        ParallelWork work;
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            if (m_work_queue.empty()) {
                break;  // No more work
            }
            work = std::move(m_work_queue.front());
            m_work_queue.pop();
        }
        
        // Read chunk from file
        auto file_result = pal::open_file(work.file_path, pal::FileMode::Read);
        if (!file_result) {
            if (m_error_cb) {
                m_error_cb(TELEPORT_ERROR_FILE_READ, "Failed to open file");
            }
            continue;
        }
        
        auto& file = *file_result;
        file->seek(work.offset);
        
        auto read_result = file->read(buffer.data(), work.size);
        if (!read_result) {
            if (m_error_cb) {
                m_error_cb(TELEPORT_ERROR_FILE_READ, read_result.error().message);
            }
            continue;
        }
        
        // Build header
        ChunkHeader header;
        header.file_id = work.file_id;
        header.chunk_id = work.chunk_id;
        header.offset = static_cast<uint32_t>(work.offset % UINT32_MAX);
        header.size = work.size;
        
        uint8_t header_buf[ChunkHeader::HEADER_SIZE];
        header.serialize(header_buf);
        
        // Send header + data
        auto send_result = socket->send_all(header_buf, ChunkHeader::HEADER_SIZE);
        if (!send_result) {
            if (m_error_cb) {
                m_error_cb(TELEPORT_ERROR_SOCKET_SEND, send_result.error().message);
            }
            m_cancelled.store(true);
            break;
        }
        
        send_result = socket->send_all(buffer.data(), work.size);
        if (!send_result) {
            if (m_error_cb) {
                m_error_cb(TELEPORT_ERROR_SOCKET_SEND, send_result.error().message);
            }
            m_cancelled.store(true);
            break;
        }
        
        update_stats(work.size);
    }
    
    LOG_DEBUG("Sender worker ", stream_id, " finished");
}

void ParallelTransfer::receiver_worker(size_t stream_id) {
    auto& socket = m_streams[stream_id];
    std::vector<uint8_t> buffer(m_config.chunk_size);
    
    LOG_DEBUG("Receiver worker ", stream_id, " started");
    
    while (m_running.load() && !m_cancelled.load()) {
        // Handle pause
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            while (m_paused.load() && !m_cancelled.load()) {
                m_pause_cv.wait(lock);
            }
        }
        
        if (m_cancelled.load()) break;
        
        // Receive header
        uint8_t header_buf[ChunkHeader::HEADER_SIZE];
        auto recv_result = socket->recv_all(header_buf, ChunkHeader::HEADER_SIZE);
        if (!recv_result) {
            // Connection closed or timeout - check if we're done
            break;
        }
        
        auto header = ChunkHeader::deserialize(header_buf);
        
        // Receive data
        recv_result = socket->recv_all(buffer.data(), header.size);
        if (!recv_result) {
            if (m_error_cb) {
                m_error_cb(TELEPORT_ERROR_SOCKET_RECV, recv_result.error().message);
            }
            m_cancelled.store(true);
            break;
        }
        
        // Write to file at correct offset
        {
            std::lock_guard<std::mutex> lock(m_file_mutex);
            if (m_output_file) {
                uint64_t offset = static_cast<uint64_t>(header.chunk_id) * m_config.chunk_size;
                m_output_file->seek(offset);
                
                auto write_result = m_output_file->write(buffer.data(), header.size);
                if (!write_result) {
                    if (m_error_cb) {
                        m_error_cb(TELEPORT_ERROR_FILE_WRITE, write_result.error().message);
                    }
                    m_cancelled.store(true);
                    break;
                }
            }
        }
        
        // Mark chunk as received
        if (auto tracker = get_tracker(header.file_id)) {
            tracker->mark_received(header.chunk_id);
            
            // Check if complete
            if (tracker->is_complete()) {
                m_running.store(false);
            }
        }
        
        update_stats(header.size);
    }
    
    LOG_DEBUG("Receiver worker ", stream_id, " finished");
}

void ParallelTransfer::update_stats(uint64_t bytes) {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    
    m_stats.bytes_sent += bytes;
    m_stats.bytes_received += bytes;
    m_stats.chunks_completed++;
    
    auto now = Clock::now();
    auto elapsed_ms = std::chrono::duration_cast<Milliseconds>(
        now - m_stats.start_time
    ).count();
    
    if (elapsed_ms > 0) {
        m_stats.speed_bps = (m_stats.bytes_sent * 1000.0) / elapsed_ms;
        uint64_t remaining = m_stats.bytes_total - m_stats.bytes_sent;
        if (m_stats.speed_bps > 0) {
            m_stats.eta_seconds = static_cast<int>(remaining / m_stats.speed_bps);
        }
    }
    
    if (m_progress_cb) {
        m_progress_cb(m_stats);
    }
}

ChunkTracker* ParallelTransfer::get_tracker(uint32_t file_id) {
    auto it = m_trackers.find(file_id);
    return it != m_trackers.end() ? it->second.get() : nullptr;
}

void ParallelTransfer::pause() {
    m_paused.store(true);
    LOG_INFO("Parallel transfer paused");
}

void ParallelTransfer::resume() {
    m_paused.store(false);
    m_pause_cv.notify_all();
    LOG_INFO("Parallel transfer resumed");
}

void ParallelTransfer::cancel() {
    m_cancelled.store(true);
    m_paused.store(false);
    m_running.store(false);
    m_pause_cv.notify_all();
    m_queue_cv.notify_all();
    LOG_INFO("Parallel transfer cancelled");
}

void ParallelTransfer::close() {
    cancel();
    
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
    
    for (auto& stream : m_streams) {
        if (stream) {
            stream->close();
        }
    }
    m_streams.clear();
    
    m_output_file.reset();
}

} // namespace teleport
