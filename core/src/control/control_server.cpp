/**
 * @file control_server.cpp
 * @brief Control server implementation
 */

#include "control_server.hpp"
#include "transfer/parallel_transfer.hpp"
#include "utils/logger.hpp"
#include "utils/uuid.hpp"
#include "utils/sanitize.hpp"
#include "security/token.hpp"

#ifdef TELEPORT_WINDOWS
#include <fileapi.h>
#endif

// Security limits
static constexpr size_t MAX_FILES_PER_TRANSFER = 10000;
static constexpr uint64_t MAX_TOTAL_SIZE = 100ULL * 1024 * 1024 * 1024; // 100 GB
static constexpr int MAX_CONNECTIONS_PER_SECOND = 10;  // Rate limiting

// SECURITY: Get available disk space for a path
static uint64_t get_available_disk_space(const std::string& path) {
#ifdef TELEPORT_WINDOWS
    ULARGE_INTEGER free_bytes;
    if (GetDiskFreeSpaceExA(path.c_str(), &free_bytes, nullptr, nullptr)) {
        return free_bytes.QuadPart;
    }
    return 0;  // Unable to determine
#else
    // POSIX implementation (future)
    return UINT64_MAX;  // Assume sufficient space on non-Windows
#endif
}

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
    int connections_this_second = 0;
    auto last_reset = std::chrono::steady_clock::now();
    
    while (m_running.load()) {
        // Rate limiting: reset counter every second
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_reset).count() >= 1) {
            connections_this_second = 0;
            last_reset = now;
        }
        
        m_server_socket->set_recv_timeout(1000);
        
        auto result = m_server_socket->accept();
        if (!result) {
            // Timeout or shutdown
            continue;
        }
        
        // Rate limiting check
        if (connections_this_second >= MAX_CONNECTIONS_PER_SECOND) {
            LOG_WARN("Rate limit exceeded, dropping connection");
            result.value()->close();
            continue;
        }
        connections_this_second++;
        
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
            
            // SECURITY: Enforce transfer limits (M3+M4 fix)
            if (file_list.files.size() > MAX_FILES_PER_TRANSFER) {
                LOG_WARN("Transfer rejected: too many files (", file_list.files.size(), " > ", MAX_FILES_PER_TRANSFER, ")");
                AcceptRejectMessage reject_msg;
                reject_msg.accepted = false;
                reject_msg.reason = "Too many files";
                writer.write(ControlMessage::reject(reject_msg));
                final_error = TELEPORT_ERROR_INVALID_ARGUMENT;
                goto cleanup;
            }
            
            if (file_list.total_size > MAX_TOTAL_SIZE) {
                LOG_WARN("Transfer rejected: size limit exceeded (", file_list.total_size, " > ", MAX_TOTAL_SIZE, ")");
                AcceptRejectMessage reject_msg;
                reject_msg.accepted = false;
                reject_msg.reason = "Transfer size exceeds limit";
                writer.write(ControlMessage::reject(reject_msg));
                final_error = TELEPORT_ERROR_INVALID_ARGUMENT;
                goto cleanup;
            }
            
            // SECURITY: Check available disk space before accepting
            {
                uint64_t available_space = get_available_disk_space(m_output_dir);
                // Add 10% buffer for filesystem overhead
                uint64_t required_space = file_list.total_size + (file_list.total_size / 10);
                if (available_space > 0 && required_space > available_space) {
                    LOG_WARN("Transfer rejected: insufficient disk space (need ", 
                             required_space, ", have ", available_space, ")");
                    AcceptRejectMessage reject_msg;
                    reject_msg.accepted = false;
                    reject_msg.reason = "Insufficient disk space";
                    writer.write(ControlMessage::reject(reject_msg));
                    final_error = TELEPORT_ERROR_FILE_WRITE;
                    goto cleanup;
                }
            }
            
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
            
            // Accept transfer — bind a dedicated parallel-data port.
            // Bind BEFORE sending Accept so we are guaranteed to be
            // listening by the time the client tries to connect streams.
            pal::SocketOptions data_opts;
            data_opts.reuse_addr = true;
            auto data_listen = pal::create_tcp_socket(data_opts);
            if (!data_listen || !data_listen->is_valid()) {
                AcceptRejectMessage reject_msg;
                reject_msg.accepted = false;
                reject_msg.reason   = "Server out of resources";
                writer.write(ControlMessage::reject(reject_msg));
                final_error = TELEPORT_ERROR_SOCKET_CREATE;
                goto cleanup;
            }

            // Port 0 → OS picks an ephemeral port (guaranteed free).
            auto bind_result = data_listen->bind(0);
            if (!bind_result) {
                AcceptRejectMessage reject_msg;
                reject_msg.accepted = false;
                reject_msg.reason   = "Could not bind data port";
                writer.write(ControlMessage::reject(reject_msg));
                final_error = TELEPORT_ERROR_SOCKET_BIND;
                goto cleanup;
            }
            auto listen_result = data_listen->listen(ParallelTransfer::DEFAULT_STREAMS + 4);
            if (!listen_result) {
                AcceptRejectMessage reject_msg;
                reject_msg.accepted = false;
                reject_msg.reason   = "Could not listen on data port";
                writer.write(ControlMessage::reject(reject_msg));
                final_error = TELEPORT_ERROR_SOCKET_BIND;
                goto cleanup;
            }

            uint16_t data_port = data_listen->local_address().port;
            LOG_INFO("Parallel data port: ", data_port);

            AcceptRejectMessage accept_msg;
            accept_msg.accepted  = true;
            accept_msg.data_port = data_port;
            auto accept_result = writer.write(ControlMessage::accept(accept_msg));
            if (!accept_result) {
                final_error = static_cast<TeleportError>(accept_result.error().code);
                goto cleanup;
            }

            LOG_INFO("Transfer accepted, receiving ", transfer.files.size(), " files in parallel");

            // Receive files via ParallelTransfer.
            // Pass the control socket so receive_files_parallel can drain the
            // START control message and send the final Complete ack.
            auto recv_result = receive_files_parallel(
                *client, *data_listen, transfer.files
            );
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

