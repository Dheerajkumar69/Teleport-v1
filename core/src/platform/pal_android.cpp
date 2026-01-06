/**
 * @file pal_android.cpp
 * @brief Platform Abstraction Layer - Android/POSIX Implementation
 */

#include "pal.hpp"
#include "teleport/errors.h"

#if defined(TELEPORT_PLATFORM_ANDROID) || defined(__ANDROID__)

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <cerrno>
#include <cstring>

#include <fstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <filesystem>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace teleport {
namespace pal {

/* ============================================================================
 * Platform Initialization
 * ============================================================================ */

bool platform_init() {
    // No-op on POSIX systems - sockets work out of the box
    return true;
}

void platform_cleanup() {
    // No-op on POSIX systems
}

/* ============================================================================
 * System Information
 * ============================================================================ */

std::string get_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "Android Device";
}

std::string get_device_name() {
    // On Android, the device name is passed from Java side during initialization
    // This is just a fallback
    return get_hostname();
}

OperatingSystem get_os_type() {
    return OperatingSystem::Android;
}

std::vector<std::string> get_local_ips() {
    std::vector<std::string> ips;
    
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        return ips;
    }
    
    for (auto* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        
        // Only IPv4 for now
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        
        // Skip loopback
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        
        // Skip interfaces that are down
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        
        auto* addr = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        
        std::string ip_str(ip);
        // Skip link-local addresses
        if (ip_str.substr(0, 4) != "169.") {
            ips.push_back(ip_str);
        }
    }
    
    freeifaddrs(ifaddr);
    return ips;
}

std::string get_primary_local_ip() {
    auto ips = get_local_ips();
    if (ips.empty()) {
        return "127.0.0.1";
    }
    
    // Prefer 192.168.x.x (common LAN)
    for (const auto& ip : ips) {
        if (ip.substr(0, 8) == "192.168.") {
            return ip;
        }
    }
    
    // Then 10.x.x.x
    for (const auto& ip : ips) {
        if (ip.substr(0, 3) == "10.") {
            return ip;
        }
    }
    
    return ips[0];
}

std::string get_broadcast_address() {
    std::string ip = get_primary_local_ip();
    auto last_dot = ip.rfind('.');
    if (last_dot != std::string::npos) {
        return ip.substr(0, last_dot) + ".255";
    }
    return "255.255.255.255";
}

/* ============================================================================
 * Android TCP Socket Implementation
 * ============================================================================ */

class AndroidTcpSocket : public TcpSocket {
public:
    AndroidTcpSocket() : m_socket(-1), m_last_error(0) {
        m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    
    explicit AndroidTcpSocket(int sock) : m_socket(sock), m_last_error(0) {}
    
    ~AndroidTcpSocket() override {
        close();
    }
    
    bool is_valid() const override { return m_socket >= 0; }
    
    void close() override {
        if (m_socket >= 0) {
            ::close(m_socket);
            m_socket = -1;
        }
    }
    
    SocketHandle handle() const override { 
        return static_cast<SocketHandle>(m_socket); 
    }
    
    NetworkAddress local_address() const override {
        NetworkAddress addr;
        struct sockaddr_in sin;
        socklen_t len = sizeof(sin);
        if (getsockname(m_socket, reinterpret_cast<sockaddr*>(&sin), &len) == 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sin.sin_addr, ip, sizeof(ip));
            addr.ip = ip;
            addr.port = ntohs(sin.sin_port);
        }
        return addr;
    }
    
