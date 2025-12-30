/**
 * @file pal.hpp
 * @brief Platform Abstraction Layer interface
 * 
 * Provides cross-platform wrappers for:
 * - Socket operations (TCP/UDP)
 * - File I/O
 * - System utilities (hostname, IP detection)
 */

#ifndef TELEPORT_PAL_HPP
#define TELEPORT_PAL_HPP

#include "teleport/types.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace teleport {
namespace pal {

/* ============================================================================
 * Platform Initialization
 * ============================================================================ */

/**
 * @brief Initialize platform-specific subsystems (e.g., Winsock)
 * @return true on success
 */
bool platform_init();

/**
 * @brief Cleanup platform-specific subsystems
 */
void platform_cleanup();

/**
 * @brief RAII wrapper for platform init/cleanup
 */
class PlatformGuard {
public:
    PlatformGuard() : m_initialized(platform_init()) {}
    ~PlatformGuard() { if (m_initialized) platform_cleanup(); }
    bool ok() const { return m_initialized; }
    
    PlatformGuard(const PlatformGuard&) = delete;
    PlatformGuard& operator=(const PlatformGuard&) = delete;
private:
    bool m_initialized;
};

/* ============================================================================
 * System Information
 * ============================================================================ */

/**
 * @brief Get the local hostname
 */
std::string get_hostname();

/**
 * @brief Get a user-friendly device name
 */
std::string get_device_name();

/**
 * @brief Get the operating system type
 */
OperatingSystem get_os_type();

/**
 * @brief Get local IP addresses (non-loopback)
 */
std::vector<std::string> get_local_ips();

/**
 * @brief Get the primary local IP (best guess for LAN)
 */
std::string get_primary_local_ip();

/**
 * @brief Get broadcast address for the local network
 */
std::string get_broadcast_address();

/* ============================================================================
 * Socket Abstraction
 * ============================================================================ */

/**
 * @brief Socket types
 */
enum class SocketType {
    TCP,
    UDP
};

/**
 * @brief Socket options
 */
struct SocketOptions {
    bool reuse_addr = true;
    bool broadcast = false;       // For UDP
    bool non_blocking = false;
    int recv_timeout_ms = 0;      // 0 = no timeout
    int send_timeout_ms = 0;
    int recv_buffer_size = 0;     // 0 = default
    int send_buffer_size = 0;
};

/**
 * @brief Abstract socket wrapper
 */
class Socket {
public:
    virtual ~Socket() = default;
    
    // Core operations
    virtual bool is_valid() const = 0;
    virtual void close() = 0;
    
    // Get underlying handle (for select/poll)
    virtual SocketHandle handle() const = 0;
    
    // Get local/remote address
    virtual NetworkAddress local_address() const = 0;
    virtual NetworkAddress remote_address() const = 0;
    
    // Set options
    virtual bool set_non_blocking(bool enabled) = 0;
    virtual bool set_recv_timeout(int ms) = 0;
    virtual bool set_send_timeout(int ms) = 0;
    
    // Get last error
    virtual int last_error() const = 0;
    virtual std::string last_error_string() const = 0;
};

/**
 * @brief TCP socket for stream connections
 */
class TcpSocket : public Socket {
public:
    virtual ~TcpSocket() = default;
    
    // Client operations
    virtual Result<void> connect(const std::string& ip, uint16_t port, int timeout_ms = 5000) = 0;
    
    // Server operations
    virtual Result<void> bind(uint16_t port) = 0;
    virtual Result<void> listen(int backlog = 16) = 0;
    virtual Result<std::unique_ptr<TcpSocket>> accept() = 0;
    
    // Data transfer
    virtual Result<size_t> send(const uint8_t* data, size_t len) = 0;
    virtual Result<size_t> recv(uint8_t* buffer, size_t len) = 0;
    
    // Convenience for exact byte counts
    virtual Result<void> send_all(const uint8_t* data, size_t len) = 0;
    virtual Result<void> recv_all(uint8_t* buffer, size_t len) = 0;
};

/**
 * @brief UDP socket for datagrams
 */
class UdpSocket : public Socket {
public:
    virtual ~UdpSocket() = default;
    
    // Bind to port
    virtual Result<void> bind(uint16_t port) = 0;
    
    // Enable broadcasting
    virtual Result<void> enable_broadcast() = 0;
    
    // Send datagram
    virtual Result<size_t> send_to(
        const uint8_t* data, 
        size_t len, 
        const std::string& ip, 
        uint16_t port
    ) = 0;
    
    // Receive datagram (returns sender address)
    virtual Result<size_t> recv_from(
        uint8_t* buffer, 
        size_t len, 
        std::string& out_ip, 
        uint16_t& out_port
    ) = 0;
};

/**
 * @brief Create a TCP socket
 */
std::unique_ptr<TcpSocket> create_tcp_socket(const SocketOptions& opts = {});

/**
 * @brief Create a UDP socket
 */
std::unique_ptr<UdpSocket> create_udp_socket(const SocketOptions& opts = {});

/* ============================================================================
 * File I/O
 * ============================================================================ */

/**
 * @brief File open modes
 */
enum class FileMode {
    Read,
    Write,
    ReadWrite,
    Append
};

/**
 * @brief File handle wrapper
 */
class File {
public:
    virtual ~File() = default;
    
    virtual bool is_open() const = 0;
    virtual void close() = 0;
    
    // File info
    virtual uint64_t size() const = 0;
    virtual std::string path() const = 0;
    
    // Read/Write
    virtual Result<size_t> read(uint8_t* buffer, size_t len) = 0;
    virtual Result<size_t> write(const uint8_t* data, size_t len) = 0;
    
    // Seek
    virtual Result<void> seek(uint64_t offset) = 0;
    virtual uint64_t tell() const = 0;
    
    // Flush
    virtual Result<void> flush() = 0;
};

/**
 * @brief Open a file
 */
Result<std::unique_ptr<File>> open_file(const std::string& path, FileMode mode);

/**
 * @brief Check if a file exists
 */
bool file_exists(const std::string& path);

/**
 * @brief Get file size
 */
uint64_t file_size(const std::string& path);

/**
 * @brief Get filename from path
 */
std::string get_filename(const std::string& path);

/**
 * @brief Create directory (and parents if needed)
 */
bool create_directory(const std::string& path);

/**
 * @brief Check if path is a directory
 */
bool is_directory(const std::string& path);

/* ============================================================================
 * Time Utilities
 * ============================================================================ */

/**
 * @brief Sleep for milliseconds
 */
void sleep_ms(int ms);

/**
 * @brief Get current timestamp in milliseconds
 */
int64_t timestamp_ms();

} // namespace pal
} // namespace teleport

#endif // TELEPORT_PAL_HPP
