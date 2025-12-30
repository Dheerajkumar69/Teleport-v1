/**
 * @file device_list.cpp
 * @brief Device list implementation
 */

#include "device_list.hpp"
#include "utils/logger.hpp"
#include <algorithm>

namespace teleport {

DeviceList::DeviceList(uint32_t ttl_ms) : m_ttl_ms(ttl_ms) {}

bool DeviceList::upsert(const Device& device) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_devices.find(device.id);
    bool is_new = (it == m_devices.end());
    
    if (is_new) {
        m_devices[device.id] = device;
        m_order.push_back(device.id);
        LOG_DEBUG("New device discovered: ", device.name, " (", device.id.substr(0, 8), ")");
    } else {
        // Update existing device
        it->second = device;
    }
    
    return is_new;
}

std::vector<std::string> DeviceList::remove_expired() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> expired;
    
    auto now = now_ms();
    
    for (auto it = m_devices.begin(); it != m_devices.end(); ) {
        if (it->second.is_expired(m_ttl_ms)) {
            expired.push_back(it->first);
            LOG_DEBUG("Device expired: ", it->second.name, " (", it->first.substr(0, 8), ")");
            it = m_devices.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clean up order list
    if (!expired.empty()) {
        m_order.erase(
            std::remove_if(m_order.begin(), m_order.end(), 
                [this](const std::string& id) {
                    return m_devices.find(id) == m_devices.end();
                }),
            m_order.end()
        );
    }
    
    return expired;
}

std::optional<Device> DeviceList::get(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_devices.find(id);
    if (it != m_devices.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<Device> DeviceList::get_by_index(size_t index) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_order.size()) {
        return std::nullopt;
    }
    auto it = m_devices.find(m_order[index]);
    if (it != m_devices.end()) {
        return it->second;
    }
    return std::nullopt;
}

size_t DeviceList::count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_devices.size();
}

std::vector<Device> DeviceList::all() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Device> result;
    result.reserve(m_order.size());
    
    for (const auto& id : m_order) {
        auto it = m_devices.find(id);
        if (it != m_devices.end()) {
            result.push_back(it->second);
        }
    }
    
    return result;
}

bool DeviceList::contains(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_devices.find(id);
    return it != m_devices.end() && !it->second.is_expired(m_ttl_ms);
}

void DeviceList::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_devices.clear();
    m_order.clear();
}

} // namespace teleport
