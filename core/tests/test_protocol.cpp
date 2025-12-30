/**
 * @file test_protocol.cpp
 * @brief Unit tests for control protocol
 */

#include <gtest/gtest.h>
#include "control/protocol.hpp"
#include <nlohmann/json.hpp>

using namespace teleport;
using json = nlohmann::json;

class ProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ============================================================================
// Message Type Conversion Tests
// ============================================================================

TEST_F(ProtocolTest, MessageTypeToString) {
    EXPECT_EQ(message_type_to_string(ControlMessageType::Handshake), "HANDSHAKE");
    EXPECT_EQ(message_type_to_string(ControlMessageType::HandshakeAck), "HANDSHAKE_ACK");
    EXPECT_EQ(message_type_to_string(ControlMessageType::FileList), "FILE_LIST");
    EXPECT_EQ(message_type_to_string(ControlMessageType::Accept), "ACCEPT");
    EXPECT_EQ(message_type_to_string(ControlMessageType::Reject), "REJECT");
    EXPECT_EQ(message_type_to_string(ControlMessageType::Progress), "PROGRESS");
    EXPECT_EQ(message_type_to_string(ControlMessageType::Complete), "COMPLETE");
    EXPECT_EQ(message_type_to_string(ControlMessageType::Error), "ERROR");
}

TEST_F(ProtocolTest, MessageTypeFromString) {
    EXPECT_EQ(message_type_from_string("HANDSHAKE"), ControlMessageType::Handshake);
    EXPECT_EQ(message_type_from_string("FILE_LIST"), ControlMessageType::FileList);
    EXPECT_EQ(message_type_from_string("ACCEPT"), ControlMessageType::Accept);
    EXPECT_EQ(message_type_from_string("ERROR"), ControlMessageType::Error);
    
    EXPECT_THROW(message_type_from_string("INVALID"), std::runtime_error);
}

// ============================================================================
// HandshakeMessage Tests
// ============================================================================

TEST_F(ProtocolTest, HandshakeMessageSerialization) {
    HandshakeMessage msg;
    msg.protocol_version = 1;
    msg.device_name = "Test Device";
    msg.device_os = "Windows";
    msg.session_token = "abc123";
    
    json j = msg.to_json();
    
    EXPECT_EQ(j["protocol_version"], 1);
    EXPECT_EQ(j["device"]["name"], "Test Device");
    EXPECT_EQ(j["device"]["os"], "Windows");
    EXPECT_EQ(j["session_token"], "abc123");
}

TEST_F(ProtocolTest, HandshakeMessageDeserialization) {
    json j = {
        {"protocol_version", 1},
        {"device", {
            {"name", "Remote Device"},
            {"os", "macOS"}
        }},
        {"session_token", "xyz789"}
    };
    
    auto msg = HandshakeMessage::from_json(j);
    
    EXPECT_EQ(msg.protocol_version, 1);
    EXPECT_EQ(msg.device_name, "Remote Device");
    EXPECT_EQ(msg.device_os, "macOS");
    EXPECT_EQ(msg.session_token, "xyz789");
}

// ============================================================================
// FileListMessage Tests
// ============================================================================

TEST_F(ProtocolTest, FileListMessageSerialization) {
    FileListMessage msg;
    msg.files.push_back({1, "file1.txt", 1024});
    msg.files.push_back({2, "file2.mp4", 1073741824});
    msg.total_size = 1073742848;
    
    json j = msg.to_json();
    
    EXPECT_EQ(j["total_size"], 1073742848);
    EXPECT_EQ(j["files"].size(), 2);
    EXPECT_EQ(j["files"][0]["id"], 1);
    EXPECT_EQ(j["files"][0]["name"], "file1.txt");
    EXPECT_EQ(j["files"][0]["size"], 1024);
}

TEST_F(ProtocolTest, FileListMessageDeserialization) {
    json j = {
        {"total_size", 2048},
        {"files", {
            {{"id", 1}, {"name", "a.txt"}, {"size", 1024}},
            {{"id", 2}, {"name", "b.txt"}, {"size", 1024}}
        }}
    };
    
    auto msg = FileListMessage::from_json(j);
    
    EXPECT_EQ(msg.files.size(), 2);
    EXPECT_EQ(msg.files[0].id, 1);
    EXPECT_EQ(msg.files[0].name, "a.txt");
    EXPECT_EQ(msg.files[1].id, 2);
    EXPECT_EQ(msg.total_size, 2048);
}

// ============================================================================
// ProgressMessage Tests
// ============================================================================

TEST_F(ProtocolTest, ProgressMessageSerialization) {
    ProgressMessage msg;
    msg.file_id = 1;
    msg.bytes_transferred = 500000;
    msg.bytes_total = 1000000;
    msg.speed_bps = 50000000.0;
    
    json j = msg.to_json();
    
    EXPECT_EQ(j["file_id"], 1);
    EXPECT_EQ(j["bytes_transferred"], 500000);
    EXPECT_EQ(j["bytes_total"], 1000000);
    EXPECT_DOUBLE_EQ(j["speed_bps"], 50000000.0);
}

