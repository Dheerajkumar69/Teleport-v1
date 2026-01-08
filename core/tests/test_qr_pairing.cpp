/**
 * @file test_qr_pairing.cpp
 * @brief Unit tests for QR pairing functionality
 */

#include <gtest/gtest.h>
#include "pairing/qr_pairing.hpp"
#include "platform/pal.hpp"

using namespace teleport;

class QrPairingTest : public ::testing::Test {
protected:
    void SetUp() override {
        pal::platform_init();
    }
    
    void TearDown() override {
        pal::platform_cleanup();
    }
};

// ============================================================================
// Token Generation Tests
// ============================================================================

TEST_F(QrPairingTest, TokenLength) {
    std::string token = generate_session_token();
    EXPECT_EQ(token.length(), 32);
}

TEST_F(QrPairingTest, TokenHexFormat) {
    std::string token = generate_session_token();
    EXPECT_TRUE(is_valid_token_format(token));
    
    // Test all characters are hex
    for (char c : token) {
        bool isHex = (c >= '0' && c <= '9') || 
                     (c >= 'a' && c <= 'f') || 
                     (c >= 'A' && c <= 'F');
        EXPECT_TRUE(isHex) << "Invalid hex character: " << c;
    }
}

TEST_F(QrPairingTest, TokenUniqueness) {
    std::set<std::string> tokens;
    for (int i = 0; i < 100; ++i) {
        std::string token = generate_session_token();
        EXPECT_EQ(tokens.count(token), 0) << "Duplicate token generated";
        tokens.insert(token);
    }
}

TEST_F(QrPairingTest, InvalidTokenFormat) {
    EXPECT_FALSE(is_valid_token_format(""));  // Empty
    EXPECT_FALSE(is_valid_token_format("abc"));  // Too short
    EXPECT_FALSE(is_valid_token_format("0123456789abcdef0123456789abcdefXY"));  // Invalid chars
    EXPECT_FALSE(is_valid_token_format("0123456789abcdef0123456789abcdef00"));  // Too long
}

// ============================================================================
// Pairing Info Tests
// ============================================================================

TEST_F(QrPairingTest, GeneratePairingInfo) {
    auto info = generate_pairing_info("192.168.1.100", 45455, "TestDevice", 300);
    
    EXPECT_EQ(info.version, 1);
    EXPECT_EQ(info.ip, "192.168.1.100");
    EXPECT_EQ(info.port, 45455);
    EXPECT_EQ(info.device_name, "TestDevice");
    EXPECT_EQ(info.session_token.length(), 32);
    EXPECT_GT(info.expires_at_ms, now_ms());
    EXPECT_FALSE(info.is_expired());
}

TEST_F(QrPairingTest, PairingInfoExpiry) {
    auto info = generate_pairing_info("192.168.1.100", 45455, "TestDevice", 1);  // 1 second expiry
    
    EXPECT_FALSE(info.is_expired());
    EXPECT_GT(info.seconds_until_expiry(), 0);
    
    // Wait for expiry
    pal::sleep_ms(1500);
    
    EXPECT_TRUE(info.is_expired());
    EXPECT_EQ(info.seconds_until_expiry(), 0);
}

// ============================================================================
// JSON Encoding/Decoding Tests
// ============================================================================

TEST_F(QrPairingTest, JsonRoundTrip) {
    auto original = generate_pairing_info("10.0.0.1", 8080, "My Phone", 600);
    
    std::string json = encode_pairing_to_json(original);
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"v\":1"), std::string::npos);
    EXPECT_NE(json.find("\"ip\":\"10.0.0.1\""), std::string::npos);
    
    auto decoded = decode_pairing_from_json(json);
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    
    EXPECT_EQ(decoded->version, original.version);
    EXPECT_EQ(decoded->ip, original.ip);
    EXPECT_EQ(decoded->port, original.port);
    EXPECT_EQ(decoded->device_name, original.device_name);
    EXPECT_EQ(decoded->session_token, original.session_token);
    EXPECT_EQ(decoded->expires_at_ms, original.expires_at_ms);
}

