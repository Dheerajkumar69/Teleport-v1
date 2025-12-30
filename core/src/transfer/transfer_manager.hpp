/**
 * @file transfer_manager.hpp
 * @brief High-level transfer orchestration
 */

#ifndef TELEPORT_TRANSFER_MANAGER_HPP
#define TELEPORT_TRANSFER_MANAGER_HPP

#include "teleport/types.h"
#include "teleport/errors.h"
#include "chunk_reader.hpp"
#include "chunk_writer.hpp"
#include "stream_pool.hpp"
#include <memory>
#include <vector>
#include <atomic>
#include <functional>

namespace teleport {

/**
 * @brief Manages file transfer operations
 */
class TransferManager {
public:
    TransferManager(const Config& config);
    ~TransferManager();
    
    /**
     * @brief Send files to a connected socket
     */
    Result<void> send_files(
        pal::TcpSocket& socket,
        const std::vector<FileInfo>& files,
        std::function<void(const TransferStats&)> on_progress
    );
    
    /**
     * @brief Receive files from a connected socket
     */
    Result<void> receive_files(
        pal::TcpSocket& socket,
        const std::vector<FileInfo>& files,
        const std::string& output_dir,
        std::function<void(const TransferStats&)> on_progress
    );
    
    /**
     * @brief Pause transfer
     */
    void pause();
    
    /**
     * @brief Resume transfer
     */
    void resume();
    
    /**
     * @brief Cancel transfer
     */
    void cancel();
    
    /**
     * @brief Check if paused
     */
    bool is_paused() const { return m_paused.load(); }
    
    /**
     * @brief Check if cancelled
     */
    bool is_cancelled() const { return m_cancelled.load(); }
    
private:
    Config m_config;
    std::atomic<bool> m_paused;
    std::atomic<bool> m_cancelled;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

} // namespace teleport

#endif // TELEPORT_TRANSFER_MANAGER_HPP
