/**
 * @file types.h
 * @brief Internal type definitions for Teleport core
 */

#ifndef TELEPORT_TYPES_H
#define TELEPORT_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

namespace teleport {

/* ============================================================================
 * Time Utilities
 * ============================================================================ */

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;
using Milliseconds = std::chrono::milliseconds;

inline int64_t now_ms() {
    return std::chrono::duration_cast<Milliseconds>(
        Clock::now().time_since_epoch()
    ).count();
}

/* ============================================================================
 * Network Types
 * ============================================================================ */

/**
 * @brief Socket handle type (platform-specific)
 */
#ifdef _WIN32
using SocketHandle = uintptr_t;  // SOCKET is UINT_PTR on Windows
constexpr SocketHandle INVALID_SOCKET_HANDLE = ~static_cast<SocketHandle>(0);
#else
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
#endif

/**
 * @brief Network address (IPv4/IPv6 agnostic)
 */
struct NetworkAddress {
    std::string ip;
    uint16_t port = 0;
    
    bool operator==(const NetworkAddress& other) const {
        return ip == other.ip && port == other.port;
    }
    
    std::string to_string() const {
        return ip + ":" + std::to_string(port);
    }
};

/* ============================================================================
 * Device Types
 * ============================================================================ */

/**
 * @brief Capability flags
 */
enum class Capability : uint32_t {
    None     = 0,
    Parallel = 1 << 0,
    Resume   = 1 << 1,
    Compress = 1 << 2,
    Encrypt  = 1 << 3,
    
    // Default capabilities for this implementation
    Default = Parallel | Resume
};

inline Capability operator|(Capability a, Capability b) {
    return static_cast<Capability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline Capability operator&(Capability a, Capability b) {
    return static_cast<Capability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool has_capability(Capability caps, Capability flag) {
    return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(flag)) != 0;
}

/**
 * @brief Operating system identifier
 */
enum class OperatingSystem {
    Unknown,
    Windows,
    macOS,
    Linux,
    Android,
    iOS
};

inline std::string os_to_string(OperatingSystem os) {
    switch (os) {
        case OperatingSystem::Windows: return "Windows";
        case OperatingSystem::macOS: return "macOS";
        case OperatingSystem::Linux: return "Linux";
        case OperatingSystem::Android: return "Android";
        case OperatingSystem::iOS: return "iOS";
        default: return "Unknown";
    }
}

inline OperatingSystem os_from_string(const std::string& s) {
    if (s == "Windows") return OperatingSystem::Windows;
    if (s == "macOS") return OperatingSystem::macOS;
    if (s == "Linux") return OperatingSystem::Linux;
    if (s == "Android") return OperatingSystem::Android;
    if (s == "iOS") return OperatingSystem::iOS;
    return OperatingSystem::Unknown;
}

/**
 * @brief Discovered device information (internal C++ version)
 */
struct Device {
    std::string id;              // UUID v4
    std::string name;            // User-friendly name
    OperatingSystem os = OperatingSystem::Unknown;
    NetworkAddress address;      // IP:port for control channel
    Capability capabilities = Capability::Default;
    int64_t last_seen_ms = 0;    // Timestamp of last discovery packet
    
    bool is_expired(int64_t ttl_ms) const {
        return (now_ms() - last_seen_ms) > ttl_ms;
    }
};

/* ============================================================================
 * File Types
 * ============================================================================ */

/**
 * @brief File information for transfer
 */
struct FileInfo {
    uint32_t id = 0;             // ID within transfer session
    std::string path;            // Full path
    std::string name;            // Filename only
    uint64_t size = 0;           // File size in bytes
    
    // Calculated during transfer
    uint32_t total_chunks = 0;   // Total chunk count
};

/**
 * @brief Chunk metadata
 */
struct ChunkHeader {
    uint32_t file_id;
    uint32_t chunk_id;
    uint32_t offset;             // Offset within file (for small files)
    uint32_t size;               // Data size in this chunk
    
    static constexpr size_t HEADER_SIZE = 16;
    
    // Serialize to bytes (network byte order)
    void serialize(uint8_t* buffer) const;
    
    // Deserialize from bytes
    static ChunkHeader deserialize(const uint8_t* buffer);
};

/* ============================================================================
 * Transfer Types
 * ============================================================================ */

/**
 * @brief Transfer state
 */
enum class TransferState {
    Idle,
    Connecting,
    Handshaking,
    Transferring,
    Paused,
    Completing,
    Complete,
    Failed,
    Cancelled
};

inline std::string state_to_string(TransferState state) {
    switch (state) {
        case TransferState::Idle: return "Idle";
        case TransferState::Connecting: return "Connecting";
        case TransferState::Handshaking: return "Handshaking";
        case TransferState::Transferring: return "Transferring";
        case TransferState::Paused: return "Paused";
        case TransferState::Completing: return "Completing";
        case TransferState::Complete: return "Complete";
        case TransferState::Failed: return "Failed";
        case TransferState::Cancelled: return "Cancelled";
        default: return "Unknown";
    }
}

/**
 * @brief Transfer statistics
 */
struct TransferStats {
    uint64_t bytes_transferred = 0;
    uint64_t bytes_total = 0;
    uint32_t files_completed = 0;
    uint32_t files_total = 0;
    double speed_bps = 0.0;          // Bytes per second
    int32_t eta_seconds = -1;        // -1 if unknown
    TimePoint start_time;
    TimePoint last_update;
    
    double progress_percent() const {
        return bytes_total > 0 
            ? (static_cast<double>(bytes_transferred) / bytes_total) * 100.0 
            : 0.0;
    }
};

/* ============================================================================
 * Protocol Types
 * ============================================================================ */

/**
 * @brief Control message types
 */
enum class MessageType {
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
    
    // Progress & Status
    Progress,
    ResumeRequest,
    Complete,
    Error
};

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * @brief Engine configuration (internal)
 */
struct Config {
    std::string device_name;
    uint16_t control_port = 0;           // 0 = auto-select
    uint32_t chunk_size = 2 * 1024 * 1024;  // 2 MB
    uint8_t parallel_streams = 4;
    uint32_t discovery_interval_ms = 1000;
    uint32_t device_ttl_ms = 5000;
    std::string download_path;
    
    static Config with_defaults();
};

/* ============================================================================
 * Result Type
 * ============================================================================ */

/**
 * @brief Error information
 */
struct Error {
    int code;
    std::string message;
    
    explicit Error(int c = 0, std::string msg = "") 
        : code(c), message(std::move(msg)) {}
    
    bool ok() const { return code == 0; }
    operator bool() const { return !ok(); }  // True if error exists
};

/**
 * @brief Result type for operations that can fail
 */
template<typename T>
class Result {
public:
    Result(T value) : m_value(std::move(value)), m_error() {}
    Result(Error error) : m_value(), m_error(std::move(error)) {}
    
    bool ok() const { return !m_error; }
    operator bool() const { return ok(); }
    
    const T& value() const { return *m_value; }
    T& value() { return *m_value; }
    const Error& error() const { return m_error; }
    
    const T& operator*() const { return value(); }
    T& operator*() { return value(); }
    
private:
    std::optional<T> m_value;
    Error m_error;
};

// Specialization for void
template<>
class Result<void> {
public:
    Result() : m_error() {}
    Result(Error error) : m_error(std::move(error)) {}
    
    bool ok() const { return !m_error; }
    operator bool() const { return ok(); }
    const Error& error() const { return m_error; }
    
private:
    Error m_error;
};

} // namespace teleport

#endif // TELEPORT_TYPES_H
