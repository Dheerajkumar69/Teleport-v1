/**
 * @file wifi_direct.hpp
 * @brief Wi-Fi Direct / Wi-Fi P2P abstraction for peer-to-peer connections
 * 
 * Enables direct device-to-device file transfer without requiring
 * an existing Wi-Fi network or router.
 * 
 * Platform Support:
 * - Windows 10 1803+ (Wi-Fi Direct via WinRT)
 * - Android 4.0+ (Wi-Fi P2P via WifiP2pManager)
 */

#ifndef TELEPORT_WIFI_DIRECT_HPP
#define TELEPORT_WIFI_DIRECT_HPP

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include "teleport/types.h"

namespace teleport {

/* ============================================================================
 * WiFi Direct Types
 * ============================================================================ */

/**
 * @brief WiFi Direct peer device information
 */
struct WifiDirectPeer {
    std::string mac_address;        // Device MAC address (unique identifier)
    std::string device_name;        // User-friendly device name
    std::string device_type;        // Device category (e.g., "Computer", "Phone")
    int signal_strength = 0;        // Signal strength in dBm (0 if unknown)
    bool is_group_owner = false;    // True if peer is WiFi Direct group owner
    int64_t last_seen_ms = 0;       // Timestamp of last discovery
    
    bool operator==(const WifiDirectPeer& other) const {
        return mac_address == other.mac_address;
    }
};

/**
 * @brief WiFi Direct connection state
 */
enum class WifiDirectState {
    Disabled,           // WiFi Direct not available or disabled
    Idle,               // Ready but not discovering/connected
    Discovering,        // Actively scanning for peers
    Connecting,         // Connection in progress
    Connected,          // Connected to a peer
    GroupOwner,         // Acting as group owner (others connect to us)
    Failed              // Operation failed
};

/**
 * @brief WiFi Direct connection info (available after connection)
 */
struct WifiDirectConnection {
    std::string peer_mac;           // Connected peer's MAC
    std::string peer_name;          // Connected peer's name
    std::string group_owner_ip;     // IP of the group owner
    std::string local_ip;           // Our IP in the P2P network
    bool is_group_owner = false;    // Are we the group owner?
    std::string passphrase;         // Group passphrase (if GO)
};

/* ============================================================================
 * Callbacks
 * ============================================================================ */

using OnWifiDirectPeerFound = std::function<void(const WifiDirectPeer&)>;
using OnWifiDirectPeerLost = std::function<void(const std::string& mac)>;
using OnWifiDirectConnected = std::function<void(const WifiDirectConnection&)>;
using OnWifiDirectDisconnected = std::function<void(const std::string& mac)>;
using OnWifiDirectError = std::function<void(int error_code, const std::string& message)>;
using OnWifiDirectStateChanged = std::function<void(WifiDirectState state)>;

/* ============================================================================
 * WiFi Direct Interface
 * ============================================================================ */

/**
 * @brief Abstract WiFi Direct interface
 * 
 * Platform-specific implementations provide actual WiFi Direct functionality.
 * After successful connection, the group owner's IP can be used for TCP transfer.
 */
class WifiDirect {
public:
    virtual ~WifiDirect() = default;
    
    /**
     * @brief Check if WiFi Direct is available on this device
     * @return true if WiFi Direct can be used
     */
    virtual bool is_available() const = 0;
    
    /**
     * @brief Get current WiFi Direct state
     */
    virtual WifiDirectState state() const = 0;
    
    /**
     * @brief Start peer discovery
     * @param on_found Callback when a new peer is discovered
     * @param on_lost Callback when a peer is no longer visible (optional)
     * @return Success result
     * 
     * Discovery continues until stop_discovery() is called or an error occurs.
     */
    virtual Result<void> start_discovery(
        OnWifiDirectPeerFound on_found,
        OnWifiDirectPeerLost on_lost = nullptr
    ) = 0;
    
    /**
     * @brief Stop peer discovery
     */
    virtual void stop_discovery() = 0;
    
    /**
     * @brief Connect to a discovered peer
     * @param mac_address MAC address of the peer to connect to
     * @param on_connected Callback when connection succeeds
     * @param on_error Callback on connection failure
     * @return Success result (async - callbacks indicate final status)
     * 
     * WiFi Direct automatically negotiates group owner. After connection,
     * use get_connection_info() to get the IP for TCP transfer.
     */
    virtual Result<void> connect(
        const std::string& mac_address,
        OnWifiDirectConnected on_connected,
        OnWifiDirectError on_error = nullptr
    ) = 0;
    
    /**
     * @brief Disconnect from current peer
     */
    virtual void disconnect() = 0;
    
    /**
     * @brief Get current connection information
     * @return Connection info if connected, nullopt otherwise
     */
    virtual std::optional<WifiDirectConnection> get_connection_info() const = 0;
    
    /**
     * @brief Get list of currently discovered peers
     */
    virtual std::vector<WifiDirectPeer> get_peers() const = 0;
    
    /**
     * @brief Start advertising (makes this device discoverable)
     * @return Success result
     * 
     * Some platforms require explicit advertising to be discoverable.
     */
    virtual Result<void> start_advertising() = 0;
    
    /**
     * @brief Stop advertising
     */
    virtual void stop_advertising() = 0;
    
    /**
     * @brief Set callback for disconnection events
     */
    virtual void set_disconnect_callback(OnWifiDirectDisconnected callback) = 0;
    
    /**
     * @brief Set callback for state changes
     */
    virtual void set_state_callback(OnWifiDirectStateChanged callback) = 0;
    
    /**
     * @brief Cancel any pending connection attempt
     */
    virtual void cancel_connect() = 0;
};

/* ============================================================================
 * Factory Function
 * ============================================================================ */

/**
 * @brief Create platform-specific WiFi Direct instance
 * @return unique_ptr to WiFi Direct implementation, or nullptr if not supported
 */
std::unique_ptr<WifiDirect> create_wifi_direct();

/**
 * @brief Check if WiFi Direct is supported on this platform
 * @return true if WiFi Direct can be created
 */
bool is_wifi_direct_supported();

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Convert WiFi Direct state to string
 */
inline std::string wifi_direct_state_to_string(WifiDirectState state) {
    switch (state) {
        case WifiDirectState::Disabled: return "Disabled";
        case WifiDirectState::Idle: return "Idle";
        case WifiDirectState::Discovering: return "Discovering";
        case WifiDirectState::Connecting: return "Connecting";
        case WifiDirectState::Connected: return "Connected";
        case WifiDirectState::GroupOwner: return "GroupOwner";
        case WifiDirectState::Failed: return "Failed";
        default: return "Unknown";
    }
}

} // namespace teleport

#endif // TELEPORT_WIFI_DIRECT_HPP