TEST_F(QrPairingTest, DecodeInvalidJson) {
    auto result = decode_pairing_from_json("not valid json");
    EXPECT_FALSE(result.ok());
}

TEST_F(QrPairingTest, DecodeMissingFields) {
    auto result = decode_pairing_from_json(R"({"v":1})");  // Missing IP, port, etc.
    EXPECT_FALSE(result.ok());
}

TEST_F(QrPairingTest, DecodeInvalidVersion) {
    auto result = decode_pairing_from_json(R"({"v":99,"ip":"1.2.3.4","port":1234,"token":"aaaabbbbccccddddeeeeffffgggghhhh","exp":9999999999999})");
    EXPECT_FALSE(result.ok());
}

// ============================================================================
// Validation Tests
// ============================================================================

TEST_F(QrPairingTest, ValidationValid) {
    auto info = generate_pairing_info("192.168.1.1", 45455, "Test", 300);
    EXPECT_EQ(validate_pairing_info(info), QrValidationResult::Valid);
}

TEST_F(QrPairingTest, ValidationExpired) {
    QrPairingInfo info;
    info.version = 1;
    info.ip = "192.168.1.1";
    info.port = 45455;
    info.session_token = generate_session_token();
    info.expires_at_ms = now_ms() - 1000;  // Already expired
    
    EXPECT_EQ(validate_pairing_info(info), QrValidationResult::Expired);
}

TEST_F(QrPairingTest, ValidationInvalidToken) {
    QrPairingInfo info;
    info.version = 1;
    info.ip = "192.168.1.1";
    info.port = 45455;
    info.session_token = "invalid";
    info.expires_at_ms = now_ms() + 300000;
    
    EXPECT_EQ(validate_pairing_info(info), QrValidationResult::InvalidToken);
}

TEST_F(QrPairingTest, ValidationMissingFields) {
    QrPairingInfo info;
    info.version = 1;
    info.ip = "";  // Missing IP
    info.port = 45455;
    info.session_token = generate_session_token();
    info.expires_at_ms = now_ms() + 300000;
    
    EXPECT_EQ(validate_pairing_info(info), QrValidationResult::MissingFields);
}

// ============================================================================
// QR Code Generation Tests
// ============================================================================

TEST_F(QrPairingTest, GenerateQrBitmap) {
    auto result = generate_qr_code("Hello, World!", QrErrorCorrection::Medium);
    ASSERT_TRUE(result.ok()) << result.error().message;
    
    EXPECT_GT(result->size, 0);
    EXPECT_FALSE(result->data.empty());
    
    // QR code should be at least 21x21 for version 1
    EXPECT_GE(result->size, 21);
}

TEST_F(QrPairingTest, GenerateQrPng) {
    auto result = generate_qr_png("Test data", 8);
    ASSERT_TRUE(result.ok()) << result.error().message;
    
    // Should produce valid BMP file (we use BMP format)
    ASSERT_GE(result->size(), 54);  // Minimum BMP header size
    EXPECT_EQ((*result)[0], 'B');
    EXPECT_EQ((*result)[1], 'M');
}

TEST_F(QrPairingTest, GeneratePairingQr) {
    auto info = generate_pairing_info("192.168.1.1", 45455, "Test", 300);
    auto result = generate_pairing_qr(info, 8);
    
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_GT(result->size(), 0);
}

TEST_F(QrPairingTest, QrBitmapToRgba) {
    auto qr = generate_qr_code("Test", QrErrorCorrection::Low);
    ASSERT_TRUE(qr.ok());
    
    auto rgba = qr->to_rgba(4, 2);  // 4 pixels per module, 2 quiet zone modules
    
    int expected_size = (qr->size + 2 * 2) * 4;  // (size + 2*quiet_zone) * module_size
    EXPECT_EQ(rgba.size(), expected_size * expected_size * 4);  // RGBA = 4 bytes per pixel
}
