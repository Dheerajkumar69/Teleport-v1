/**
 * @file stream_pool.cpp
 * @brief Stream pool implementation
 */

#include "stream_pool.hpp"
#include "utils/logger.hpp"

namespace teleport {

StreamPool::StreamPool(size_t num_streams)
    : m_num_streams(num_streams)
    , m_running(false)
    , m_pending(0) {
    m_sockets.resize(num_streams);
}

StreamPool::~StreamPool() {
    stop();
}

Result<void> StreamPool::connect(const std::string& ip, uint16_t port) {
    for (size_t i = 0; i < m_num_streams; ++i) {
        pal::SocketOptions opts;
        opts.recv_timeout_ms = 30000;
        opts.send_timeout_ms = 30000;
        
        m_sockets[i] = pal::create_tcp_socket(opts);
        if (!m_sockets[i] || !m_sockets[i]->is_valid()) {
            return make_error(TELEPORT_ERROR_SOCKET_CREATE, 
                "Failed to create stream socket " + std::to_string(i));
        }
        
        auto result = m_sockets[i]->connect(ip, port, 10000);
        if (!result) {
            return result.error();
        }
        
        LOG_DEBUG("Stream ", i, " connected to ", ip, ":", port);
    }
    
    // Start worker threads
    m_running.store(true);
    for (size_t i = 0; i < m_num_streams; ++i) {
        m_workers.emplace_back(&StreamPool::worker_loop, this, i);
    }
    
    LOG_INFO("Stream pool started with ", m_num_streams, " connections");
    return ok();
}

Result<void> StreamPool::listen(uint16_t port) {
    // For receiver side, we accept connections instead (handled elsewhere)
    // This is a placeholder for future parallel receive support
    return ok();
}

void StreamPool::submit_send(StreamWork work) {
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_send_queue.push(std::move(work));
        m_pending++;
    }
    m_queue_cv.notify_one();
}

void StreamPool::submit_recv(StreamWork work) {
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_recv_queue.push(std::move(work));
        m_pending++;
    }
    m_queue_cv.notify_one();
}

void StreamPool::wait_all() {
    while (m_pending.load() > 0) {
        pal::sleep_ms(10);
    }
}

size_t StreamPool::pending_count() const {
    return m_pending.load();
}

void StreamPool::stop() {
    if (m_running.load()) {
        m_running.store(false);
        m_queue_cv.notify_all();
        
        for (auto& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
        
        for (auto& socket : m_sockets) {
            if (socket) {
                socket->close();
            }
        }
        m_sockets.clear();
        
        LOG_INFO("Stream pool stopped");
    }
}

void StreamPool::worker_loop(size_t stream_id) {
    auto& socket = m_sockets[stream_id];
    
    while (m_running.load()) {
        StreamWork work;
        bool has_send = false;
        bool has_recv = false;
        
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_queue_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !m_send_queue.empty() || !m_recv_queue.empty() || !m_running.load();
            });
            
            if (!m_running.load()) break;
            
            if (!m_send_queue.empty()) {
                work = std::move(m_send_queue.front());
                m_send_queue.pop();
                has_send = true;
            } else if (!m_recv_queue.empty()) {
                work = std::move(m_recv_queue.front());
                m_recv_queue.pop();
                has_recv = true;
            }
        }
        
        if (has_send) {
            StreamResult result;
            result.file_id = work.file_id;
            result.chunk_id = work.chunk_id;
            result.success = true;
            
            // Build and send chunk with header
            ChunkHeader header;
            header.file_id = work.file_id;
            header.chunk_id = work.chunk_id;
            header.offset = work.chunk_id * static_cast<uint32_t>(work.data.size());
            header.size = static_cast<uint32_t>(work.data.size());
            
            uint8_t header_buf[ChunkHeader::HEADER_SIZE];
            header.serialize(header_buf);
            
            auto send_result = socket->send_all(header_buf, ChunkHeader::HEADER_SIZE);
            if (!send_result) {
                result.success = false;
                result.error = send_result.error().message;
            } else {
                send_result = socket->send_all(work.data.data(), work.data.size());
                if (!send_result) {
                    result.success = false;
                    result.error = send_result.error().message;
                }
            }
            
            if (m_callback) {
                m_callback(result);
            }
            
            m_pending--;
        }
        
        if (has_recv) {
            StreamResult result;
            result.file_id = work.file_id;
            result.chunk_id = work.chunk_id;
            result.success = true;
            result.data.resize(work.expected_size);
            
            // Receive chunk with header
            uint8_t header_buf[ChunkHeader::HEADER_SIZE];
            auto recv_result = socket->recv_all(header_buf, ChunkHeader::HEADER_SIZE);
            
            if (!recv_result) {
                result.success = false;
                result.error = recv_result.error().message;
            } else {
                auto header = ChunkHeader::deserialize(header_buf);
                
                recv_result = socket->recv_all(result.data.data(), header.size);
                if (!recv_result) {
                    result.success = false;
                    result.error = recv_result.error().message;
                }
                result.data.resize(header.size);
            }
            
            if (m_callback) {
                m_callback(result);
            }
            
            m_pending--;
        }
    }
}

} // namespace teleport