    NetworkAddress remote_address() const override {
        NetworkAddress addr;
        struct sockaddr_in sin;
        socklen_t len = sizeof(sin);
        if (getpeername(m_socket, reinterpret_cast<sockaddr*>(&sin), &len) == 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sin.sin_addr, ip, sizeof(ip));
            addr.ip = ip;
            addr.port = ntohs(sin.sin_port);
        }
        return addr;
    }
    
    bool set_non_blocking(bool enabled) override {
        int flags = fcntl(m_socket, F_GETFL, 0);
        if (flags < 0) return false;
        
        if (enabled) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        return fcntl(m_socket, F_SETFL, flags) == 0;
    }
    
    bool set_recv_timeout(int ms) override {
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        return setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
    }
    
    bool set_send_timeout(int ms) override {
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        return setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
    }
    
    int last_error() const override { return m_last_error; }
    
    std::string last_error_string() const override {
        return std::string(strerror(m_last_error));
    }
    
    Result<void> connect(const std::string& ip, uint16_t port, int timeout_ms) override {
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            m_last_error = EINVAL;
            return make_error(TELEPORT_ERROR_INVALID_ARGUMENT, "Invalid IP address");
        }
        
        // Set non-blocking for timeout support
        set_non_blocking(true);
        
        int ret = ::connect(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (ret < 0) {
            if (errno != EINPROGRESS) {
                m_last_error = errno;
                return make_error(TELEPORT_ERROR_SOCKET_CONNECT, last_error_string());
            }
            
            // Wait for connection with timeout
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(m_socket, &write_fds);
            
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            
            ret = select(m_socket + 1, nullptr, &write_fds, nullptr, &tv);
            if (ret == 0) {
                m_last_error = ETIMEDOUT;
                return make_error(TELEPORT_ERROR_TIMEOUT, "Connection timed out");
            }
            if (ret < 0) {
                m_last_error = errno;
                return make_error(TELEPORT_ERROR_SOCKET_CONNECT, last_error_string());
            }
            
            // Check for connection error
            int opt_err = 0;
            socklen_t opt_len = sizeof(opt_err);
            getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &opt_err, &opt_len);
            if (opt_err != 0) {
                m_last_error = opt_err;
                return make_error(TELEPORT_ERROR_SOCKET_CONNECT, strerror(opt_err));
            }
        }
        
        // Restore blocking mode
        set_non_blocking(false);
        return ok();
    }
    
    Result<void> bind(uint16_t port) override {
        int opt = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (::bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            m_last_error = errno;
            return make_error(TELEPORT_ERROR_SOCKET_BIND, last_error_string());
        }
        return ok();
    }
    
    Result<void> listen(int backlog) override {
        if (::listen(m_socket, backlog) < 0) {
            m_last_error = errno;
            return make_error(TELEPORT_ERROR_SOCKET_BIND, last_error_string());
        }
        return ok();
    }
    
    Result<std::unique_ptr<TcpSocket>> accept() override {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        int client = ::accept(m_socket, reinterpret_cast<sockaddr*>(&addr), &addr_len);
        if (client < 0) {
            m_last_error = errno;
            return make_error(TELEPORT_ERROR_SOCKET_RECV, last_error_string());
        }
        return std::unique_ptr<TcpSocket>(std::make_unique<AndroidTcpSocket>(client));
    }
    
    Result<size_t> send(const uint8_t* data, size_t len) override {
        ssize_t sent = ::send(m_socket, data, len, MSG_NOSIGNAL);
        if (sent < 0) {
            m_last_error = errno;
            return make_error(TELEPORT_ERROR_SOCKET_SEND, last_error_string());
        }
        return static_cast<size_t>(sent);
    }
    
    Result<size_t> recv(uint8_t* buffer, size_t len) override {
        ssize_t received = ::recv(m_socket, buffer, len, 0);
        if (received < 0) {
            m_last_error = errno;
            return make_error(TELEPORT_ERROR_SOCKET_RECV, last_error_string());
        }
        if (received == 0) {
            return make_error(TELEPORT_ERROR_SOCKET_RECV, "Connection closed");
        }
        return static_cast<size_t>(received);
    }
    
    Result<void> send_all(const uint8_t* data, size_t len) override {
        size_t total_sent = 0;
        while (total_sent < len) {
            auto result = send(data + total_sent, len - total_sent);
            if (!result) return result.error();
            total_sent += *result;
        }
        return ok();
    }
    
    Result<void> recv_all(uint8_t* buffer, size_t len) override {
        size_t total_recv = 0;
        while (total_recv < len) {
            auto result = recv(buffer + total_recv, len - total_recv);
            if (!result) return result.error();
            total_recv += *result;
        }
        return ok();
    }
    
