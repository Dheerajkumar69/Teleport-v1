/**
 * @file pal.cpp
 * @brief Platform Abstraction Layer - Windows Implementation
 */

#include "pal.hpp"
#include "teleport/errors.h"

#ifdef TELEPORT_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace teleport {
namespace pal {

/* ============================================================================
 * Platform Initialization
 * ============================================================================ */

bool platform_init() {
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    return result == 0;
}

void platform_cleanup() {
    WSACleanup();
}

/* ============================================================================
 * System Information
 * ============================================================================ */

std::string get_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "Unknown";
}

std::string get_device_name() {
    // Try computer name first
    char name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(name);
    if (GetComputerNameA(name, &size)) {
        return std::string(name);
    }
    return get_hostname();
}

OperatingSystem get_os_type() {
    return OperatingSystem::Windows;
}

std::vector<std::string> get_local_ips() {
    std::vector<std::string> ips;
    
    ULONG buffer_size = 15000;
    std::vector<uint8_t> buffer(buffer_size);
    PIP_ADAPTER_ADDRESSES addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
    
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | 
                  GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    
    DWORD ret = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &buffer_size);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(buffer_size);
        addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        ret = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &buffer_size);
    }
    
    if (ret != NO_ERROR) {
        return ips;
    }
    
    for (auto adapter = addresses; adapter; adapter = adapter->Next) {
        // Skip non-operational adapters
        if (adapter->OperStatus != IfOperStatusUp) continue;
        
        // Skip loopback
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        
        for (auto unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
                char ip[INET_ADDRSTRLEN];
                auto addr = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
                
                // Skip link-local
                std::string ip_str(ip);
                if (ip_str.substr(0, 3) != "169") {
                    ips.push_back(ip_str);
                }
            }
        }
    }
    
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
    
    // Then 172.16-31.x.x
    for (const auto& ip : ips) {
        if (ip.substr(0, 4) == "172.") {
            try {
                size_t dot_pos = ip.find('.', 4);
                if (dot_pos == std::string::npos || dot_pos <= 4) continue;
                int second = std::stoi(ip.substr(4, dot_pos - 4));
                if (second >= 16 && second <= 31) {
                    return ip;
                }
            } catch (const std::exception&) {
                // SECURITY: Skip malformed IP addresses instead of crashing
                continue;
            }
        }
    }
    
    return ips[0];
}

std::string get_broadcast_address() {
    // For simplicity, use subnet broadcast of primary IP
    std::string ip = get_primary_local_ip();
    auto last_dot = ip.rfind('.');
    if (last_dot != std::string::npos) {
        return ip.substr(0, last_dot) + ".255";
    }
    return "255.255.255.255";
}

/* ============================================================================
 * Windows TCP Socket Implementation
 * ============================================================================ */

