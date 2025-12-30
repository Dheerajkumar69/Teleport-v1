/**
 * @file hotspot.hpp
 * @brief Wi-Fi hotspot abstraction for peer-to-peer connections
 * 
 * Enables Teleport to work without existing Wi-Fi infrastructure
 * by creating a local hotspot on one device.
 */

#ifndef TELEPORT_HOTSPOT_HPP
#define TELEPORT_HOTSPOT_HPP

#include <string>
#include <memory>
#include <functional>
#include "teleport/types.h"

namespace teleport {

/**
 * @brief Hotspot configuration
 */
struct HotspotConfig {
    std::string ssid;           // Network name (auto-generated if empty)
    std::string password;       // Password (auto-generated if empty, min 8 chars)
    bool auto_shutdown = true;  // Shutdown when no clients for timeout
    int idle_timeout_ms = 60000; // Idle timeout in milliseconds
};

/**
 * @brief Hotspot status information
 */
struct HotspotInfo {
    std::string ssid;
    std::string password;
    std::string gateway_ip;     // IP address of hotspot host
    uint16_t control_port;      // Teleport control port on gateway
    bool is_active = false;
    int client_count = 0;
};

/**
 * @brief Client connected callback
 */
using OnClientConnected = std::function<void(const std::string& client_ip)>;
using OnClientDisconnected = std::function<void(const std::string& client_ip)>;

/**
 * @brief Abstract hotspot interface
 * 
 * Platform-specific implementations provide actual hotspot functionality.
 */
class Hotspot {
public:
    virtual ~Hotspot() = default;
    
    /**
     * @brief Create and start a Wi-Fi hotspot
     * @param config Hotspot configuration
     * @return HotspotInfo on success, error otherwise
     */
    virtual Result<HotspotInfo> create(const HotspotConfig& config) = 0;
    
    /**
     * @brief Stop and destroy the hotspot
     */
    virtual void destroy() = 0;
    
    /**
     * @brief Check if hotspot is currently active
     */
    virtual bool is_active() const = 0;
    
    /**
     * @brief Get current hotspot information
     */
    virtual HotspotInfo get_info() const = 0;
    
    /**
     * @brief Get the gateway IP address (for clients to connect to)
     */
    virtual std::string get_gateway_ip() const = 0;
    
    /**
     * @brief Set callback for client connections
     */
    virtual void set_client_callback(OnClientConnected on_connect, 
                                     OnClientDisconnected on_disconnect) = 0;
    
    /**
     * @brief Get list of connected client IPs
     */
    virtual std::vector<std::string> get_connected_clients() const = 0;
};

/**
 * @brief Create platform-specific hotspot instance
 */
std::unique_ptr<Hotspot> create_hotspot();

/**
 * @brief Detect if we're connected to a Teleport hotspot
 * @return Gateway IP if connected to hotspot, empty string otherwise
 */
std::string detect_hotspot_gateway();

/**
 * @brief Generate a random SSID for hotspot
 */
std::string generate_hotspot_ssid();

/**
 * @brief Generate a random password for hotspot
 */
std::string generate_hotspot_password();

} // namespace teleport

#endif // TELEPORT_HOTSPOT_HPP
