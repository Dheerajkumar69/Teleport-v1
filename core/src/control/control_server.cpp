/**
 * @file control_server.cpp
 * @brief Control server implementation
 */

#include "control_server.hpp"
#include "utils/logger.hpp"
#include "utils/uuid.hpp"
#include "utils/sanitize.hpp"
#include "security/token.hpp"

// Security limits
static constexpr size_t MAX_FILES_PER_TRANSFER = 10000;
static constexpr uint64_t MAX_TOTAL_SIZE = 100ULL * 1024 * 1024 * 1024; // 100 GB

namespace teleport {

ControlServer::ControlServer(const Config& config)
    : m_config(config)
    , m_port(0)
    , m_output_dir(config.download_path)
    , m_running(false) {
}

ControlServer::~ControlServer() {
    stop();
}

Result<uint16_t> ControlServer::start(
    OnIncomingTransfer on_incoming,
    OnTransferProgress on_progress,
    OnTransferComplete on_complete
) {
    if (m_running.load()) {
        return make_error(TELEPORT_ERROR_ALREADY_RUNNING, "Server already running");
    }
    
    m_on_incoming = std::move(on_incoming);
    m_on_progress = std::move(on_progress);
    m_on_complete = std::move(on_complete);
    
    // Create server socket
    pal::SocketOptions opts;
    opts.reuse_addr = true;
    m_server_socket = pal::create_tcp_socket(opts);
    
    if (!m_server_socket || !m_server_socket->is_valid()) {
        return make_error(TELEPORT_ERROR_SOCKET_CREATE, "Failed to create server socket");
    }
    
    // Try to bind to configured port or find available one
    uint16_t try_port = m_config.control_port;
    if (try_port == 0) {
        try_port = TELEPORT_CONTROL_PORT_MIN;
    }
    
    bool bound = false;
    for (int i = 0; i < 100 && !bound; ++i) {
        auto result = m_server_socket->bind(try_port);
        if (result) {
            bound = true;
            m_port = try_port;
        } else {
            try_port++;
            if (try_port > TELEPORT_CONTROL_PORT_MAX) {
                try_port = TELEPORT_CONTROL_PORT_MIN;
            }
        }
    }
    
    if (!bound) {
        return make_error(TELEPORT_ERROR_SOCKET_BIND, "Could not find available port");
    }
    
    auto listen_result = m_server_socket->listen(5);
    if (!listen_result) {
        return listen_result.error();
    }
    
    // Ensure output directory exists
    pal::create_directory(m_output_dir);
    
    m_running.store(true);
    m_accept_thread = std::thread(&ControlServer::accept_loop, this);
    
    LOG_INFO("Control server started on port ", m_port);
    return m_port;
}

void ControlServer::stop() {
    if (m_running.load()) {
        m_running.store(false);
        
        if (m_server_socket) {
            m_server_socket->close();
        }
        
        if (m_accept_thread.joinable()) {
            m_accept_thread.join();
        }
        
        m_server_socket.reset();
        LOG_INFO("Control server stopped");
    }
}

void ControlServer::accept_loop() {
    while (m_running.load()) {
        m_server_socket->set_recv_timeout(1000);
        
        auto result = m_server_socket->accept();
        if (!result) {
            // Timeout or shutdown
            continue;
        }
        
        auto client = std::move(*result);
        LOG_INFO("Incoming connection from ", client->remote_address().to_string());
        
        // Handle in current thread for simplicity
        // TODO: Thread pool for concurrent transfers
        handle_connection(std::move(client));
    }
}

void ControlServer::handle_connection(std::unique_ptr<pal::TcpSocket> client) {
    TeleportError final_error = TELEPORT_OK;
    
    try {
        MessageReader reader(*client);
        MessageWriter writer(*client);
        
        // Receive handshake
        Device sender;
        auto handshake_result = perform_handshake(*client, sender);
        if (!handshake_result) {
            LOG_ERROR("Handshake failed: ", handshake_result.error().message);
            final_error = static_cast<TeleportError>(handshake_result.error().code);
            goto cleanup;
        }
        
        // Receive file list
        {
            auto msg_result = reader.read();
            if (!msg_result) {
                LOG_ERROR("Failed to receive file list");
                final_error = TELEPORT_ERROR_PROTOCOL;
                goto cleanup;
            }
            
            if (msg_result.value().type != ControlMessageType::FileList) {
                LOG_ERROR("Expected FILE_LIST, got ", message_type_to_string(msg_result.value().type));
                final_error = TELEPORT_ERROR_PROTOCOL;
                goto cleanup;
            }
            
            auto file_list = FileListMessage::from_json(msg_result.value().payload);
            
            // Build transfer info
            IncomingTransfer transfer;
            transfer.sender = sender;
            transfer.total_size = file_list.total_size;
            
            for (const auto& f : file_list.files) {
                FileInfo info;
                info.id = f.id;
                info.name = f.name;
                info.size = f.size;
                transfer.files.push_back(info);
            }
            
            // Ask user to accept
            bool accepted = m_on_incoming ? m_on_incoming(transfer) : false;
            
            if (!accepted) {
                AcceptRejectMessage reject_msg;
                reject_msg.accepted = false;
                reject_msg.reason = "User declined";
                writer.write(ControlMessage::reject(reject_msg));
                LOG_INFO("Transfer rejected by user");
                final_error = TELEPORT_ERROR_REJECTED;
                goto cleanup;
            }
            
            // Accept transfer
            // TODO: Create data channel on separate port
            AcceptRejectMessage accept_msg;
            accept_msg.accepted = true;
            accept_msg.data_port = m_port;  // Use same port for now
            auto accept_result = writer.write(ControlMessage::accept(accept_msg));
            if (!accept_result) {
                final_error = static_cast<TeleportError>(accept_result.error().code);
                goto cleanup;
            }
            
            LOG_INFO("Transfer accepted, receiving ", transfer.files.size(), " files");
            
            // Receive files
            auto recv_result = receive_files(*client, transfer.files, accept_msg.data_port);
            if (!recv_result) {
                final_error = static_cast<TeleportError>(recv_result.error().code);
                goto cleanup;
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in connection handler: ", e.what());
        final_error = TELEPORT_ERROR_INTERNAL;
    }
    
cleanup:
    client->close();
    
    if (m_on_complete) {
        m_on_complete(final_error);
    }
}

Result<void> ControlServer::perform_handshake(pal::TcpSocket& socket, Device& sender) {
    MessageReader reader(socket);
    MessageWriter writer(socket);
    
    // Receive handshake
    auto msg_result = reader.read();
    if (!msg_result) {
        return msg_result.error();
    }
    
    if (msg_result.value().type != ControlMessageType::Handshake) {
        return make_error(TELEPORT_ERROR_PROTOCOL, "Expected HANDSHAKE");
    }
    
    auto handshake = HandshakeMessage::from_json(msg_result.value().payload);
    
    // Validate protocol version
    if (handshake.protocol_version != TELEPORT_PROTOCOL_VERSION) {
        ErrorMessage err;
        err.code = TELEPORT_ERROR_PROTOCOL;
        err.message = "Protocol version mismatch";
        err.fatal = true;
        writer.write(ControlMessage::error(err));
        return make_error(TELEPORT_ERROR_PROTOCOL, "Protocol version mismatch");
    }
    
    // Store sender info
    sender.name = handshake.device_name;
    sender.os = os_from_string(handshake.device_os);
    sender.address = socket.remote_address();
    
    // Send handshake ack
    HandshakeMessage ack;
    ack.protocol_version = TELEPORT_PROTOCOL_VERSION;
    ack.device_name = m_config.device_name;
    ack.device_os = os_to_string(pal::get_os_type());
    ack.session_token = generate_session_token();
    
    auto write_result = writer.write(ControlMessage::handshake_ack(ack));
    if (!write_result) {
        return write_result.error();
    }
    
    LOG_INFO("Handshake complete with ", sender.name);
    return ok();
}

Result<void> ControlServer::receive_files(
    pal::TcpSocket& socket,
    const std::vector<FileInfo>& files,
    uint16_t data_port
) {
    MessageReader reader(socket);
    MessageWriter writer(socket);
    
    // Wait for START message
    auto start_result = reader.read();
    if (!start_result) {
        return start_result.error();
    }
    
    if (start_result.value().type != ControlMessageType::Start) {
        return make_error(TELEPORT_ERROR_PROTOCOL, "Expected START");
    }
    
    // Receive data for each file
    TransferStats stats;
    stats.files_total = static_cast<uint32_t>(files.size());
    stats.start_time = Clock::now();
    
    for (const auto& file : files) {
        stats.bytes_total += file.size;
    }
    
    for (const auto& file : files) {
        // SECURITY: Sanitize filename to prevent path traversal attacks
        std::string safe_name = sanitize_filename(file.name);
        if (safe_name.empty() || safe_name == "unnamed") {
            safe_name = "file_" + std::to_string(file.id);
        }
        
        std::string output_path = m_output_dir + "/" + safe_name;
        LOG_INFO("Receiving: ", safe_name, " (", file.size, " bytes)");
        
        if (file.name != safe_name) {
            LOG_WARN("Sanitized filename from '", file.name, "' to '", safe_name, "'");
        }
        
        auto file_result = pal::open_file(output_path, pal::FileMode::Write);
        if (!file_result) {
            return file_result.error();
        }
        
        auto& out_file = *file_result;
        
        // Receive file data in chunks
        uint64_t bytes_remaining = file.size;
        std::vector<uint8_t> buffer(m_config.chunk_size);
        
        while (bytes_remaining > 0) {
            // Read chunk header (16 bytes)
            uint8_t header[ChunkHeader::HEADER_SIZE];
            auto header_result = socket.recv_all(header, ChunkHeader::HEADER_SIZE);
            if (!header_result) {
                return header_result.error();
            }
            
            auto chunk = ChunkHeader::deserialize(header);
            
            if (chunk.file_id != file.id) {
                return make_error(TELEPORT_ERROR_PROTOCOL, "File ID mismatch in chunk");
            }
            
            // Read chunk data
            size_t to_read = std::min(static_cast<size_t>(chunk.size), buffer.size());
            auto data_result = socket.recv_all(buffer.data(), to_read);
            if (!data_result) {
                return data_result.error();
            }
            
            // Write to file
            auto write_result = out_file->write(buffer.data(), to_read);
            if (!write_result) {
                return write_result.error();
            }
            
            bytes_remaining -= to_read;
            stats.bytes_transferred += to_read;
            
            // Update progress
            auto now = Clock::now();
            auto elapsed = std::chrono::duration_cast<Milliseconds>(now - stats.start_time).count();
            if (elapsed > 0) {
                stats.speed_bps = (stats.bytes_transferred * 1000.0) / elapsed;
            }
            
            if (m_on_progress) {
                m_on_progress(stats);
            }
        }
        
        out_file->flush();
        stats.files_completed++;
        LOG_INFO("Received: ", file.name);
    }
    
    // Send completion
    CompleteMessage complete;
    complete.success = true;
    complete.files_transferred = stats.files_completed;
    complete.bytes_transferred = stats.bytes_transferred;
    writer.write(ControlMessage::complete(complete));
    
    LOG_INFO("Transfer complete: ", stats.files_completed, " files, ", 
             stats.bytes_transferred, " bytes");
    
    return ok();
}

} // namespace teleport