class WinTcpSocket : public TcpSocket {
public:
    WinTcpSocket() : m_socket(INVALID_SOCKET), m_last_error(0) {
        m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    
    explicit WinTcpSocket(SOCKET sock) : m_socket(sock), m_last_error(0) {}
    
    ~WinTcpSocket() override {
        close();
    }
    
    bool is_valid() const override { return m_socket != INVALID_SOCKET; }
    
    void close() override {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }
    
    SocketHandle handle() const override { 
        return static_cast<SocketHandle>(m_socket); 
    }
    
    NetworkAddress local_address() const override {
        NetworkAddress addr;
        sockaddr_in sin;
        int len = sizeof(sin);
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
        sockaddr_in sin;
        int len = sizeof(sin);
        if (getpeername(m_socket, reinterpret_cast<sockaddr*>(&sin), &len) == 0) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sin.sin_addr, ip, sizeof(ip));
            addr.ip = ip;
            addr.port = ntohs(sin.sin_port);
        }
        return addr;
    }
    
    bool set_non_blocking(bool enabled) override {
        u_long mode = enabled ? 1 : 0;
        return ioctlsocket(m_socket, FIONBIO, &mode) == 0;
    }
    
    bool set_recv_timeout(int ms) override {
        DWORD timeout = ms;
        return setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, 
                         reinterpret_cast<char*>(&timeout), sizeof(timeout)) == 0;
    }
    
    bool set_send_timeout(int ms) override {
        DWORD timeout = ms;
        return setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, 
                         reinterpret_cast<char*>(&timeout), sizeof(timeout)) == 0;
    }
    
    int last_error() const override { return m_last_error; }
    
    std::string last_error_string() const override {
        char* msg = nullptr;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            nullptr, m_last_error, 0, reinterpret_cast<LPSTR>(&msg), 0, nullptr
        );
        std::string result = msg ? msg : "Unknown error";
        LocalFree(msg);
        return result;
    }
    
    Result<void> connect(const std::string& ip, uint16_t port, int timeout_ms) override {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            m_last_error = WSAEINVAL;
            return make_error(TELEPORT_ERROR_INVALID_ARGUMENT, "Invalid IP address");
        }
        
        // Set non-blocking for timeout support
        set_non_blocking(true);
        
        int ret = ::connect(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                m_last_error = err;
                return make_error(TELEPORT_ERROR_SOCKET_CONNECT, last_error_string());
            }
            
            // Wait for connection with timeout
            fd_set write_fds, except_fds;
            FD_ZERO(&write_fds);
            FD_ZERO(&except_fds);
            FD_SET(m_socket, &write_fds);
            FD_SET(m_socket, &except_fds);
            
            timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            
            ret = select(0, nullptr, &write_fds, &except_fds, &tv);
            if (ret == 0) {
                m_last_error = WSAETIMEDOUT;
                return make_error(TELEPORT_ERROR_TIMEOUT, "Connection timed out");
            }
            if (ret == SOCKET_ERROR || FD_ISSET(m_socket, &except_fds)) {
                int opt_err = 0;
                int opt_len = sizeof(opt_err);
                getsockopt(m_socket, SOL_SOCKET, SO_ERROR, 
                          reinterpret_cast<char*>(&opt_err), &opt_len);
                m_last_error = opt_err;
                return make_error(TELEPORT_ERROR_SOCKET_CONNECT, last_error_string());
            }
        }
        
        // Restore blocking mode
        set_non_blocking(false);
        return ok();
    }
    
    Result<void> bind(uint16_t port) override {
        // Enable address reuse
        int opt = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, 
                  reinterpret_cast<char*>(&opt), sizeof(opt));
        
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (::bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            m_last_error = WSAGetLastError();
            return make_error(TELEPORT_ERROR_SOCKET_BIND, last_error_string());
        }
        return ok();
    }
    
    Result<void> listen(int backlog) override {
        if (::listen(m_socket, backlog) == SOCKET_ERROR) {
            m_last_error = WSAGetLastError();
            return make_error(TELEPORT_ERROR_SOCKET_BIND, last_error_string());
        }
        return ok();
    }
    
    Result<std::unique_ptr<TcpSocket>> accept() override {
        sockaddr_in addr;
        int addr_len = sizeof(addr);
        SOCKET client = ::accept(m_socket, reinterpret_cast<sockaddr*>(&addr), &addr_len);
        if (client == INVALID_SOCKET) {
            m_last_error = WSAGetLastError();
            return make_error(TELEPORT_ERROR_SOCKET_RECV, last_error_string());
        }
        return std::unique_ptr<TcpSocket>(std::make_unique<WinTcpSocket>(client));
    }
    
    Result<size_t> send(const uint8_t* data, size_t len) override {
        int sent = ::send(m_socket, reinterpret_cast<const char*>(data), 
                         static_cast<int>(len), 0);
        if (sent == SOCKET_ERROR) {
            m_last_error = WSAGetLastError();
            return make_error(TELEPORT_ERROR_SOCKET_SEND, last_error_string());
        }
        return static_cast<size_t>(sent);
    }
    
    Result<size_t> recv(uint8_t* buffer, size_t len) override {
        int received = ::recv(m_socket, reinterpret_cast<char*>(buffer), 
                             static_cast<int>(len), 0);
        if (received == SOCKET_ERROR) {
            m_last_error = WSAGetLastError();
            return make_error(TELEPORT_ERROR_SOCKET_RECV, last_error_string());
        }
        if (received == 0) {
            // Connection closed
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
    SOCKET m_socket;
    mutable int m_last_error;
};

/* ============================================================================
 * Windows UDP Socket Implementation
 * ============================================================================ */

class WinUdpSocket : public UdpSocket {
public:
    WinUdpSocket() : m_socket(INVALID_SOCKET), m_last_error(0) {
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
    
    ~WinUdpSocket() override {
        close();
    }
    
    bool is_valid() const override { return m_socket != INVALID_SOCKET; }
    
    void close() override {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }
    
    SocketHandle handle() const override { 
        return static_cast<SocketHandle>(m_socket); 
    }
    
    NetworkAddress local_address() const override {
        NetworkAddress addr;
        sockaddr_in sin;
        int len = sizeof(sin);
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
        u_long mode = enabled ? 1 : 0;
        return ioctlsocket(m_socket, FIONBIO, &mode) == 0;
    }
    
    bool set_recv_timeout(int ms) override {
        DWORD timeout = ms;
        return setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, 
                         reinterpret_cast<char*>(&timeout), sizeof(timeout)) == 0;
    }
    
    bool set_send_timeout(int ms) override {
        DWORD timeout = ms;
        return setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, 
                         reinterpret_cast<char*>(&timeout), sizeof(timeout)) == 0;
    }
    
    int last_error() const override { return m_last_error; }
    
    std::string last_error_string() const override {
        char* msg = nullptr;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            nullptr, m_last_error, 0, reinterpret_cast<LPSTR>(&msg), 0, nullptr
        );
        std::string result = msg ? msg : "Unknown error";
        LocalFree(msg);
        return result;
    }
    
    Result<void> bind(uint16_t port) override {
        int opt = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, 
                  reinterpret_cast<char*>(&opt), sizeof(opt));
        
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (::bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            m_last_error = WSAGetLastError();
            return make_error(TELEPORT_ERROR_SOCKET_BIND, last_error_string());
        }
        return ok();
    }
    
    Result<void> enable_broadcast() override {
        int opt = 1;
        if (setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, 
                      reinterpret_cast<char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
            m_last_error = WSAGetLastError();
            return make_error(TELEPORT_ERROR_SOCKET_CREATE, last_error_string());
        }
        return ok();
    }
    
    Result<size_t> send_to(const uint8_t* data, size_t len, 
                           const std::string& ip, uint16_t port) override {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        
        int sent = ::sendto(m_socket, reinterpret_cast<const char*>(data), 
                           static_cast<int>(len), 0,
                           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (sent == SOCKET_ERROR) {
            m_last_error = WSAGetLastError();
            return make_error(TELEPORT_ERROR_SOCKET_SEND, last_error_string());
        }
        return static_cast<size_t>(sent);
    }
    
    Result<size_t> recv_from(uint8_t* buffer, size_t len, 
                             std::string& out_ip, uint16_t& out_port) override {
        sockaddr_in addr;
        int addr_len = sizeof(addr);
        
        int received = ::recvfrom(m_socket, reinterpret_cast<char*>(buffer), 
                                  static_cast<int>(len), 0,
                                  reinterpret_cast<sockaddr*>(&addr), &addr_len);
        if (received == SOCKET_ERROR) {
            m_last_error = WSAGetLastError();
            return make_error(TELEPORT_ERROR_SOCKET_RECV, last_error_string());
        }
        
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        out_ip = ip;
        out_port = ntohs(addr.sin_port);
        
        return static_cast<size_t>(received);
    }
    
private:
    SOCKET m_socket;
    mutable int m_last_error;
};

/* ============================================================================
 * Socket Factory Functions
 * ============================================================================ */

std::unique_ptr<TcpSocket> create_tcp_socket(const SocketOptions& opts) {
    auto sock = std::make_unique<WinTcpSocket>();
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
    auto sock = std::make_unique<WinUdpSocket>();
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
 * Windows File Implementation
 * ============================================================================ */

class WinFile : public File {
public:
    WinFile(const std::string& path, FileMode mode) 
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
    
    ~WinFile() override {
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
    auto file = std::make_unique<WinFile>(path, mode);
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
    } catch (const std::filesystem::filesystem_error& e) {
        // SECURITY: Log specific filesystem errors for debugging
        // (Logger not available in PAL, so just return 0)
        return 0;
    } catch (const std::exception&) {
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
    Sleep(ms);
}

int64_t timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

} // namespace pal
} // namespace teleport

#endif // TELEPORT_WINDOWS
