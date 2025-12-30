/**
 * @file device_list.hpp
 * @brief Thread-safe device list with TTL expiration
 */

#ifndef TELEPORT_DEVICE_LIST_HPP
#define TELEPORT_DEVICE_LIST_HPP

#include "teleport/types.h"
#include <vector>
#include <mutex>
#include <functional>
#include <unordered_map>

namespace teleport {

/**
 * @brief Thread-safe container for discovered devices with TTL expiration
 */
class DeviceList {
public:
    using DeviceCallback = std::function<void(const Device&)>;
    using DeviceLostCallback = std::function<void(const std::string&)>;
    
    explicit DeviceList(uint32_t ttl_ms = 5000);
    
    /**
     * @brief Add or update a device
     * @param device Device information
     * @return true if this is a new device, false if update
     */
    bool upsert(const Device& device);
    
    /**
     * @brief Remove expired devices
     * @return List of expired device IDs
     */
    std::vector<std::string> remove_expired();
    
    /**
     * @brief Get device by ID
     */
    std::optional<Device> get(const std::string& id) const;
    
    /**
     * @brief Get device by index (for CLI enumeration)
     */
    std::optional<Device> get_by_index(size_t index) const;
    
    /**
     * @brief Get current device count
     */
    size_t count() const;
    
    /**
     * @brief Get all devices (snapshot)
     */
    std::vector<Device> all() const;
    
    /**
     * @brief Check if a device exists and is active
     */
    bool contains(const std::string& id) const;
    
    /**
     * @brief Clear all devices
     */
    void clear();
    
    /**
     * @brief Set TTL for device expiration
     */
    void set_ttl(uint32_t ms) { m_ttl_ms = ms; }
    
private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Device> m_devices;
    std::vector<std::string> m_order;  // Insertion order for index access
    uint32_t m_ttl_ms;
};

} // namespace teleport

#endif // TELEPORT_DEVICE_LIST_HPP
