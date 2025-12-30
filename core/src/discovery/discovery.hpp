/**
 * @file discovery.hpp
 * @brief High-level discovery manager
 */

#ifndef TELEPORT_DISCOVERY_HPP
#define TELEPORT_DISCOVERY_HPP

#include "teleport/types.h"
#include "device_list.hpp"
#include "udp_broadcaster.hpp"
#include <atomic>
#include <thread>
#include <functional>

namespace teleport {

/**
 * @brief Callback types for discovery events
 */
using OnDeviceFound = std::function<void(const Device&)>;
using OnDeviceLost = std::function<void(const std::string&)>;

/**
 * @brief High-level discovery manager that coordinates broadcasting and listening
 */
class DiscoveryManager {
public:
    DiscoveryManager(const Config& config);
    ~DiscoveryManager();
    
    /**
     * @brief Start device discovery
     * @param on_found Callback when a new device is found
     * @param on_lost Callback when a device expires (optional)
     * @return Success result
     */
    Result<void> start(OnDeviceFound on_found, OnDeviceLost on_lost = nullptr);
    
    /**
     * @brief Stop discovery
     */
    void stop();
    
    /**
     * @brief Check if discovery is running
     */
    bool is_running() const { return m_running.load(); }
    
    /**
     * @brief Get our device information
     */
    const Device& self() const { return m_self_device; }
    
    /**
     * @brief Get device list
     */
    const DeviceList& devices() const { return m_devices; }
    DeviceList& devices() { return m_devices; }
    
    /**
     * @brief Force a broadcast now
     */
    void broadcast_now();
    
private:
    void expiration_loop();
    void on_device_received(const Device& device);
    std::string detect_hotspot_gateway();
    
    Config m_config;
    Device m_self_device;
    DeviceList m_devices;
    
    UdpBroadcaster m_broadcaster;
    UdpListener m_listener;
    
    std::atomic<bool> m_running;
    std::thread m_expiration_thread;
    
    OnDeviceFound m_on_found;
    OnDeviceLost m_on_lost;
    
    // Hotspot mode detection
    bool m_hotspot_mode = false;
    std::string m_hotspot_gateway;
};

} // namespace teleport

#endif // TELEPORT_DISCOVERY_HPP
