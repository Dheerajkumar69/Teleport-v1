/**
 * @file stream_pool.hpp
 * @brief Parallel TCP stream pool for high-throughput transfer
 */

#ifndef TELEPORT_STREAM_POOL_HPP
#define TELEPORT_STREAM_POOL_HPP

#include "teleport/types.h"
#include "teleport/errors.h"
#include "platform/pal.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <functional>

namespace teleport {

/**
 * @brief Work item for stream pool
 */
struct StreamWork {
    uint32_t file_id;
    uint32_t chunk_id;
    std::vector<uint8_t> data;  // For sending
    size_t expected_size;       // For receiving
};

/**
 * @brief Result of stream work
 */
struct StreamResult {
    uint32_t file_id;
    uint32_t chunk_id;
    std::vector<uint8_t> data;  // For receiving
    bool success;
    std::string error;
};

using StreamResultCallback = std::function<void(const StreamResult&)>;

/**
 * @brief Pool of parallel TCP streams for high-throughput transfer
 * 
 * This class manages multiple TCP connections to maximize throughput
 * by utilizing the full network bandwidth.
 */
class StreamPool {
public:
    StreamPool(size_t num_streams = 4);
    ~StreamPool();
    
    /**
     * @brief Connect all streams to target
     */
    Result<void> connect(const std::string& ip, uint16_t port);
    
    /**
     * @brief Start listening for incoming streams
     */
    Result<void> listen(uint16_t port);
    
    /**
     * @brief Submit work item for sending
     */
    void submit_send(StreamWork work);
    
    /**
     * @brief Submit work item for receiving
     */
    void submit_recv(StreamWork work);
    
    /**
     * @brief Set callback for completed work
     */
    void set_callback(StreamResultCallback callback) { m_callback = std::move(callback); }
    
    /**
     * @brief Wait for all pending work to complete
     */
    void wait_all();
    
    /**
     * @brief Get number of pending items
     */
    size_t pending_count() const;
    
    /**
     * @brief Stop all streams
     */
    void stop();
    
private:
    void worker_loop(size_t stream_id);
    
    size_t m_num_streams;
    std::vector<std::unique_ptr<pal::TcpSocket>> m_sockets;
    std::vector<std::thread> m_workers;
    
    std::queue<StreamWork> m_send_queue;
    std::queue<StreamWork> m_recv_queue;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    
    std::atomic<bool> m_running;
    std::atomic<size_t> m_pending;
    
    StreamResultCallback m_callback;
};

} // namespace teleport

#endif // TELEPORT_STREAM_POOL_HPP
