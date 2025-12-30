/**
 * @file control_server.hpp
 * @brief TCP control channel server (receiver side)
 */

#ifndef TELEPORT_CONTROL_SERVER_HPP
#define TELEPORT_CONTROL_SERVER_HPP

#include "teleport/types.h"
#include "teleport/teleport.h"
#include "teleport/errors.h"
#include "callbacks.hpp"
#include "protocol.hpp"
#include "platform/pal.hpp"
#include <atomic>
#include <thread>
#include <functional>
#include <memory>

namespace teleport {

// Note: IncomingTransfer, OnIncomingTransfer, OnTransferProgress, OnTransferComplete
// are defined in callbacks.hpp

/**
 * @brief Control server for receiving file transfers
 */
class ControlServer {
public:
    ControlServer(const Config& config);
    ~ControlServer();
    
    /**
     * @brief Start listening for incoming connections
     * @param on_incoming Callback when transfer is requested (return true to accept)
     * @param on_progress Progress callback
     * @param on_complete Completion callback
     * @return Port number on success
     */
    Result<uint16_t> start(
        OnIncomingTransfer on_incoming,
        OnTransferProgress on_progress,
        OnTransferComplete on_complete
    );
    
    /**
     * @brief Stop the server
     */
    void stop();
    
    /**
     * @brief Check if server is running
     */
    bool is_running() const { return m_running.load(); }
    
    /**
     * @brief Get the listening port
     */
    uint16_t port() const { return m_port; }
    
    /**
     * @brief Set output directory for received files
     */
    void set_output_dir(const std::string& dir) { m_output_dir = dir; }
    
private:
    void accept_loop();
    void handle_connection(std::unique_ptr<pal::TcpSocket> client);
    Result<void> perform_handshake(pal::TcpSocket& socket, Device& sender);
    Result<void> receive_files(pal::TcpSocket& socket, 
                                const std::vector<FileInfo>& files,
                                uint16_t data_port);
    
    Config m_config;
    std::unique_ptr<pal::TcpSocket> m_server_socket;
    uint16_t m_port;
    std::string m_output_dir;
    
    std::atomic<bool> m_running;
    std::thread m_accept_thread;
    
    OnIncomingTransfer m_on_incoming;
    OnTransferProgress m_on_progress;
    OnTransferComplete m_on_complete;
};

} // namespace teleport

#endif // TELEPORT_CONTROL_SERVER_HPP
