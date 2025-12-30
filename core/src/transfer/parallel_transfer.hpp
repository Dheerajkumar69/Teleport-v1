/**
 * @file parallel_transfer.hpp
 * @brief Real parallel multi-stream file transfer with resume support
 * 
 * This is the PRODUCTION implementation that uses multiple TCP connections
 * to maximize throughput on high-bandwidth networks.
 */

#ifndef TELEPORT_PARALLEL_TRANSFER_HPP
#define TELEPORT_PARALLEL_TRANSFER_HPP

#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <bitset>
#include <unordered_set>
#include <unordered_map>
#include "platform/pal.hpp"
#include "transfer/chunk.hpp"
#include "teleport/types.h"

namespace teleport {

// Maximum chunks to track (allows ~8TB files with 2MB chunks)
static constexpr size_t MAX_CHUNKS = 4 * 1024 * 1024;

/**
 * @brief Tracks which chunks have been received for resume support
 */
class ChunkTracker {
public:
    explicit ChunkTracker(uint32_t total_chunks)
        : m_total(total_chunks)
        , m_received(0) {
        m_chunks.resize((total_chunks + 7) / 8, 0);
    }
    
    void mark_received(uint32_t chunk_id) {
        if (chunk_id >= m_total) return;
        size_t byte_idx = chunk_id / 8;
        uint8_t bit = 1 << (chunk_id % 8);
        if (!(m_chunks[byte_idx] & bit)) {
            m_chunks[byte_idx] |= bit;
            m_received++;
        }
    }
    
    bool is_received(uint32_t chunk_id) const {
        if (chunk_id >= m_total) return false;
        size_t byte_idx = chunk_id / 8;
        uint8_t bit = 1 << (chunk_id % 8);
        return (m_chunks[byte_idx] & bit) != 0;
    }
    
    std::vector<uint32_t> get_missing_chunks() const {
        std::vector<uint32_t> missing;
        for (uint32_t i = 0; i < m_total; ++i) {
            if (!is_received(i)) {
                missing.push_back(i);
            }
        }
        return missing;
    }
    
    std::vector<uint32_t> get_received_chunks() const {
        std::vector<uint32_t> received;
        for (uint32_t i = 0; i < m_total; ++i) {
            if (is_received(i)) {
                received.push_back(i);
            }
        }
        return received;
    }
    
    uint32_t received_count() const { return m_received; }
    uint32_t total_count() const { return m_total; }
    bool is_complete() const { return m_received >= m_total; }
    float progress() const { return m_total ? float(m_received) / m_total : 0; }
    
    // Serialize for resume protocol
    std::vector<uint8_t> to_bitmap() const { return m_chunks; }
    
    void from_bitmap(const std::vector<uint8_t>& bitmap) {
        m_chunks = bitmap;
        m_received = 0;
        for (uint32_t i = 0; i < m_total; ++i) {
            if (is_received(i)) m_received++;
        }
    }

private:
    uint32_t m_total;
    uint32_t m_received;
    std::vector<uint8_t> m_chunks;
};

/**
 * @brief Work item for parallel stream workers
 */
struct ParallelWork {
    uint32_t file_id;
    uint32_t chunk_id;
    uint64_t offset;
    uint32_t size;
    std::string file_path;  // For sender
    std::vector<uint8_t> data;  // For pre-loaded data
};

/**
 * @brief Production-grade parallel file transfer
 * 
 * Uses N TCP streams to send/receive file chunks in parallel.
 * Supports resume by tracking received chunks.
 */
class ParallelTransfer {
public:
    static constexpr size_t DEFAULT_STREAMS = 4;
    static constexpr size_t DEFAULT_CHUNK_SIZE = 2 * 1024 * 1024;  // 2MB
    
    struct Config {
        size_t num_streams = DEFAULT_STREAMS;
        size_t chunk_size = DEFAULT_CHUNK_SIZE;
        int connect_timeout_ms = 10000;
        int transfer_timeout_ms = 30000;
    };
    
    struct Stats {
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t bytes_total = 0;
        uint32_t chunks_completed = 0;
        uint32_t chunks_total = 0;
        double speed_bps = 0;
        int eta_seconds = 0;
        Clock::time_point start_time;
    };
    
    using ProgressCallback = std::function<void(const Stats&)>;
    using ErrorCallback = std::function<void(TeleportError, const std::string&)>;
    
    explicit ParallelTransfer(const Config& config = Config{});
    ~ParallelTransfer();
    
    /**
     * @brief Connect N streams to destination
     */
    Result<void> connect(const std::string& ip, uint16_t port);
    
    /**
     * @brief Accept N incoming streams
     */
    Result<void> accept(pal::TcpSocket& listen_socket);
    
    /**
     * @brief Send a file using parallel streams
     * @param file_path Path to file to send
     * @param file_id Unique file identifier
     * @param skip_chunks Chunks already received (for resume)
     */
    Result<void> send_file(
        const std::string& file_path,
        uint32_t file_id,
        const std::vector<uint32_t>& skip_chunks = {}
    );
    
    /**
     * @brief Receive a file using parallel streams
     * @param output_path Where to write the file
     * @param file_id Expected file identifier
     * @param file_size Expected file size
     * @param existing_chunks Already received chunks (for resume)
     */
    Result<void> receive_file(
        const std::string& output_path,
        uint32_t file_id,
        uint64_t file_size,
        const std::vector<uint32_t>& existing_chunks = {}
    );
    
    /**
     * @brief Get tracker for resume support
     */
    ChunkTracker* get_tracker(uint32_t file_id);
    
    void set_progress_callback(ProgressCallback cb) { m_progress_cb = std::move(cb); }
    void set_error_callback(ErrorCallback cb) { m_error_cb = std::move(cb); }
    
    void pause();
    void resume();
    void cancel();
    
    Stats get_stats() const { return m_stats; }
    bool is_cancelled() const { return m_cancelled.load(); }
    
    void close();

private:
    void sender_worker(size_t stream_id);
    void receiver_worker(size_t stream_id);
    void update_stats(uint64_t bytes);
    
    Config m_config;
    std::vector<std::unique_ptr<pal::TcpSocket>> m_streams;
    std::vector<std::thread> m_workers;
    
    // Work distribution
    std::queue<ParallelWork> m_work_queue;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    
    // Output file (receiver)
    std::unique_ptr<pal::File> m_output_file;
    std::mutex m_file_mutex;
    
    // Chunk tracking
    std::unordered_map<uint32_t, std::unique_ptr<ChunkTracker>> m_trackers;
    std::unordered_set<uint32_t> m_skip_chunks;
    
    // State
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_cancelled{false};
    std::mutex m_pause_mutex;
    std::condition_variable m_pause_cv;
    
    // Stats
    Stats m_stats;
    mutable std::mutex m_stats_mutex;
    
    // Callbacks
    ProgressCallback m_progress_cb;
    ErrorCallback m_error_cb;
};

} // namespace teleport

#endif // TELEPORT_PARALLEL_TRANSFER_HPP
