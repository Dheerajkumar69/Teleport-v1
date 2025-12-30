/**
 * @file control_client.cpp
 * @brief Control client implementation
 */

#include "control_client.hpp"
#include "utils/logger.hpp"
#include "utils/sanitize.hpp"

namespace {
    // Timeout constants (milliseconds)
    constexpr int HANDSHAKE_TIMEOUT_MS = 30000;  // 30 seconds
    constexpr int SOCKET_TIMEOUT_MS = 30000;     // 30 seconds
    constexpr int CONNECT_TIMEOUT_MS = 10000;    // 10 seconds
}

namespace teleport {

ControlClient::ControlClient(const Config& config)
    : m_config(config)
    , m_state(TransferState::Idle) {
}

ControlClient::~ControlClient() {
    cancel();
    if (m_socket) {
        m_socket->close();
    }
}

Result<void> ControlClient::send_files(
    const Device& target,
    const std::vector<std::string>& file_paths,
    OnTransferProgress on_progress,
    OnTransferComplete on_complete
) {
    if (m_state.load() != TransferState::Idle) {
        return make_error(TELEPORT_ERROR_ALREADY_RUNNING, "Transfer already in progress");
    }
    
    m_on_progress = std::move(on_progress);
    m_on_complete = std::move(on_complete);
    m_pause_requested = false;
    m_cancel_requested = false;
    
    // Build file list
    m_files.clear();
    m_stats = TransferStats();
    m_stats.start_time = Clock::now();
    
    uint32_t file_id = 0;
    for (const auto& path : file_paths) {
        if (!pal::file_exists(path)) {
            return make_error(TELEPORT_ERROR_FILE_OPEN, "File not found: " + path);
        }
        
        FileInfo info;
        info.id = file_id++;
        info.path = path;
        info.name = pal::get_filename(path);
        info.size = pal::file_size(path);
        info.total_chunks = static_cast<uint32_t>((info.size + m_config.chunk_size - 1) / m_config.chunk_size);
        
        m_files.push_back(info);
        m_stats.bytes_total += info.size;
    }
    m_stats.files_total = static_cast<uint32_t>(m_files.size());
    
    LOG_INFO("Sending ", m_files.size(), " files to ", target.name);
    
    TeleportError final_error = TELEPORT_OK;
    
    try {
        // Connect
        m_state.store(TransferState::Connecting);
        auto connect_result = connect_to_target(target);
        if (!connect_result) {
            final_error = static_cast<TeleportError>(connect_result.error().code);
            goto cleanup;
        }
        
        // Handshake
        m_state.store(TransferState::Handshaking);
        auto handshake_result = perform_handshake();
        if (!handshake_result) {
            final_error = static_cast<TeleportError>(handshake_result.error().code);
            goto cleanup;
        }
        
        // Send file list
        auto list_result = send_file_list(m_files);
        if (!list_result) {
            final_error = static_cast<TeleportError>(list_result.error().code);
            goto cleanup;
        }
        
        // Wait for acceptance
        uint16_t data_port;
        auto accept_result = wait_for_acceptance(data_port);
        if (!accept_result) {
            final_error = static_cast<TeleportError>(accept_result.error().code);
            goto cleanup;
        }
        
        // Transfer files
        m_state.store(TransferState::Transferring);
        auto transfer_result = transfer_files(m_files);
        if (!transfer_result) {
            final_error = static_cast<TeleportError>(transfer_result.error().code);
            goto cleanup;
        }
        
        // Wait for completion ack
        MessageReader reader(*m_socket);
        auto complete_result = reader.read();
        if (complete_result && complete_result.value().type == ControlMessageType::Complete) {
            auto complete = CompleteMessage::from_json(complete_result.value().payload);
            if (complete.success) {
                LOG_INFO("Transfer complete: ", complete.files_transferred, " files");
            }
        }
        
        m_state.store(TransferState::Complete);
        
    } catch (const nlohmann::json::exception& e) {
        // JSON parsing error from protocol messages
        LOG_ERROR("JSON protocol error during transfer: ", e.what());
        final_error = TELEPORT_ERROR_PROTOCOL;
    } catch (const std::runtime_error& e) {
        // Runtime errors (network, file I/O)
        LOG_ERROR("Runtime error during transfer: ", e.what());
        final_error = TELEPORT_ERROR_TRANSFER_FAILED;
    } catch (const std::exception& e) {
        // Other standard exceptions
        LOG_ERROR("Unexpected exception during transfer: ", e.what());
        final_error = TELEPORT_ERROR_INTERNAL;
    } catch (...) {
        // Non-standard exceptions (shouldn't happen but safety)
        LOG_ERROR("Unknown exception during transfer");
        final_error = TELEPORT_ERROR_INTERNAL;
    }
    
cleanup:
    if (final_error != TELEPORT_OK) {
        m_state.store(TransferState::Failed);
    }
    
    if (m_socket) {
        m_socket->close();
        m_socket.reset();
    }
    
    if (m_on_complete) {
        m_on_complete(final_error);
    }
    
    m_state.store(TransferState::Idle);
    
    return final_error == TELEPORT_OK ? ok() : make_error(final_error);
}

Result<void> ControlClient::connect_to_target(const Device& target) {
    // Validate target address
    if (target.address.ip.empty() || !validate_ipv4(target.address.ip)) {
        return make_error(TELEPORT_ERROR_INVALID_ARGUMENT, "Invalid target IP address");
    }
    if (!validate_port(target.address.port)) {
        return make_error(TELEPORT_ERROR_INVALID_ARGUMENT, "Invalid target port");
    }
    
    pal::SocketOptions opts;
    opts.recv_timeout_ms = SOCKET_TIMEOUT_MS;
    opts.send_timeout_ms = SOCKET_TIMEOUT_MS;
    
    m_socket = pal::create_tcp_socket(opts);
    if (!m_socket || !m_socket->is_valid()) {
        return make_error(TELEPORT_ERROR_SOCKET_CREATE, "Failed to create socket");
    }
    
    LOG_INFO("Connecting to ", target.address.to_string());
    
    auto result = m_socket->connect(target.address.ip, target.address.port, CONNECT_TIMEOUT_MS);
    if (!result) {
        LOG_ERROR("Connection failed: ", result.error().message);
        return result.error();
    }
    
    LOG_INFO("Connected to ", target.name);
    return ok();
}

Result<void> ControlClient::perform_handshake() {
    // Set receive timeout specifically for handshake
    m_socket->set_recv_timeout(HANDSHAKE_TIMEOUT_MS);
    
    MessageReader reader(*m_socket);
    MessageWriter writer(*m_socket);
    
    // Send handshake
    HandshakeMessage handshake;
    handshake.protocol_version = TELEPORT_PROTOCOL_VERSION;
    handshake.device_name = m_config.device_name;
    handshake.device_os = os_to_string(pal::get_os_type());
    
    LOG_DEBUG("Sending handshake with protocol version ", handshake.protocol_version);
    
    auto write_result = writer.write(ControlMessage::handshake(handshake));
    if (!write_result) {
        LOG_ERROR("Failed to send handshake: ", write_result.error().message);
        return write_result.error();
    }
    
    // Receive handshake ack with timeout
    auto msg_result = reader.read();
    if (!msg_result) {
        if (msg_result.error().code == static_cast<int>(TELEPORT_ERROR_TIMEOUT)) {
            return make_error(TELEPORT_ERROR_TIMEOUT, "Handshake timeout - receiver not responding");
        }
        return msg_result.error();
    }
    
    if (msg_result.value().type == ControlMessageType::Error) {
        auto err = ErrorMessage::from_json(msg_result.value().payload);
        return make_error(static_cast<TeleportError>(err.code), err.message);
    }
    
    if (msg_result.value().type != ControlMessageType::HandshakeAck) {
        return make_error(TELEPORT_ERROR_PROTOCOL, "Expected HANDSHAKE_ACK");
    }
    
    LOG_INFO("Handshake completed");
    return ok();
}

Result<void> ControlClient::send_file_list(const std::vector<FileInfo>& files) {
    MessageWriter writer(*m_socket);
    
    FileListMessage msg;
    for (const auto& f : files) {
        FileListMessage::FileEntry entry;
        entry.id = f.id;
        entry.name = f.name;
        entry.size = f.size;
        msg.files.push_back(entry);
        msg.total_size += f.size;
    }
    
    return writer.write(ControlMessage::file_list(msg));
}

Result<void> ControlClient::wait_for_acceptance(uint16_t& data_port) {
    MessageReader reader(*m_socket);
    
    auto msg_result = reader.read();
    if (!msg_result) {
        return msg_result.error();
    }
    
    if (msg_result.value().type == ControlMessageType::Reject) {
        auto reject = AcceptRejectMessage::from_json(msg_result.value().payload);
        LOG_WARN("Transfer rejected: ", reject.reason);
        return make_error(TELEPORT_ERROR_REJECTED, reject.reason);
    }
    
    if (msg_result.value().type != ControlMessageType::Accept) {
        return make_error(TELEPORT_ERROR_PROTOCOL, "Expected ACCEPT or REJECT");
    }
    
    auto accept = AcceptRejectMessage::from_json(msg_result.value().payload);
    data_port = accept.data_port;
    
    LOG_INFO("Transfer accepted, data port: ", data_port);
    return ok();
}

Result<void> ControlClient::transfer_files(const std::vector<FileInfo>& files) {
    MessageWriter writer(*m_socket);
    
    // Send START
    ControlMessage start_msg;
    start_msg.type = ControlMessageType::Start;
    start_msg.payload = json::object();
    auto start_result = writer.write(start_msg);
    if (!start_result) {
        return start_result.error();
    }
    
    // Transfer each file
    for (const auto& file : files) {
        // Check for cancel
        if (m_cancel_requested) {
            return make_error(TELEPORT_ERROR_CANCELLED, "Transfer cancelled");
        }
        
        auto file_result = transfer_file(file);
        if (!file_result) {
            return file_result.error();
        }
        
        m_stats.files_completed++;
    }
    
    return ok();
}

Result<void> ControlClient::transfer_file(const FileInfo& file) {
    LOG_INFO("Sending: ", file.name, " (", file.size, " bytes)");
    
    auto file_result = pal::open_file(file.path, pal::FileMode::Read);
    if (!file_result) {
        return file_result.error();
    }
    
    auto& in_file = *file_result;
    std::vector<uint8_t> buffer(m_config.chunk_size);
    uint32_t chunk_id = 0;
    uint64_t bytes_remaining = file.size;
    
    while (bytes_remaining > 0) {
        // Handle pause
        {
            std::unique_lock<std::mutex> lock(m_control_mutex);
            while (m_pause_requested && !m_cancel_requested) {
                m_state.store(TransferState::Paused);
                m_control_cv.wait(lock);
            }
            m_state.store(TransferState::Transferring);
            
            if (m_cancel_requested) {
                return make_error(TELEPORT_ERROR_CANCELLED, "Transfer cancelled");
            }
        }
        
        // Read chunk from file
        size_t to_read = static_cast<size_t>(std::min(
            static_cast<uint64_t>(m_config.chunk_size), 
            bytes_remaining
        ));
        
        auto read_result = in_file->read(buffer.data(), to_read);
        if (!read_result) {
            return read_result.error();
        }
        
        size_t bytes_read = *read_result;
        if (bytes_read == 0) break;
        
        // Build chunk header
        ChunkHeader header;
        header.file_id = file.id;
        header.chunk_id = chunk_id++;
        header.offset = static_cast<uint32_t>(file.size - bytes_remaining);
        header.size = static_cast<uint32_t>(bytes_read);
        
        // Send header
        uint8_t header_buf[ChunkHeader::HEADER_SIZE];
        header.serialize(header_buf);
        auto header_result = m_socket->send_all(header_buf, ChunkHeader::HEADER_SIZE);
        if (!header_result) {
            return header_result.error();
        }
        
        // Send data
        auto data_result = m_socket->send_all(buffer.data(), bytes_read);
        if (!data_result) {
            return data_result.error();
        }
        
        bytes_remaining -= bytes_read;
        m_stats.bytes_transferred += bytes_read;
        
        // Update progress
        auto now = Clock::now();
        auto elapsed = std::chrono::duration_cast<Milliseconds>(now - m_stats.start_time).count();
        if (elapsed > 0) {
            m_stats.speed_bps = (m_stats.bytes_transferred * 1000.0) / elapsed;
            m_stats.eta_seconds = static_cast<int32_t>(
                (m_stats.bytes_total - m_stats.bytes_transferred) / m_stats.speed_bps
            );
        }
        
        if (m_on_progress) {
            m_on_progress(m_stats);
        }
    }
    
    LOG_INFO("Sent: ", file.name);
    return ok();
}

Result<void> ControlClient::pause() {
    std::lock_guard<std::mutex> lock(m_control_mutex);
    m_pause_requested = true;
    LOG_INFO("Pause requested");
    return ok();
}

Result<void> ControlClient::resume() {
    {
        std::lock_guard<std::mutex> lock(m_control_mutex);
        m_pause_requested = false;
    }
    m_control_cv.notify_all();
    LOG_INFO("Resume requested");
    return ok();
}

Result<void> ControlClient::cancel() {
    {
        std::lock_guard<std::mutex> lock(m_control_mutex);
        m_cancel_requested = true;
        m_pause_requested = false;
    }
    m_control_cv.notify_all();
    LOG_INFO("Cancel requested");
    return ok();
}

// ChunkHeader serialization
void ChunkHeader::serialize(uint8_t* buffer) const {
    // Big-endian encoding
    buffer[0] = (file_id >> 24) & 0xFF;
    buffer[1] = (file_id >> 16) & 0xFF;
    buffer[2] = (file_id >> 8) & 0xFF;
    buffer[3] = file_id & 0xFF;
    
    buffer[4] = (chunk_id >> 24) & 0xFF;
    buffer[5] = (chunk_id >> 16) & 0xFF;
    buffer[6] = (chunk_id >> 8) & 0xFF;
    buffer[7] = chunk_id & 0xFF;
    
    buffer[8] = (offset >> 24) & 0xFF;
    buffer[9] = (offset >> 16) & 0xFF;
    buffer[10] = (offset >> 8) & 0xFF;
    buffer[11] = offset & 0xFF;
    
    buffer[12] = (size >> 24) & 0xFF;
    buffer[13] = (size >> 16) & 0xFF;
    buffer[14] = (size >> 8) & 0xFF;
    buffer[15] = size & 0xFF;
}

ChunkHeader ChunkHeader::deserialize(const uint8_t* buffer) {
    ChunkHeader h;
    h.file_id = (static_cast<uint32_t>(buffer[0]) << 24) |
                (static_cast<uint32_t>(buffer[1]) << 16) |
                (static_cast<uint32_t>(buffer[2]) << 8) |
                static_cast<uint32_t>(buffer[3]);
    
    h.chunk_id = (static_cast<uint32_t>(buffer[4]) << 24) |
                 (static_cast<uint32_t>(buffer[5]) << 16) |
                 (static_cast<uint32_t>(buffer[6]) << 8) |
                 static_cast<uint32_t>(buffer[7]);
    
    h.offset = (static_cast<uint32_t>(buffer[8]) << 24) |
               (static_cast<uint32_t>(buffer[9]) << 16) |
               (static_cast<uint32_t>(buffer[10]) << 8) |
               static_cast<uint32_t>(buffer[11]);
    
    h.size = (static_cast<uint32_t>(buffer[12]) << 24) |
             (static_cast<uint32_t>(buffer[13]) << 16) |
             (static_cast<uint32_t>(buffer[14]) << 8) |
             static_cast<uint32_t>(buffer[15]);
    
    return h;
}

} // namespace teleport