private:
    int m_socket;
    mutable int m_last_error;
};

/* ============================================================================
 * Android UDP Socket Implementation
 * ============================================================================ */

class AndroidUdpSocket : public UdpSocket {
public:
    AndroidUdpSocket() : m_socket(-1), m_last_error(0) {
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
    
    ~AndroidUdpSocket() override {
        close();
    }
    
    bool is_valid() const override { return m_socket >= 0; }
    
    void close() override {
        if (m_socket >= 0) {
            ::close(m_socket);
            m_socket = -1;
        }
    }
    
    SocketHandle handle() const override { 
        return static_cast<SocketHandle>(m_socket); 
    }
    
    NetworkAddress local_address() const override {
        NetworkAddress addr;
        struct sockaddr_in sin;
        socklen_t len = sizeof(sin);
        if (getsockname(m_socket, reinterpret_cast<sockaddr*>(&sin), &len) == 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sin.sin_addr, ip, sizeof(ip));
            addr.ip = ip;
            addr.port = ntohs(sin.sin_port);
        }
        return addr;
    }
    
    NetworkAddress remote_address() const override {
        return {};  // UDP has no persistent remote
    }
    
    bool set_non_blocking(bool enabled) override {
        int flags = fcntl(m_socket, F_GETFL, 0);
        if (flags < 0) return false;
        
        if (enabled) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        return fcntl(m_socket, F_SETFL, flags) == 0;
    }
    
    bool set_recv_timeout(int ms) override {
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        return setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
    }
    
    bool set_send_timeout(int ms) override {
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        return setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
    }
    
    int last_error() const override { return m_last_error; }
    
    std::string last_error_string() const override {
        return std::string(strerror(m_last_error));
    }
    
    Result<void> bind(uint16_t port) override {
        int opt = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (::bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            m_last_error = errno;
            return make_error(TELEPORT_ERROR_SOCKET_BIND, last_error_string());
        }
        return ok();
    }
    
    Result<void> enable_broadcast() override {
        int opt = 1;
        if (setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
            m_last_error = errno;
            return make_error(TELEPORT_ERROR_SOCKET_CREATE, last_error_string());
        }
        return ok();
    }
    
    Result<size_t> send_to(const uint8_t* data, size_t len, 
                           const std::string& ip, uint16_t port) override {
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        
        ssize_t sent = ::sendto(m_socket, data, len, 0,
                               reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (sent < 0) {
            m_last_error = errno;
            return make_error(TELEPORT_ERROR_SOCKET_SEND, last_error_string());
        }
        return static_cast<size_t>(sent);
    }
    
    Result<size_t> recv_from(uint8_t* buffer, size_t len, 
                             std::string& out_ip, uint16_t& out_port) override {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        
        ssize_t received = ::recvfrom(m_socket, buffer, len, 0,
                                      reinterpret_cast<sockaddr*>(&addr), &addr_len);
        if (received < 0) {
            m_last_error = errno;
            return make_error(TELEPORT_ERROR_SOCKET_RECV, last_error_string());
        }
        
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        out_ip = ip;
        out_port = ntohs(addr.sin_port);
        
        return static_cast<size_t>(received);
    }
    
private:
    int m_socket;
    mutable int m_last_error;
};

/* ============================================================================
 * Socket Factory Functions
 * ============================================================================ */

std::unique_ptr<TcpSocket> create_tcp_socket(const SocketOptions& opts) {
    auto sock = std::make_unique<AndroidTcpSocket>();
    if (!sock->is_valid()) {
        return nullptr;
    }
    
    if (opts.non_blocking) {
        sock->set_non_blocking(true);
    }
    if (opts.recv_timeout_ms > 0) {
        sock->set_recv_timeout(opts.recv_timeout_ms);
    }
    if (opts.send_timeout_ms > 0) {
        sock->set_send_timeout(opts.send_timeout_ms);
    }
    
    return sock;
}

std::unique_ptr<UdpSocket> create_udp_socket(const SocketOptions& opts) {
    auto sock = std::make_unique<AndroidUdpSocket>();
    if (!sock->is_valid()) {
        return nullptr;
    }
    
    if (opts.broadcast) {
        sock->enable_broadcast();
    }
    if (opts.non_blocking) {
        sock->set_non_blocking(true);
    }
    if (opts.recv_timeout_ms > 0) {
        sock->set_recv_timeout(opts.recv_timeout_ms);
    }
    
    return sock;
}

/* ============================================================================
 * Android File Implementation
 * ============================================================================ */

class AndroidFile : public File {
public:
    AndroidFile(const std::string& path, FileMode mode) 
        : m_path(path), m_size(0), m_position(0) {
        
        std::ios_base::openmode flags = std::ios::binary;
        switch (mode) {
            case FileMode::Read:
                flags |= std::ios::in;
                break;
            case FileMode::Write:
                flags |= std::ios::out | std::ios::trunc;
                break;
            case FileMode::ReadWrite:
                flags |= std::ios::in | std::ios::out;
                break;
            case FileMode::Append:
                flags |= std::ios::out | std::ios::app;
                break;
        }
        
        m_stream.open(path, flags);
        if (m_stream.is_open()) {
            m_stream.seekg(0, std::ios::end);
            m_size = static_cast<uint64_t>(m_stream.tellg());
            m_stream.seekg(0, std::ios::beg);
        }
    }
    
