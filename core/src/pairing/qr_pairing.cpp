/**
 * @file qr_pairing.cpp
 * @brief QR code pairing implementation
 */

#include "qr_pairing.hpp"
#include "../utils/logger.hpp"
#include "../control/protocol.hpp"

#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Include qrcodegen - MIT licensed header-only QR generator
// Source: https://github.com/nayuki/QR-Code-generator
#include "third_party/qrcodegen.hpp"

using json = nlohmann::json;
using namespace qrcodegen;

namespace teleport {

/* ============================================================================
 * Token Generation
 * ============================================================================ */

std::string generate_session_token() {
    // Generate 16 random bytes = 32 hex characters
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    
    uint64_t v1 = dist(gen);
    uint64_t v2 = dist(gen);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << v1
        << std::setw(16) << v2;
    
    return oss.str();
}

bool is_valid_token_format(const std::string& token) {
    if (token.length() != 32) return false;
    return std::all_of(token.begin(), token.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

/* ============================================================================
 * Pairing Info Generation
 * ============================================================================ */

QrPairingInfo generate_pairing_info(
    const std::string& ip,
    uint16_t port,
    const std::string& device_name,
    int expiry_seconds
) {
    QrPairingInfo info;
    info.version = 1;
    info.ip = ip;
    info.port = port;
    info.device_name = device_name;
    info.session_token = generate_session_token();
    info.expires_at_ms = now_ms() + (expiry_seconds * 1000LL);
    
    LOG_INFO("Generated pairing info: ", ip, ":", port, " expires in ", expiry_seconds, "s");
    return info;
}

/* ============================================================================
 * JSON Encoding/Decoding
 * ============================================================================ */

std::string encode_pairing_to_json(const QrPairingInfo& info) {
    json j;
    j["v"] = info.version;
    j["ip"] = info.ip;
    j["port"] = info.port;
    j["token"] = info.session_token;
    j["name"] = info.device_name;
    j["exp"] = info.expires_at_ms;
    
    return j.dump();
}

Result<QrPairingInfo> decode_pairing_from_json(const std::string& json_data) {
    try {
        json j = json::parse(json_data);
        
        QrPairingInfo info;
        
        // Version check
        if (!j.contains("v") || !j["v"].is_number_integer()) {
            return Error{TELEPORT_ERROR_INVALID_ARGUMENT, "Missing or invalid version"};
        }
        info.version = j["v"].get<int>();
        
        if (info.version != 1) {
            return Error{TELEPORT_ERROR_INVALID_ARGUMENT, 
                "Unsupported QR version: " + std::to_string(info.version)};
        }
        
        // Required fields
        if (!j.contains("ip") || !j["ip"].is_string()) {
            return Error{TELEPORT_ERROR_INVALID_ARGUMENT, "Missing IP address"};
        }
        info.ip = j["ip"].get<std::string>();
        
        if (!j.contains("port") || !j["port"].is_number_integer()) {
            return Error{TELEPORT_ERROR_INVALID_ARGUMENT, "Missing port"};
        }
        info.port = j["port"].get<uint16_t>();
        
        if (!j.contains("token") || !j["token"].is_string()) {
            return Error{TELEPORT_ERROR_INVALID_ARGUMENT, "Missing session token"};
        }
        info.session_token = j["token"].get<std::string>();
        
        if (!j.contains("exp") || !j["exp"].is_number_integer()) {
            return Error{TELEPORT_ERROR_INVALID_ARGUMENT, "Missing expiry"};
        }
        info.expires_at_ms = j["exp"].get<int64_t>();
        
        // Optional fields
        if (j.contains("name") && j["name"].is_string()) {
            info.device_name = j["name"].get<std::string>();
        }
        
        return info;
        
    } catch (const json::exception& e) {
        return Error{TELEPORT_ERROR_INVALID_ARGUMENT, 
            std::string("JSON parse error: ") + e.what()};
    }
}

/* ============================================================================
 * Validation
 * ============================================================================ */

QrValidationResult validate_pairing_info(const QrPairingInfo& info) {
    // Check version
    if (info.version != 1) {
        return QrValidationResult::InvalidVersion;
    }
    
    // Check required fields
    if (info.ip.empty() || info.port == 0) {
        return QrValidationResult::MissingFields;
    }
    
    // Check token format
    if (!is_valid_token_format(info.session_token)) {
        return QrValidationResult::InvalidToken;
    }
    
    // Check expiry
    if (info.is_expired()) {
        return QrValidationResult::Expired;
    }
    
    return QrValidationResult::Valid;
}

std::string validation_result_to_string(QrValidationResult result) {
    switch (result) {
        case QrValidationResult::Valid: return "Valid";
        case QrValidationResult::InvalidFormat: return "Invalid QR code format";
        case QrValidationResult::InvalidVersion: return "Unsupported QR version";
        case QrValidationResult::Expired: return "QR code has expired";
        case QrValidationResult::InvalidToken: return "Invalid session token";
        case QrValidationResult::MissingFields: return "Missing required fields";
        default: return "Unknown validation error";
    }
}

/* ============================================================================
 * QR Code Generation
 * ============================================================================ */

std::vector<uint8_t> QrBitmap::to_rgba(int module_size, int quiet_zone) const {
    int image_size = (size + 2 * quiet_zone) * module_size;
    std::vector<uint8_t> rgba(image_size * image_size * 4, 255); // White background
    
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (get(x, y)) {
                // Draw black module
                int px = (quiet_zone + x) * module_size;
                int py = (quiet_zone + y) * module_size;
                
                for (int dy = 0; dy < module_size; ++dy) {
                    for (int dx = 0; dx < module_size; ++dx) {
                        int idx = ((py + dy) * image_size + (px + dx)) * 4;
                        rgba[idx + 0] = 0;   // R
                        rgba[idx + 1] = 0;   // G
                        rgba[idx + 2] = 0;   // B
                        rgba[idx + 3] = 255; // A
                    }
                }
            }
        }
    }
    
    return rgba;
}

Result<QrBitmap> generate_qr_code(
    const std::string& text,
    QrErrorCorrection error_correction
) {
    try {
        // Convert error correction level
        QrCode::Ecc ecc;
        switch (error_correction) {
            case QrErrorCorrection::Low: ecc = QrCode::Ecc::LOW; break;
            case QrErrorCorrection::Medium: ecc = QrCode::Ecc::MEDIUM; break;
            case QrErrorCorrection::Quartile: ecc = QrCode::Ecc::QUARTILE; break;
            case QrErrorCorrection::High: ecc = QrCode::Ecc::HIGH; break;
            default: ecc = QrCode::Ecc::MEDIUM; break;
        }
        
        // Generate QR code
        QrCode qr = QrCode::encodeText(text.c_str(), ecc);
        
        // Convert to bitmap
        QrBitmap bitmap;
        bitmap.size = qr.getSize();
        
        // Pack into bytes (1 bit per module)
        int total_modules = bitmap.size * bitmap.size;
        bitmap.data.resize((total_modules + 7) / 8, 0);
        
        for (int y = 0; y < bitmap.size; ++y) {
            for (int x = 0; x < bitmap.size; ++x) {
                if (qr.getModule(x, y)) {
                    int bit_idx = y * bitmap.size + x;
                    bitmap.data[bit_idx / 8] |= (1 << (bit_idx % 8));
                }
            }
        }
        
        return bitmap;
        
    } catch (const std::exception& e) {
        return Error{TELEPORT_ERROR_INTERNAL, 
            std::string("QR generation failed: ") + e.what()};
    }
}

// IEEE CRC32 helper (used only for PNG chunk writing)
namespace {

// Build CRC32 lookup table at static initialisation time.
const uint32_t* crc32_table() {
    static uint32_t table[256];
    static bool initialised = false;
    if (!initialised) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[n] = c;
        }
        initialised = true;
    }
    return table;
}