Result<void> ControlServer::receive_files_parallel(
    pal::TcpSocket& control_sock,
    pal::TcpSocket& data_listen,
    const std::vector<FileInfo>& files
) {
    // ---- Step 1: drain the START message from the control socket ----
    // The client sends START on the control connection *before* connecting
    // any data streams.  We must read it here to keep the control socket
    // in a clean state and avoid a protocol mismatch.
    {
        MessageReader ctrl_reader(control_sock);
        auto start_result = ctrl_reader.read();
        if (!start_result) {
            return start_result.error();
        }
        if (start_result.value().type != ControlMessageType::Start) {
            return make_error(TELEPORT_ERROR_PROTOCOL,
                              "Expected START before parallel data streams");
        }
    }

    // ---- Step 2: configure ParallelTransfer ----
    ParallelTransfer::Config pt_cfg;
    pt_cfg.num_streams         = ParallelTransfer::DEFAULT_STREAMS;
    pt_cfg.chunk_size          = m_config.chunk_size;
    pt_cfg.connect_timeout_ms  = 10000;
    pt_cfg.transfer_timeout_ms = 30000;

    ParallelTransfer pt(pt_cfg);

    // Wire progress callback
    pt.set_progress_callback([this](const ParallelTransfer::Stats& s) {
        TransferStats ts;
        ts.bytes_total       = s.bytes_total;
        ts.bytes_transferred = s.bytes_received;
        ts.speed_bps         = s.speed_bps;
        ts.eta_seconds       = s.eta_seconds;
        ts.start_time        = s.start_time;
        if (m_on_progress) {
            m_on_progress(ts);
        }
    });

    // ---- Step 3: accept N incoming streams ----
    auto accept_result = pt.accept(data_listen);
    if (!accept_result) {
        return accept_result.error();
    }

    // ---- Step 4: receive each file ----
    uint32_t files_completed = 0;
    uint64_t bytes_total     = 0;

    for (const auto& file : files) {
        // Sanitize output filename (path traversal defence)
        std::string safe_name = sanitize_filename(file.name);
        if (safe_name.empty() || safe_name == "unnamed") {
            safe_name = "file_" + std::to_string(file.id);
        }
        if (file.name != safe_name) {
            LOG_WARN("Sanitized filename '", file.name, "' -> '", safe_name, "'");
        }

        std::string output_path = m_output_dir + "/" + safe_name;
        LOG_INFO("Parallel receiving: ", safe_name, " (", file.size, " bytes)");

        auto recv_result = pt.receive_file(output_path, file.id, file.size, {});
        if (!recv_result) {
            return recv_result.error();
        }

        files_completed++;
        bytes_total += file.size;
        LOG_INFO("Parallel received: ", safe_name);
    }

    pt.close();

    LOG_INFO("Parallel transfer complete: ", files_completed, " files, ", bytes_total, " bytes");

    // ---- Step 5: send Complete ack over the control socket ----
    {
        MessageWriter ctrl_writer(control_sock);
        CompleteMessage complete;
        complete.success          = true;
        complete.files_transferred = files_completed;
        complete.bytes_transferred = bytes_total;
        ctrl_writer.write(ControlMessage::complete(complete));
    }

    if (m_on_complete) {
        m_on_complete(TELEPORT_OK);
    }

    return ok();
}

Result<void> ControlServer::receive_files(
    pal::TcpSocket& socket,
    const std::vector<FileInfo>& files,
    [[maybe_unused]] uint16_t data_port
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

            uint64_t expected_offset = file.size - bytes_remaining;
            if (static_cast<uint64_t>(chunk.offset) != expected_offset) {
                return make_error(TELEPORT_ERROR_PROTOCOL, "Unexpected chunk offset");
            }

            if (chunk.size == 0) {
                return make_error(TELEPORT_ERROR_PROTOCOL, "Zero-sized chunk not allowed");
            }

            if (chunk.size > m_config.chunk_size) {
                return make_error(TELEPORT_ERROR_PROTOCOL,
                                  "Chunk size exceeds configured limit");
            }

            if (chunk.size > bytes_remaining) {
                return make_error(TELEPORT_ERROR_PROTOCOL,
                                  "Chunk size exceeds remaining file bytes");
            }
            
            // Read chunk data
            size_t to_read = static_cast<size_t>(chunk.size);
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