// ============================================================================
// ControlMessage Wire Format Tests
// ============================================================================

TEST_F(ProtocolTest, ControlMessageSerialization) {
    HandshakeMessage handshake;
    handshake.protocol_version = 1;
    handshake.device_name = "Test";
    handshake.device_os = "Windows";
    
    ControlMessage msg = ControlMessage::handshake(handshake);
    
    auto data = msg.serialize();
    
    // Check length prefix (first 4 bytes, big-endian)
    EXPECT_GT(data.size(), 4);
    uint32_t len = (static_cast<uint32_t>(data[0]) << 24) |
                   (static_cast<uint32_t>(data[1]) << 16) |
                   (static_cast<uint32_t>(data[2]) << 8) |
                   static_cast<uint32_t>(data[3]);
    
    EXPECT_EQ(len, data.size() - 4);
}

TEST_F(ProtocolTest, ControlMessageDeserialization) {
    // Create a message
    HandshakeMessage handshake;
    handshake.protocol_version = 1;
    handshake.device_name = "Test Device";
    handshake.device_os = "Windows";
    handshake.session_token = "token123";
    
    ControlMessage original = ControlMessage::handshake(handshake);
    auto data = original.serialize();
    
    // Skip length prefix (4 bytes)
    auto deserialized = ControlMessage::deserialize(data.data() + 4, data.size() - 4);
    
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(deserialized->type, ControlMessageType::Handshake);
    
    auto recovered = HandshakeMessage::from_json(deserialized->payload);
    EXPECT_EQ(recovered.protocol_version, 1);
    EXPECT_EQ(recovered.device_name, "Test Device");
    EXPECT_EQ(recovered.device_os, "Windows");
}

