/**
 * @file protocol.hpp
 * @brief Control channel protocol definitions
 */

#ifndef TELEPORT_PROTOCOL_HPP
#define TELEPORT_PROTOCOL_HPP

#include "teleport/teleport.h"
#include "teleport/types.h"
#include "teleport/errors.h"
#include "platform/pal.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <variant>

namespace teleport {

using json = nlohmann::json;

/* ============================================================================
 * Message Types
 * ============================================================================ */

/**
 * @brief Control message enumeration
 */
enum class ControlMessageType {
    // Handshake
    Handshake,
    HandshakeAck,
    
    // File negotiation
    FileList,
    Accept,
    Reject,
    
    // Transfer control
    Start,
    Pause,
    Resume,
    Cancel,
    
    // Status
    Progress,
    ResumeRequest,
    Complete,
    Error
};

std::string message_type_to_string(ControlMessageType type);
ControlMessageType message_type_from_string(const std::string& s);

/* ============================================================================
 * Message Structures
 * ============================================================================ */

/**
 * @brief Handshake message
 */
struct HandshakeMessage {
    int protocol_version = TELEPORT_PROTOCOL_VERSION;
    std::string device_name;
    std::string device_os;
    std::string session_token;  // For receiver to validate
    
    json to_json() const;
    static HandshakeMessage from_json(const json& j);
};

/**
 * @brief File list message
 */
struct FileListMessage {
    struct FileEntry {
        uint32_t id;
        std::string name;
        uint64_t size;
    };
    
    std::vector<FileEntry> files;
    uint64_t total_size = 0;
    
    json to_json() const;
    static FileListMessage from_json(const json& j);
};

/**
 * @brief Accept/Reject response
 */
struct AcceptRejectMessage {
    bool accepted;
    std::string reason;  // If rejected
    uint16_t data_port;  // Port for data transfer (if accepted)
    
    json to_json() const;
    static AcceptRejectMessage from_json(const json& j);
};

/**
 * @brief Transfer control action
 */
struct ControlActionMessage {
    std::string action;  // "pause", "resume", "cancel"
    uint32_t file_id = 0;  // 0 means all files
    
    json to_json() const;
    static ControlActionMessage from_json(const json& j);
};

/**
 * @brief Progress update
 */
struct ProgressMessage {
    uint32_t file_id;
    uint64_t bytes_transferred;
    uint64_t bytes_total;
    double speed_bps;
    
    json to_json() const;
    static ProgressMessage from_json(const json& j);
};

/**
 * @brief Resume request (receiver -> sender)
 */
struct ResumeRequestMessage {
    uint32_t file_id;
    std::vector<uint32_t> received_chunks;  // Chunks already received
    uint64_t received_bytes;
    
    json to_json() const;
    static ResumeRequestMessage from_json(const json& j);
};

/**
 * @brief Completion message
 */
struct CompleteMessage {
    bool success;
    std::string message;
    uint32_t files_transferred;
    uint64_t bytes_transferred;
    
    json to_json() const;
    static CompleteMessage from_json(const json& j);
};

/**
 * @brief Error message
 */
struct ErrorMessage {
    int code;
    std::string message;
    bool fatal;  // If true, connection will be closed
    
    json to_json() const;
    static ErrorMessage from_json(const json& j);
};

/* ============================================================================
 * Wire Format
 * ============================================================================ */

/**
 * @brief Framed message (type + payload)
 */
struct ControlMessage {
    ControlMessageType type;
    json payload;
    
    /**
     * @brief Serialize to wire format (length-prefixed JSON)
     */
    std::vector<uint8_t> serialize() const;
    
    /**
     * @brief Deserialize from wire format
     * @param data Raw bytes (without length prefix)
     */
    static std::optional<ControlMessage> deserialize(const uint8_t* data, size_t len);
    
    // Convenience constructors
    static ControlMessage handshake(const HandshakeMessage& msg);
    static ControlMessage handshake_ack(const HandshakeMessage& msg);
    static ControlMessage file_list(const FileListMessage& msg);
    static ControlMessage accept(const AcceptRejectMessage& msg);
    static ControlMessage reject(const AcceptRejectMessage& msg);
    static ControlMessage control(const ControlActionMessage& msg);
    static ControlMessage progress(const ProgressMessage& msg);
    static ControlMessage resume_request(const ResumeRequestMessage& msg);
    static ControlMessage complete(const CompleteMessage& msg);
    static ControlMessage error(const ErrorMessage& msg);
};

/* ============================================================================
 * Message I/O Helpers
 * ============================================================================ */

/**
 * @brief Read a length-prefixed message from socket
 */
class MessageReader {
public:
    explicit MessageReader(pal::TcpSocket& socket) : m_socket(socket) {}
    
    /**
     * @brief Read next control message
     * @return Message or error
     */
    Result<ControlMessage> read();
    
private:
    pal::TcpSocket& m_socket;
};

/**
 * @brief Write length-prefixed messages to socket
 */
class MessageWriter {
public:
    explicit MessageWriter(pal::TcpSocket& socket) : m_socket(socket) {}
    
    /**
     * @brief Write a control message
     */
    Result<void> write(const ControlMessage& msg);
    
private:
    pal::TcpSocket& m_socket;
};

} // namespace teleport

#endif // TELEPORT_PROTOCOL_HPP
