/**
 * @file discovery.cpp
 * @brief Discovery manager implementation
 */

#include "discovery.hpp"
#include "teleport/teleport.h"
#include "teleport/errors.h"
#include "utils/uuid.hpp"
#include "utils/logger.hpp"
#include "platform/pal.hpp"

namespace teleport {

Config Config::with_defaults() {
    Config config;
    config.device_name = pal::get_device_name();
    config.control_port = 0;  // Auto-select
    config.chunk_size = TELEPORT_CHUNK_SIZE;
    config.parallel_streams = TELEPORT_PARALLEL_STREAMS;
    config.discovery_interval_ms = TELEPORT_DISCOVERY_INTERVAL;
    config.device_ttl_ms = TELEPORT_DEVICE_TTL;
    config.download_path = ".";
    return config;
}

DiscoveryManager::DiscoveryManager(const Config& config)
    : m_config(config)
    , m_devices(config.device_ttl_ms)
    , m_broadcaster(TELEPORT_DISCOVERY_PORT)
    , m_listener(TELEPORT_DISCOVERY_PORT)
    , m_running(false) {
    
    // Initialize our device info
    m_self_device.id = generate_uuid();
    m_self_device.name = config.device_name;
    m_self_device.os = pal::get_os_type();
    m_self_device.address.ip = pal::get_primary_local_ip();
    m_self_device.address.port = config.control_port;
    m_self_device.capabilities = Capability::Default;
    
    // Detect if we're on a hotspot network
    std::string gateway = detect_hotspot_gateway();
    if (!gateway.empty()) {
        m_hotspot_mode = true;
        m_hotspot_gateway = gateway;
        LOG_INFO("Hotspot mode detected, gateway: ", gateway);
    }
    
    LOG_DEBUG("Self device: ", m_self_device.name, " (", m_self_device.id.substr(0, 8), ")");
    LOG_DEBUG("Local IP: ", m_self_device.address.ip);
}

DiscoveryManager::~DiscoveryManager() {
    stop();
}

Result<void> DiscoveryManager::start(OnDeviceFound on_found, OnDeviceLost on_lost) {
    if (m_running.load()) {
        return make_error(TELEPORT_ERROR_ALREADY_RUNNING, "Discovery already running");
    }
    
    m_on_found = std::move(on_found);
    m_on_lost = std::move(on_lost);
    
    // Set self ID to filter our own broadcasts
    m_listener.set_self_id(m_self_device.id);
    
    // Start listener first
    auto listen_result = m_listener.start([this](const Device& d) {
        on_device_received(d);
    });
    if (!listen_result) {
        return listen_result.error();
    }
    
    // Start broadcaster
    auto broadcast_result = m_broadcaster.start(m_self_device, m_config.discovery_interval_ms);
    if (!broadcast_result) {
        m_listener.stop();
        return broadcast_result.error();
    }
    
    // Start expiration thread
    m_running.store(true);
    m_expiration_thread = std::thread(&DiscoveryManager::expiration_loop, this);
    
    LOG_INFO("Discovery started");
    return ok();
}

void DiscoveryManager::stop() {
    if (m_running.load()) {
        m_running.store(false);
        
        m_broadcaster.stop();
        m_listener.stop();
        
        if (m_expiration_thread.joinable()) {
            m_expiration_thread.join();
        }
        
        m_devices.clear();
        LOG_INFO("Discovery stopped");
    }
}

void DiscoveryManager::broadcast_now() {
    if (m_broadcaster.is_running()) {
        m_self_device.last_seen_ms = now_ms();
        m_broadcaster.broadcast_once(m_self_device);
    }
}

void DiscoveryManager::on_device_received(const Device& device) {
    bool is_new = m_devices.upsert(device);
    
    if (is_new && m_on_found) {
        m_on_found(device);
    }
}

void DiscoveryManager::expiration_loop() {
    while (m_running.load()) {
        // Check for expired devices every second
        pal::sleep_ms(1000);
        
        if (!m_running.load()) break;
        
        auto expired = m_devices.remove_expired();
        
        if (m_on_lost) {
            for (const auto& id : expired) {
                m_on_lost(id);
            }
        }
    }
}

std::string DiscoveryManager::detect_hotspot_gateway() {
    // Placeholder: Hotspot gateway detection for Phase 3
    // For now, return empty string (no hotspot mode)
    // Full implementation would check network interface for hotspot patterns
    return "";
}

} // namespace teleport
