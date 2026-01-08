/**
 * @file qr_pairing.hpp
 * @brief QR code-based device pairing for Teleport
 * 
 * Generates and validates QR codes containing connection information
 * for quick peer-to-peer pairing without network discovery.
 */

#ifndef TELEPORT_QR_PAIRING_HPP
#define TELEPORT_QR_PAIRING_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include "teleport/types.h"

namespace teleport {

/* ============================================================================
 * QR Pairing Types
 * ============================================================================ */

/**
 * @brief Pairing information encoded in QR code
 */
struct QrPairingInfo {
    int version = 1;                // Protocol version
    std::string ip;                 // IP address of the host
    uint16_t port = 0;              // Control port
    std::string session_token;      // Unique session token (32 chars hex)
    std::string device_name;        // Host device name
    int64_t expires_at_ms = 0;      // Expiry timestamp (Unix ms)
    
    /**
     * @brief Check if the pairing info has expired
     */
    bool is_expired() const {
        return now_ms() > expires_at_ms;
    }
    
    /**
     * @brief Get seconds until expiry
     */
    int seconds_until_expiry() const {
        int64_t remaining = expires_at_ms - now_ms();
        return remaining > 0 ? static_cast<int>(remaining / 1000) : 0;
    }
};

/**
 * @brief Validation result for QR pairing
 */
enum class QrValidationResult {
    Valid,              // QR data is valid and not expired
    InvalidFormat,      // QR data is malformed
    InvalidVersion,     // Unsupported protocol version
    Expired,            // Session has expired
    InvalidToken,       // Token format is invalid
    MissingFields       // Required fields are missing
};

/* ============================================================================
 * QR Pairing Functions
 * ============================================================================ */

/**
 * @brief Generate pairing information for QR code
 * @param ip Local IP address to advertise
 * @param port Control port
 * @param device_name Device name to display
 * @param expiry_seconds Seconds until expiry (default: 5 minutes)
 * @return QrPairingInfo structure
 */
QrPairingInfo generate_pairing_info(
    const std::string& ip,
    uint16_t port,
    const std::string& device_name,
    int expiry_seconds = 300
);

/**
 * @brief Encode pairing info to JSON string for QR code
 * @param info Pairing information
 * @return JSON string suitable for QR encoding
 */
std::string encode_pairing_to_json(const QrPairingInfo& info);

/**
 * @brief Decode pairing info from JSON string
 * @param json_data JSON string from scanned QR code
 * @return Result containing QrPairingInfo or error
 */
Result<QrPairingInfo> decode_pairing_from_json(const std::string& json_data);

/**
 * @brief Validate pairing information
 * @param info Pairing info to validate
 * @return Validation result
 */
QrValidationResult validate_pairing_info(const QrPairingInfo& info);

/**
 * @brief Get human-readable error for validation result
 */
std::string validation_result_to_string(QrValidationResult result);

/**
 * @brief Generate a secure random session token
 * @return 32-character hex string
 */
std::string generate_session_token();

/**
 * @brief Validate session token format
 * @param token Token to validate
 * @return true if token is valid format (32 hex chars)
 */
bool is_valid_token_format(const std::string& token);

/* ============================================================================
 * QR Code Generation
 * ============================================================================ */

/**
 * @brief QR code error correction level
 */
enum class QrErrorCorrection {
    Low,        // ~7% recovery
    Medium,     // ~15% recovery
    Quartile,   // ~25% recovery
    High        // ~30% recovery
};

/**
 * @brief Generated QR code bitmap
 */
struct QrBitmap {
    std::vector<uint8_t> data;      // 1 bit per module (1=black, 0=white)
    int size = 0;                    // Width/height in modules
    
    /**
     * @brief Get module value at position
     */
    bool get(int x, int y) const {
        if (x < 0 || x >= size || y < 0 || y >= size) return false;
        int byte_idx = (y * size + x) / 8;
        int bit_idx = (y * size + x) % 8;
        return (data[byte_idx] & (1 << bit_idx)) != 0;
    }
    
    /**
     * @brief Convert to image data (RGBA, 4 bytes per pixel)
     * @param module_size Pixels per QR module
     * @param quiet_zone Number of quiet zone modules (default: 4)
     * @return RGBA pixel data
     */
    std::vector<uint8_t> to_rgba(int module_size = 8, int quiet_zone = 4) const;
};

/**
 * @brief Generate QR code bitmap from text
 * @param text Text to encode
 * @param error_correction Error correction level
 * @return QR code bitmap
 */
Result<QrBitmap> generate_qr_code(
    const std::string& text,
    QrErrorCorrection error_correction = QrErrorCorrection::Medium
);

/**
 * @brief Generate QR code as PNG data
 * @param text Text to encode
 * @param module_size Pixels per QR module
 * @return PNG file data
 */
Result<std::vector<uint8_t>> generate_qr_png(
    const std::string& text,
    int module_size = 8
);

/**
 * @brief Generate complete QR code for pairing
 * @param info Pairing information
 * @param module_size Pixels per QR module
 * @return PNG file data ready for display
 */
Result<std::vector<uint8_t>> generate_pairing_qr(
    const QrPairingInfo& info,
    int module_size = 8
);

} // namespace teleport

#endif // TELEPORT_QR_PAIRING_HPP