uint32_t ieee_crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    const uint32_t* tbl = crc32_table();
    for (size_t i = 0; i < len; ++i) {
        crc = tbl[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// Write a correctly CRC'd PNG chunk.
void write_png_chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    // Length (big-endian)
    uint32_t len = static_cast<uint32_t>(data.size());
    out.push_back((len >> 24) & 0xFF);
    out.push_back((len >> 16) & 0xFF);
    out.push_back((len >> 8)  & 0xFF);
    out.push_back( len        & 0xFF);

    // Type
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(type[i]));
    }

    // Data
    out.insert(out.end(), data.begin(), data.end());

    // CRC over type + data
    uint32_t crc = ieee_crc32(reinterpret_cast<const uint8_t*>(type), 4);
    if (!data.empty()) {
        // Continue CRC over the data bytes, feeding the running value.
        const uint32_t* tbl = crc32_table();
        uint32_t running = crc ^ 0xFFFFFFFFu; // undo finalisation
        for (auto b : data) {
            running = tbl[(running ^ b) & 0xFF] ^ (running >> 8);
        }
        crc = running ^ 0xFFFFFFFFu;
    }
    out.push_back((crc >> 24) & 0xFF);
    out.push_back((crc >> 16) & 0xFF);
    out.push_back((crc >> 8)  & 0xFF);
    out.push_back( crc        & 0xFF);
}

} // anonymous namespace