    ~AndroidFile() override {
        close();
    }
    
    bool is_open() const override { return m_stream.is_open(); }
    
    void close() override { m_stream.close(); }
    
    uint64_t size() const override { return m_size; }
    
    std::string path() const override { return m_path; }
    
    Result<size_t> read(uint8_t* buffer, size_t len) override {
        m_stream.read(reinterpret_cast<char*>(buffer), len);
        auto bytes_read = static_cast<size_t>(m_stream.gcount());
        m_position += bytes_read;
        
        if (m_stream.fail() && !m_stream.eof()) {
            return make_error(TELEPORT_ERROR_FILE_READ, "Read failed");
        }
        return bytes_read;
    }
    
    Result<size_t> write(const uint8_t* data, size_t len) override {
        m_stream.write(reinterpret_cast<const char*>(data), len);
        if (m_stream.fail()) {
            return make_error(TELEPORT_ERROR_FILE_WRITE, "Write failed");
        }
        m_position += len;
        if (m_position > m_size) m_size = m_position;
        return len;
    }
    
    Result<void> seek(uint64_t offset) override {
        m_stream.seekg(static_cast<std::streamoff>(offset));
        m_stream.seekp(static_cast<std::streamoff>(offset));
        if (m_stream.fail()) {
            return make_error(TELEPORT_ERROR_FILE_READ, "Seek failed");
        }
        m_position = offset;
        return ok();
    }
    
    uint64_t tell() const override { return m_position; }
    
    Result<void> flush() override {
        m_stream.flush();
        if (m_stream.fail()) {
            return make_error(TELEPORT_ERROR_FILE_WRITE, "Flush failed");
        }
        return ok();
    }
    
private:
    std::fstream m_stream;
    std::string m_path;
    uint64_t m_size;
    uint64_t m_position;
};

Result<std::unique_ptr<File>> open_file(const std::string& path, FileMode mode) {
    auto file = std::make_unique<AndroidFile>(path, mode);
    if (!file->is_open()) {
        return make_error(TELEPORT_ERROR_FILE_OPEN, "Failed to open: " + path);
    }
    return std::unique_ptr<File>(std::move(file));
}

bool file_exists(const std::string& path) {
    return std::filesystem::exists(path);
}

uint64_t file_size(const std::string& path) {
    try {
        return std::filesystem::file_size(path);
    } catch (...) {
        return 0;
    }
}

std::string get_filename(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

bool create_directory(const std::string& path) {
    try {
        return std::filesystem::create_directories(path);
    } catch (...) {
        return false;
    }
}

bool is_directory(const std::string& path) {
    return std::filesystem::is_directory(path);
}

/* ============================================================================
 * Time Utilities
 * ============================================================================ */

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int64_t timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

} // namespace pal
} // namespace teleport

#endif // TELEPORT_PLATFORM_ANDROID || __ANDROID__
