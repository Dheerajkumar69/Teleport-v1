/**
 * @file protocol.cpp
 * @brief Control protocol implementation
 */

#include "protocol.hpp"
#include "utils/logger.hpp"
#include <cstring>

namespace teleport {

/* ============================================================================
 * Message Type Conversion
 * ============================================================================ */

std::string message_type_to_string(ControlMessageType type) {
    switch (type) {
        case ControlMessageType::Handshake: return "HANDSHAKE";
        case ControlMessageType::HandshakeAck: return "HANDSHAKE_ACK";
        case ControlMessageType::FileList: return "FILE_LIST";
        case ControlMessageType::Accept: return "ACCEPT";
        case ControlMessageType::Reject: return "REJECT";
        case ControlMessageType::Start: return "START";
        case ControlMessageType::Pause: return "PAUSE";
        case ControlMessageType::Resume: return "RESUME";
        case ControlMessageType::Cancel: return "CANCEL";
        case ControlMessageType::Progress: return "PROGRESS";
        case ControlMessageType::ResumeRequest: return "RESUME_REQUEST";
        case ControlMessageType::Complete: return "COMPLETE";
        case ControlMessageType::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

ControlMessageType message_type_from_string(const std::string& s) {
    if (s == "HANDSHAKE") return ControlMessageType::Handshake;
    if (s == "HANDSHAKE_ACK") return ControlMessageType::HandshakeAck;
    if (s == "FILE_LIST") return ControlMessageType::FileList;
    if (s == "ACCEPT") return ControlMessageType::Accept;
    if (s == "REJECT") return ControlMessageType::Reject;
    if (s == "START") return ControlMessageType::Start;
    if (s == "PAUSE") return ControlMessageType::Pause;
    if (s == "RESUME") return ControlMessageType::Resume;
    if (s == "CANCEL") return ControlMessageType::Cancel;
    if (s == "PROGRESS") return ControlMessageType::Progress;
    if (s == "RESUME_REQUEST") return ControlMessageType::ResumeRequest;
    if (s == "COMPLETE") return ControlMessageType::Complete;
    if (s == "ERROR") return ControlMessageType::Error;
    throw std::runtime_error("Unknown message type: " + s);
}

/* ============================================================================
 * HandshakeMessage
 * ============================================================================ */

json HandshakeMessage::to_json() const {
    return json{
        {"protocol_version", protocol_version},
        {"device", {
            {"name", device_name},
            {"os", device_os}
        }},
        {"session_token", session_token}
    };
}

HandshakeMessage HandshakeMessage::from_json(const json& j) {
    HandshakeMessage msg;
    
    // Validate required fields exist and are correct types
    if (!j.contains("protocol_version") || !j["protocol_version"].is_number_integer()) {
        throw std::runtime_error("Handshake missing or invalid 'protocol_version'");
    }
    if (!j.contains("device") || !j["device"].is_object()) {
        throw std::runtime_error("Handshake missing or invalid 'device' object");
    }
    if (!j["device"].contains("name") || !j["device"]["name"].is_string()) {
        throw std::runtime_error("Handshake missing 'device.name'");
    }
    if (!j["device"].contains("os") || !j["device"]["os"].is_string()) {
        throw std::runtime_error("Handshake missing 'device.os'");
    }
    
    msg.protocol_version = j.at("protocol_version").get<int>();
    msg.device_name = j.at("device").at("name").get<std::string>();
    msg.device_os = j.at("device").at("os").get<std::string>();
    msg.session_token = j.value("session_token", "");
    
    // Validate device name length
    if (msg.device_name.size() > 256) {
        msg.device_name = msg.device_name.substr(0, 256);
    }
    
    return msg;
}

/* ============================================================================
 * FileListMessage
 * ============================================================================ */

json FileListMessage::to_json() const {
    json j;
    json files_arr = json::array();
    
    for (const auto& f : files) {
        files_arr.push_back({
            {"id", f.id},
            {"name", f.name},
            {"size", f.size}
        });
    }
    
    j["files"] = files_arr;
    j["total_size"] = total_size;
    return j;
}

FileListMessage FileListMessage::from_json(const json& j) {
    FileListMessage msg;
    
    // Validate structure
    if (!j.contains("files") || !j["files"].is_array()) {
        throw std::runtime_error("FileList missing or invalid 'files' array");
    }
    
    msg.total_size = j.value("total_size", 0ULL);
    
    // Limit number of files to prevent DoS
    constexpr size_t MAX_FILES = 10000;
    size_t file_count = 0;
    
    for (const auto& f : j.at("files")) {
        if (++file_count > MAX_FILES) {
            throw std::runtime_error("FileList exceeds maximum file count");
        }
        
        // Validate each file entry
        if (!f.contains("id") || !f.contains("name") || !f.contains("size")) {
            throw std::runtime_error("FileList entry missing required fields");
        }
        
        FileListMessage::FileEntry entry;
        entry.id = f.at("id").get<uint32_t>();
        entry.name = f.at("name").get<std::string>();
        entry.size = f.at("size").get<uint64_t>();
        
        // Validate filename length
        if (entry.name.size() > 1024) {
            throw std::runtime_error("Filename too long in FileList");
        }
        
        msg.files.push_back(entry);
    }
    
    return msg;
}

/* ============================================================================
 * AcceptRejectMessage
 * ============================================================================ */

json AcceptRejectMessage::to_json() const {
    return json{
        {"accepted", accepted},
        {"reason", reason},
        {"data_port", data_port}
    };
}

AcceptRejectMessage AcceptRejectMessage::from_json(const json& j) {
    AcceptRejectMessage msg;
    
    // Validate required field exists and is correct type
    if (!j.contains("accepted") || !j["accepted"].is_boolean()) {
        throw std::runtime_error("AcceptReject missing or invalid 'accepted' field");
    }
    
    // Validate optional fields if present
    if (j.contains("data_port") && !j["data_port"].is_number()) {
        throw std::runtime_error("AcceptReject 'data_port' must be a number");
    }
    
    msg.accepted = j.at("accepted").get<bool>();
    msg.reason = j.value("reason", "");
    msg.data_port = j.value("data_port", 0);
    return msg;
}

/* ============================================================================
 * ControlActionMessage
 * ============================================================================ */

json ControlActionMessage::to_json() const {
    return json{
        {"action", action},
        {"file_id", file_id}
    };
}

ControlActionMessage ControlActionMessage::from_json(const json& j) {
    ControlActionMessage msg;
    
    // Validate required field exists and is correct type
    if (!j.contains("action") || !j["action"].is_string()) {
        throw std::runtime_error("ControlAction missing or invalid 'action' field");
    }
    
    // Validate optional fields if present
    if (j.contains("file_id") && !j["file_id"].is_number()) {
        throw std::runtime_error("ControlAction 'file_id' must be a number");
    }
    
    msg.action = j.at("action").get<std::string>();
    msg.file_id = j.value("file_id", 0);
    return msg;
}

/* ============================================================================
 * ProgressMessage
 * ============================================================================ */

json ProgressMessage::to_json() const {
    return json{
        {"file_id", file_id},
        {"bytes_transferred", bytes_transferred},
        {"bytes_total", bytes_total},
        {"speed_bps", speed_bps}
    };
}

ProgressMessage ProgressMessage::from_json(const json& j) {
    ProgressMessage msg;
    
    // Validate required fields exist and are correct types
    if (!j.contains("file_id") || !j["file_id"].is_number()) {
        throw std::runtime_error("Progress missing or invalid 'file_id' field");
    }
    if (!j.contains("bytes_transferred") || !j["bytes_transferred"].is_number()) {
        throw std::runtime_error("Progress missing or invalid 'bytes_transferred' field");
    }
    if (!j.contains("bytes_total") || !j["bytes_total"].is_number()) {
        throw std::runtime_error("Progress missing or invalid 'bytes_total' field");
    }
    
    msg.file_id = j.at("file_id").get<uint32_t>();
    msg.bytes_transferred = j.at("bytes_transferred").get<uint64_t>();
    msg.bytes_total = j.at("bytes_total").get<uint64_t>();
    msg.speed_bps = j.value("speed_bps", 0.0);
    return msg;
}

/* ============================================================================
 * ResumeRequestMessage
 * ============================================================================ */

json ResumeRequestMessage::to_json() const {
    return json{
        {"file_id", file_id},
        {"received_chunks", received_chunks},
        {"received_bytes", received_bytes}
    };
}

ResumeRequestMessage ResumeRequestMessage::from_json(const json& j) {
    ResumeRequestMessage msg;
    
    // Validate required field exists and is correct type
    if (!j.contains("file_id") || !j["file_id"].is_number()) {
        throw std::runtime_error("ResumeRequest missing or invalid 'file_id' field");
    }
    
    // Validate optional array field if present
    if (j.contains("received_chunks") && !j["received_chunks"].is_array()) {
        throw std::runtime_error("ResumeRequest 'received_chunks' must be an array");
    }
    
    msg.file_id = j.at("file_id").get<uint32_t>();
    msg.received_bytes = j.value("received_bytes", 0ULL);
    
    if (j.contains("received_chunks")) {
        for (const auto& c : j["received_chunks"]) {
            if (!c.is_number()) {
                throw std::runtime_error("ResumeRequest chunk IDs must be numbers");
            }
            msg.received_chunks.push_back(c.get<uint32_t>());
        }
    }
    
    return msg;
}

/* ============================================================================
 * CompleteMessage
 * ============================================================================ */

json CompleteMessage::to_json() const {
    return json{
        {"success", success},
        {"message", message},
        {"files_transferred", files_transferred},
        {"bytes_transferred", bytes_transferred}
    };
}

CompleteMessage CompleteMessage::from_json(const json& j) {
    CompleteMessage msg;
    
    // Validate required field exists and is correct type
    if (!j.contains("success") || !j["success"].is_boolean()) {
        throw std::runtime_error("Complete missing or invalid 'success' field");
    }
    
    // Validate optional numeric fields if present
    if (j.contains("files_transferred") && !j["files_transferred"].is_number()) {
        throw std::runtime_error("Complete 'files_transferred' must be a number");
    }
    if (j.contains("bytes_transferred") && !j["bytes_transferred"].is_number()) {
        throw std::runtime_error("Complete 'bytes_transferred' must be a number");
    }
    
    msg.success = j.at("success").get<bool>();
    msg.message = j.value("message", "");
    msg.files_transferred = j.value("files_transferred", 0);
    msg.bytes_transferred = j.value("bytes_transferred", 0ULL);
    return msg;
}

/* ============================================================================
 * ErrorMessage
 * ============================================================================ */

json ErrorMessage::to_json() const {
    return json{
        {"code", code},
        {"message", message},
        {"fatal", fatal}
    };
}

ErrorMessage ErrorMessage::from_json(const json& j) {
    ErrorMessage msg;
    
    // Validate required field exists and is correct type
    if (!j.contains("code") || !j["code"].is_number()) {
        throw std::runtime_error("Error missing or invalid 'code' field");
    }
    
    // Validate optional boolean field if present
    if (j.contains("fatal") && !j["fatal"].is_boolean()) {
        throw std::runtime_error("Error 'fatal' must be a boolean");
    }
    
    msg.code = j.at("code").get<int>();
    msg.message = j.value("message", "");
    msg.fatal = j.value("fatal", false);
    return msg;
}

/* ============================================================================
 * ControlMessage
 * ============================================================================ */

std::vector<uint8_t> ControlMessage::serialize() const {
    json envelope;
    envelope["type"] = message_type_to_string(type);
    envelope["payload"] = payload;
    
    std::string json_str = envelope.dump();
    
    // Length prefix (4 bytes, big-endian)
    uint32_t len = static_cast<uint32_t>(json_str.size());
    std::vector<uint8_t> data(4 + len);
    
    data[0] = (len >> 24) & 0xFF;
    data[1] = (len >> 16) & 0xFF;
    data[2] = (len >> 8) & 0xFF;
    data[3] = len & 0xFF;
    
    std::memcpy(data.data() + 4, json_str.data(), len);
    
    return data;
}

std::optional<ControlMessage> ControlMessage::deserialize(const uint8_t* data, size_t len) {
    try {
        std::string json_str(reinterpret_cast<const char*>(data), len);
        json envelope = json::parse(json_str);
        
        ControlMessage msg;
        msg.type = message_type_from_string(envelope.at("type").get<std::string>());
        msg.payload = envelope.at("payload");
        
        return msg;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to deserialize message: ", e.what());
        return std::nullopt;
    }
}

// Convenience constructors
ControlMessage ControlMessage::handshake(const HandshakeMessage& msg) {
    return {ControlMessageType::Handshake, msg.to_json()};
}

ControlMessage ControlMessage::handshake_ack(const HandshakeMessage& msg) {
    return {ControlMessageType::HandshakeAck, msg.to_json()};
}

ControlMessage ControlMessage::file_list(const FileListMessage& msg) {
    return {ControlMessageType::FileList, msg.to_json()};
}

ControlMessage ControlMessage::accept(const AcceptRejectMessage& msg) {
    return {ControlMessageType::Accept, msg.to_json()};
}

ControlMessage ControlMessage::reject(const AcceptRejectMessage& msg) {
    return {ControlMessageType::Reject, msg.to_json()};
}

ControlMessage ControlMessage::control(const ControlActionMessage& msg) {
    return {ControlMessageType::Pause, msg.to_json()};  // Type determined by action
}

ControlMessage ControlMessage::progress(const ProgressMessage& msg) {
    return {ControlMessageType::Progress, msg.to_json()};
}

ControlMessage ControlMessage::resume_request(const ResumeRequestMessage& msg) {
    return {ControlMessageType::ResumeRequest, msg.to_json()};
}

ControlMessage ControlMessage::complete(const CompleteMessage& msg) {
    return {ControlMessageType::Complete, msg.to_json()};
}

ControlMessage ControlMessage::error(const ErrorMessage& msg) {
    return {ControlMessageType::Error, msg.to_json()};
}

/* ============================================================================
 * MessageReader / MessageWriter
 * ============================================================================ */

Result<ControlMessage> MessageReader::read() {
    // Read length prefix (4 bytes)
    uint8_t len_buf[4];
    auto result = m_socket.recv_all(len_buf, 4);
    if (!result) {
        return result.error();
    }
    
    uint32_t len = (static_cast<uint32_t>(len_buf[0]) << 24) |
                   (static_cast<uint32_t>(len_buf[1]) << 16) |
                   (static_cast<uint32_t>(len_buf[2]) << 8) |
                   static_cast<uint32_t>(len_buf[3]);
    
    if (len == 0 || len > 1024 * 1024) {  // Max 1MB message
        return make_error(TELEPORT_ERROR_PROTOCOL, "Invalid message length");
    }
    
    // Read message body
    std::vector<uint8_t> body(len);
    result = m_socket.recv_all(body.data(), len);
    if (!result) {
        return result.error();
    }
    
    auto msg = ControlMessage::deserialize(body.data(), len);
    if (!msg) {
        return make_error(TELEPORT_ERROR_PROTOCOL, "Failed to parse message");
    }
    
    LOG_DEBUG("Received: ", message_type_to_string(msg->type));
    return *msg;
}

Result<void> MessageWriter::write(const ControlMessage& msg) {
    auto data = msg.serialize();
    auto result = m_socket.send_all(data.data(), data.size());
    if (!result) {
        return result.error();
    }
    
    LOG_DEBUG("Sent: ", message_type_to_string(msg.type));
    return ok();
}

} // namespace teleport