TEST_F(ProtocolTest, ControlMessageInvalidJson) {
    uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    auto result = ControlMessage::deserialize(garbage, sizeof(garbage));
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// ChunkHeader Tests
// ============================================================================

TEST_F(ProtocolTest, ChunkHeaderSerialization) {
    ChunkHeader header;
    header.file_id = 0x12345678;
    header.chunk_id = 0x9ABCDEF0;
    header.offset = 0x11223344;
    header.size = 0x55667788;
    
    uint8_t buffer[ChunkHeader::HEADER_SIZE];
    header.serialize(buffer);
    
    // Verify big-endian encoding
    EXPECT_EQ(buffer[0], 0x12);
    EXPECT_EQ(buffer[1], 0x34);
    EXPECT_EQ(buffer[2], 0x56);
    EXPECT_EQ(buffer[3], 0x78);
    
    EXPECT_EQ(buffer[4], 0x9A);
    EXPECT_EQ(buffer[5], 0xBC);
    EXPECT_EQ(buffer[6], 0xDE);
    EXPECT_EQ(buffer[7], 0xF0);
}

TEST_F(ProtocolTest, ChunkHeaderDeserialization) {
    uint8_t buffer[ChunkHeader::HEADER_SIZE] = {
        0x12, 0x34, 0x56, 0x78,  // file_id
        0x9A, 0xBC, 0xDE, 0xF0,  // chunk_id
        0x11, 0x22, 0x33, 0x44,  // offset
        0x55, 0x66, 0x77, 0x88   // size
    };
    
    auto header = ChunkHeader::deserialize(buffer);
    
    EXPECT_EQ(header.file_id, 0x12345678);
    EXPECT_EQ(header.chunk_id, 0x9ABCDEF0);
    EXPECT_EQ(header.offset, 0x11223344);
    EXPECT_EQ(header.size, 0x55667788);
}

TEST_F(ProtocolTest, ChunkHeaderRoundTrip) {
    ChunkHeader original;
    original.file_id = 42;
    original.chunk_id = 100;
    original.offset = 2097152;  // 2MB
    original.size = 2097152;
    
    uint8_t buffer[ChunkHeader::HEADER_SIZE];
    original.serialize(buffer);
    
    auto recovered = ChunkHeader::deserialize(buffer);
    
    EXPECT_EQ(recovered.file_id, original.file_id);
    EXPECT_EQ(recovered.chunk_id, original.chunk_id);
    EXPECT_EQ(recovered.offset, original.offset);
    EXPECT_EQ(recovered.size, original.size);
}

// ============================================================================
// Error/Complete Message Tests
// ============================================================================

TEST_F(ProtocolTest, ErrorMessageSerialization) {
    ErrorMessage msg;
    msg.code = TELEPORT_ERROR_REJECTED;
    msg.message = "User declined transfer";
    msg.fatal = true;
    
    json j = msg.to_json();
    
    EXPECT_EQ(j["code"], TELEPORT_ERROR_REJECTED);
    EXPECT_EQ(j["message"], "User declined transfer");
    EXPECT_EQ(j["fatal"], true);
}

TEST_F(ProtocolTest, CompleteMessageSerialization) {
    CompleteMessage msg;
    msg.success = true;
    msg.message = "Transfer complete";
    msg.files_transferred = 5;
    msg.bytes_transferred = 10737418240;  // 10 GB
    
    json j = msg.to_json();
    
    EXPECT_EQ(j["success"], true);
    EXPECT_EQ(j["files_transferred"], 5);
    EXPECT_EQ(j["bytes_transferred"], 10737418240);
}

// ============================================================================
// JSON Schema Validation Tests
// ============================================================================

TEST_F(ProtocolTest, HandshakeValidation_MissingProtocolVersion) {
    json j = {
        {"device", {{"name", "Test"}, {"os", "Windows"}}}
    };
    EXPECT_THROW(HandshakeMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, HandshakeValidation_MissingDevice) {
    json j = {{"protocol_version", 1}};
    EXPECT_THROW(HandshakeMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, HandshakeValidation_WrongType) {
    json j = {
        {"protocol_version", "not_a_number"},
        {"device", {{"name", "Test"}, {"os", "Windows"}}}
    };
    EXPECT_THROW(HandshakeMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, FileListValidation_MissingFilesArray) {
    json j = {{"total_size", 1024}};
    EXPECT_THROW(FileListMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, FileListValidation_FilesNotArray) {
    json j = {
        {"files", "not_an_array"},
        {"total_size", 1024}
    };
    EXPECT_THROW(FileListMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, AcceptRejectValidation_MissingAccepted) {
    json j = {{"reason", "test"}};
    EXPECT_THROW(AcceptRejectMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, AcceptRejectValidation_WrongType) {
    json j = {{"accepted", "not_a_bool"}};
    EXPECT_THROW(AcceptRejectMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, AcceptRejectValidation_InvalidDataPort) {
    json j = {
        {"accepted", true},
        {"data_port", "not_a_number"}
    };
    EXPECT_THROW(AcceptRejectMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, ControlActionValidation_MissingAction) {
    json j = {{"file_id", 1}};
    EXPECT_THROW(ControlActionMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, ControlActionValidation_WrongType) {
    json j = {{"action", 123}};
    EXPECT_THROW(ControlActionMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, ProgressValidation_MissingFields) {
    json j = {{"file_id", 1}};  // missing bytes_transferred and bytes_total
    EXPECT_THROW(ProgressMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, ProgressValidation_WrongType) {
    json j = {
        {"file_id", "not_a_number"},
        {"bytes_transferred", 100},
        {"bytes_total", 1000}
    };
    EXPECT_THROW(ProgressMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, ResumeRequestValidation_MissingFileId) {
    json j = {{"received_bytes", 1024}};
    EXPECT_THROW(ResumeRequestMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, ResumeRequestValidation_ChunksNotArray) {
    json j = {
        {"file_id", 1},
        {"received_chunks", "not_an_array"}
    };
    EXPECT_THROW(ResumeRequestMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, ResumeRequestValidation_InvalidChunkId) {
    json j = {
        {"file_id", 1},
        {"received_chunks", json::array({1, 2, "not_a_number"})}
    };
    EXPECT_THROW(ResumeRequestMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, CompleteValidation_MissingSuccess) {
    json j = {{"message", "done"}};
    EXPECT_THROW(CompleteMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, CompleteValidation_WrongType) {
    json j = {{"success", "not_a_bool"}};
    EXPECT_THROW(CompleteMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, ErrorValidation_MissingCode) {
    json j = {{"message", "error"}};
    EXPECT_THROW(ErrorMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, ErrorValidation_WrongType) {
    json j = {{"code", "not_a_number"}};
    EXPECT_THROW(ErrorMessage::from_json(j), std::runtime_error);
}

TEST_F(ProtocolTest, ErrorValidation_InvalidFatal) {
    json j = {
        {"code", 1},
        {"fatal", "not_a_bool"}
    };
    EXPECT_THROW(ErrorMessage::from_json(j), std::runtime_error);
}

// ============================================================================
// Valid Deserialization Tests (ensure valid data still works)
// ============================================================================

TEST_F(ProtocolTest, AcceptRejectValidDeserialization) {
    json j = {
        {"accepted", true},
        {"reason", "OK"},
        {"data_port", 8080}
    };
    auto msg = AcceptRejectMessage::from_json(j);
    EXPECT_TRUE(msg.accepted);
    EXPECT_EQ(msg.reason, "OK");
    EXPECT_EQ(msg.data_port, 8080);
}

TEST_F(ProtocolTest, ErrorMessageValidDeserialization) {
    json j = {
        {"code", 500},
        {"message", "Internal error"},
        {"fatal", true}
    };
    auto msg = ErrorMessage::from_json(j);
    EXPECT_EQ(msg.code, 500);
    EXPECT_EQ(msg.message, "Internal error");
    EXPECT_TRUE(msg.fatal);
}