Result<std::vector<uint8_t>> generate_qr_png(
    const std::string& text,
    int module_size
) {
    auto qr_result = generate_qr_code(text, QrErrorCorrection::Medium);
    if (!qr_result) {
        return qr_result.error();
    }

    auto rgba = qr_result->to_rgba(module_size, 4);
    // BUG FIX (Bug 2): derive image_size from the actual RGBA buffer so it
    // always matches what to_rgba() allocated, regardless of quiet_zone.
    int image_size = static_cast<int>(std::sqrt(static_cast<double>(rgba.size()) / 4.0));
    
    // For now, return raw RGBA data with a simple header
    // A full PNG encoder would be more complex
    // TODO: Integrate stb_image_write for proper PNG output
    
    std::vector<uint8_t> png;
    png.reserve(rgba.size() + 100);
    
    // Simple BMP header instead of PNG (easier to implement)
    // BMP is widely supported and simpler
    int row_size = ((image_size * 4 + 3) / 4) * 4;
    int data_size = row_size * image_size;
    int file_size = 54 + data_size;
    
    // BMP header
    png.push_back('B');
    png.push_back('M');
    png.push_back(file_size & 0xFF);
    png.push_back((file_size >> 8) & 0xFF);
    png.push_back((file_size >> 16) & 0xFF);
    png.push_back((file_size >> 24) & 0xFF);
    png.push_back(0); png.push_back(0); // Reserved
    png.push_back(0); png.push_back(0); // Reserved
    png.push_back(54); png.push_back(0); png.push_back(0); png.push_back(0); // Data offset
    
    // DIB header
    png.push_back(40); png.push_back(0); png.push_back(0); png.push_back(0); // Header size
    png.push_back(image_size & 0xFF);
    png.push_back((image_size >> 8) & 0xFF);
    png.push_back((image_size >> 16) & 0xFF);
    png.push_back((image_size >> 24) & 0xFF);
    int neg_height = -image_size; // Negative for top-down
    png.push_back(neg_height & 0xFF);
    png.push_back((neg_height >> 8) & 0xFF);
    png.push_back((neg_height >> 16) & 0xFF);
    png.push_back((neg_height >> 24) & 0xFF);
    png.push_back(1); png.push_back(0); // Planes
    png.push_back(32); png.push_back(0); // Bits per pixel
    png.push_back(0); png.push_back(0); png.push_back(0); png.push_back(0); // Compression
    png.push_back(data_size & 0xFF);
    png.push_back((data_size >> 8) & 0xFF);
    png.push_back((data_size >> 16) & 0xFF);
    png.push_back((data_size >> 24) & 0xFF);
    png.push_back(0); png.push_back(0); png.push_back(0); png.push_back(0); // X pixels per meter
    png.push_back(0); png.push_back(0); png.push_back(0); png.push_back(0); // Y pixels per meter
    png.push_back(0); png.push_back(0); png.push_back(0); png.push_back(0); // Colors in table
    png.push_back(0); png.push_back(0); png.push_back(0); png.push_back(0); // Important colors
    
    // Pixel data (BGRA format for BMP)
    for (int y = 0; y < image_size; ++y) {
        for (int x = 0; x < image_size; ++x) {
            int idx = (y * image_size + x) * 4;
            png.push_back(rgba[idx + 2]); // B
            png.push_back(rgba[idx + 1]); // G
            png.push_back(rgba[idx + 0]); // R
            png.push_back(rgba[idx + 3]); // A
        }
    }
    
    return png;
}

Result<std::vector<uint8_t>> generate_pairing_qr(
    const QrPairingInfo& info,
    int module_size
) {
    std::string json_data = encode_pairing_to_json(info);
    return generate_qr_png(json_data, module_size);
}

} // namespace teleport
