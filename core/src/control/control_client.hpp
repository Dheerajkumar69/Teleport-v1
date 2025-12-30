/**
 * @file control_client.hpp
 * @brief TCP control channel client (sender side)
 */

#ifndef TELEPORT_CONTROL_CLIENT_HPP
#define TELEPORT_CONTROL_CLIENT_HPP

#include "teleport/types.h"
#include "callbacks.hpp"
#include "protocol.hpp"
#include "platform/pal.hpp"
#include <atomic>
#include <thread>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>

namespace teleport {

/**
 * @brief Control client for sending file transfers
 */
class ControlClient {
public:
    ControlClient(const Config& config);
    ~ControlClient();
    
    /**
     * @brief Send files to a remote device
     * @param target Target device (from discovery)
     * @param file_paths Files to send
     * @param on_progress Progress callback
     * @param on_complete Completion callback
     */
    Result<void> send_files(
        const Device& target,
        const std::vector<std::string>& file_paths,
        OnTransferProgress on_progress,
        OnTransferComplete on_complete
    );
    
    /**
     * @brief Pause the current transfer
     */
    Result<void> pause();
    
    /**
     * @brief Resume a paused transfer
     */
    Result<void> resume();
    
    /**
     * @brief Cancel the current transfer
     */
    Result<void> cancel();
    
    /**
     * @brief Get current transfer state
     */
    TransferState state() const { return m_state.load(); }
    
private:
    Result<void> connect_to_target(const Device& target);
    Result<void> perform_handshake();
    Result<void> send_file_list(const std::vector<FileInfo>& files);
    Result<void> wait_for_acceptance(uint16_t& data_port);
    Result<void> transfer_files(const std::vector<FileInfo>& files);
    Result<void> transfer_file(const FileInfo& file);
    
    Config m_config;
    std::unique_ptr<pal::TcpSocket> m_socket;
    std::atomic<TransferState> m_state;
    
    std::vector<FileInfo> m_files;
    TransferStats m_stats;
    
    OnTransferProgress m_on_progress;
    OnTransferComplete m_on_complete;
    
    std::mutex m_control_mutex;
    std::condition_variable m_control_cv;
    bool m_pause_requested = false;
    bool m_cancel_requested = false;
};

} // namespace teleport

#endif // TELEPORT_CONTROL_CLIENT_HPP
