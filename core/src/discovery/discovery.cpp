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

#ifdef _WIN32
#include <iphlpapi.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#elif defined(__APPLE__)
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

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

void DiscoveryManager::set_control_port(uint16_t port) {
    m_self_device.address.port = port;
    LOG_INFO("Updated control port to ", port);
    
    // Immediately broadcast with updated port so other devices know where to connect
    broadcast_now();
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
#ifdef _WIN32
    // Windows implementation using IP Helper API
    // Check for common hotspot gateway patterns (192.168.43.x for Android, 172.20.10.x for iOS)
    ULONG size = 0;
    GetAdaptersInfo(nullptr, &size);
    if (size == 0) return "";

    std::vector<uint8_t> buffer(size);
    PIP_ADAPTER_INFO adapters = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());

    if (GetAdaptersInfo(adapters, &size) != NO_ERROR) {
        return "";
    }

    for (PIP_ADAPTER_INFO adapter = adapters; adapter; adapter = adapter->Next) {
        std::string gateway = adapter->GatewayList.IpAddress.String;

        // Check for common mobile hotspot patterns
        if (gateway.find("192.168.43.") == 0 ||   // Android hotspot
            gateway.find("172.20.10.") == 0 ||    // iOS hotspot
            gateway.find("192.168.137.") == 0) {  // Windows hotspot
            LOG_DEBUG("Detected hotspot gateway: ", gateway);
            return gateway;
        }
    }

#elif defined(__linux__)
    // BUG FIX (Bug 6): Linux implementation via /proc/net/route.
    // Each line: Iface  Dest  Gateway  Flags  ...
    // Dest==0 means the default route; Gateway is the default gateway (little-endian hex).
    FILE* fp = fopen("/proc/net/route", "r");
    if (!fp) return "";

    char line[512];
    // Skip header line
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return ""; }

    while (fgets(line, sizeof(line), fp)) {
        char iface[64];
        unsigned long dest = 0, gw = 0, flags = 0;
        if (sscanf(line, "%63s %lX %lX %lX", iface, &dest, &gw, &flags) < 4)
            continue;
        if (dest != 0) continue;  // Only the default route (destination 0.0.0.0)
        if (gw == 0) continue;    // Skip entries with no gateway

        struct in_addr addr;
        addr.s_addr = static_cast<in_addr_t>(gw);
        std::string gateway = inet_ntoa(addr);

        if (gateway.find("192.168.43.") == 0 ||
            gateway.find("172.20.10.") == 0 ||
            gateway.find("192.168.137.") == 0) {
            LOG_DEBUG("Detected hotspot gateway (Linux): ", gateway);
            fclose(fp);
            return gateway;
        }
    }
    fclose(fp);

#elif defined(__APPLE__)
    // BUG FIX (Bug 6): macOS — use getifaddrs to find relevant interface addresses.
    // (macOS doesn't expose /proc/net/route; iterate interfaces and match subnets.)
    struct ifaddrs* ifa_list = nullptr;
    if (getifaddrs(&ifa_list) != 0) return "";

    std::string result;
    for (struct ifaddrs* ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        char buf[INET_ADDRSTRLEN];
        const void* sinaddr = &reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr)->sin_addr;
        if (!inet_ntop(AF_INET, sinaddr, buf, sizeof(buf))) continue;
        std::string ip = buf;
        if (ip.find("192.168.43.") == 0 ||
            ip.find("172.20.10.") == 0 ||
            ip.find("192.168.137.") == 0) {
            LOG_DEBUG("Detected hotspot interface (macOS): ", ip);
            result = ip;
            break;
        }
    }
    freeifaddrs(ifa_list);
    return result;
#endif
    return "";  // No hotspot detected
}

} // namespace teleport
