/**
 * @file udp_broadcaster.cpp
 * @brief UDP broadcaster implementation
 */

#include "udp_broadcaster.hpp"
#include "teleport/teleport.h"
#include "teleport/errors.h"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>

namespace teleport {

using json = nlohmann::json;

/* ============================================================================
 * UdpBroadcaster Implementation
 * ============================================================================ */

UdpBroadcaster::UdpBroadcaster(uint16_t port)
    : m_port(port)
    , m_running(false)
    , m_interval_ms(1000) {
}

UdpBroadcaster::~UdpBroadcaster() {
    stop();
}

Result<void> UdpBroadcaster::start(const Device& device, uint32_t interval_ms) {
    if (m_running.load()) {
        return make_error(TELEPORT_ERROR_ALREADY_RUNNING, "Broadcaster already running");
    }
    
    // Create UDP socket with broadcast enabled
    pal::SocketOptions opts;
    opts.broadcast = true;
    m_socket = pal::create_udp_socket(opts);
    
    if (!m_socket || !m_socket->is_valid()) {
        return make_error(TELEPORT_ERROR_SOCKET_CREATE, "Failed to create UDP socket");
    }
    
    m_device = device;
    m_interval_ms = interval_ms;
    m_broadcast_addr = pal::get_broadcast_address();
    
    m_running.store(true);
    m_thread = std::thread(&UdpBroadcaster::broadcast_loop, this);
    
    LOG_INFO("Started UDP broadcaster on port ", m_port, " -> ", m_broadcast_addr);
    return ok();
}

void UdpBroadcaster::stop() {
    if (m_running.load()) {
        m_running.store(false);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_socket) {
            m_socket->close();
            m_socket.reset();
        }
        LOG_INFO("Stopped UDP broadcaster");
    }
}

Result<void> UdpBroadcaster::broadcast_once(const Device& device) {
    if (!m_socket || !m_socket->is_valid()) {
        return make_error(TELEPORT_ERROR_NOT_RUNNING, "Broadcaster not started");
    }
    
    std::string packet = serialize_device(device);
    auto result = m_socket->send_to(
        reinterpret_cast<const uint8_t*>(packet.data()),
        packet.size(),
        m_broadcast_addr,
        m_port
    );
    
    if (!result) {
        return result.error();
    }
    
    return ok();
}

void UdpBroadcaster::broadcast_loop() {
    while (m_running.load()) {
        // Update timestamp
        m_device.last_seen_ms = now_ms();
        
        auto result = broadcast_once(m_device);
        if (!result) {
            LOG_WARN("Broadcast failed: ", result.error().message);
        }
        
        // Sleep in small increments for responsive shutdown
        for (uint32_t elapsed = 0; elapsed < m_interval_ms && m_running.load(); elapsed += 100) {
            pal::sleep_ms(100);
        }
    }
}

std::string UdpBroadcaster::serialize_device(const Device& device) {
    json j;
    j["v"] = TELEPORT_PROTOCOL_VERSION;
    j["id"] = device.id;
    j["name"] = device.name;
    j["os"] = os_to_string(device.os);
    j["ip"] = device.address.ip;
    j["port"] = device.address.port;
    
    // Capabilities as array of strings
    json caps = json::array();
    if (has_capability(device.capabilities, Capability::Parallel)) {
        caps.push_back("parallel");
    }
    if (has_capability(device.capabilities, Capability::Resume)) {
        caps.push_back("resume");
    }
    if (has_capability(device.capabilities, Capability::Compress)) {
        caps.push_back("compress");
    }
    if (has_capability(device.capabilities, Capability::Encrypt)) {
        caps.push_back("encrypt");
    }
    j["caps"] = caps;
    
    return j.dump();
}

/* ============================================================================
 * UdpListener Implementation
 * ============================================================================ */

UdpListener::UdpListener(uint16_t port)
    : m_port(port)
    , m_running(false) {
}

UdpListener::~UdpListener() {
    stop();
}

Result<void> UdpListener::start(DiscoveryPacketCallback callback) {
    if (m_running.load()) {
        return make_error(TELEPORT_ERROR_ALREADY_RUNNING, "Listener already running");
    }
    
    m_callback = std::move(callback);
    
    // Create and bind UDP socket
    pal::SocketOptions opts;
    opts.reuse_addr = true;
    opts.recv_timeout_ms = 500;  // Allow periodic check for shutdown
    m_socket = pal::create_udp_socket(opts);
    
    if (!m_socket || !m_socket->is_valid()) {
        return make_error(TELEPORT_ERROR_SOCKET_CREATE, "Failed to create UDP socket");
    }
    
    auto bind_result = m_socket->bind(m_port);
    if (!bind_result) {
        return bind_result.error();
    }
    
    m_running.store(true);
    m_thread = std::thread(&UdpListener::listen_loop, this);
    
    LOG_INFO("Started UDP listener on port ", m_port);
    return ok();
}

void UdpListener::stop() {
    if (m_running.load()) {
        m_running.store(false);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_socket) {
            m_socket->close();
            m_socket.reset();
        }
        LOG_INFO("Stopped UDP listener");
    }
}

void UdpListener::listen_loop() {
    std::vector<uint8_t> buffer(1024);  // Max packet size
    
    while (m_running.load()) {
        std::string sender_ip;
        uint16_t sender_port;
        
        auto result = m_socket->recv_from(
            buffer.data(), buffer.size(),
            sender_ip, sender_port
        );
        
        if (!result) {
            // Timeout is expected, just continue
            continue;
        }
        
        size_t len = *result;
        if (len == 0) continue;
        
        auto device = parse_packet(buffer.data(), len, sender_ip);
        if (device && m_callback) {
            // Filter self-discovery
            if (device->id != m_self_id) {
                m_callback(*device);
            }
        }
    }
}

std::optional<Device> UdpListener::parse_packet(const uint8_t* data, size_t len,
                                                  const std::string& sender_ip) {
    try {
        std::string packet_str(reinterpret_cast<const char*>(data), len);
        json j = json::parse(packet_str);
        
        // Validate protocol version
        int version = j.value("v", 0);
        if (version != TELEPORT_PROTOCOL_VERSION) {
            LOG_DEBUG("Ignoring packet with version ", version);
            return std::nullopt;
        }
        
        Device device;
        device.id = j.at("id").get<std::string>();
        device.name = j.at("name").get<std::string>();
        device.os = os_from_string(j.at("os").get<std::string>());
        
        // Use sender IP if not specified (more reliable)
        device.address.ip = j.value("ip", sender_ip);
        if (device.address.ip.empty() || device.address.ip == "0.0.0.0") {
            device.address.ip = sender_ip;
        }
        
        device.address.port = j.at("port").get<uint16_t>();
        device.last_seen_ms = now_ms();
        
        // Parse capabilities
        device.capabilities = Capability::None;
        if (j.contains("caps") && j["caps"].is_array()) {
            for (const auto& cap : j["caps"]) {
                std::string c = cap.get<std::string>();
                if (c == "parallel") device.capabilities = device.capabilities | Capability::Parallel;
                if (c == "resume") device.capabilities = device.capabilities | Capability::Resume;
                if (c == "compress") device.capabilities = device.capabilities | Capability::Compress;
                if (c == "encrypt") device.capabilities = device.capabilities | Capability::Encrypt;
            }
        }
        
        return device;
        
    } catch (const json::exception& e) {
        LOG_DEBUG("Failed to parse discovery packet: ", e.what());
        return std::nullopt;
    }
}

} // namespace teleport
