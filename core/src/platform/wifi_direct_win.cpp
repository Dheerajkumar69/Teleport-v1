/**
 * @file wifi_direct_win.cpp
 * @brief Windows WiFi Direct implementation using WinRT APIs
 * 
 * Uses Windows.Devices.WiFiDirect for peer discovery and connection.
 * Requires Windows 10 1803+ and a compatible WiFi adapter.
 * 
 * Build Requirements:
 * - Link against: WindowsApp.lib (or use /ZW for UWP)
 * - C++17 or later
 */

#include "wifi_direct.hpp"

#ifdef _WIN32

#include "pal.hpp"
#include "teleport/errors.h"
#include "../utils/logger.hpp"

// Conditionally include WinRT headers
// Note: For MinGW/GCC builds, we provide a fallback stub implementation
#if defined(_MSC_VER) && defined(WINRT_AVAILABLE)

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.WiFiDirect.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Devices::WiFiDirect;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Foundation;
using namespace Windows::Networking;

namespace teleport {

/**
 * @brief Windows WiFi Direct implementation using WinRT
 */
class WindowsWifiDirect : public WifiDirect {
public:
    WindowsWifiDirect() {
        winrt::init_apartment();
        m_state = WifiDirectState::Idle;
    }
    
    ~WindowsWifiDirect() override {
        disconnect();
        stop_discovery();
        stop_advertising();
    }
    
    bool is_available() const override {
        try {
            // Check if WiFi Direct is supported
            auto selector = WiFiDirectDevice::GetDeviceSelector();
            return !selector.empty();
        } catch (...) {
            return false;
        }
    }
    
    WifiDirectState state() const override {
        return m_state.load();
    }
    
    Result<void> start_discovery(
        OnWifiDirectPeerFound on_found,
        OnWifiDirectPeerLost on_lost
    ) override {
        if (m_state == WifiDirectState::Discovering) {
            return Error{-1, "Already discovering"};
        }
        
        try {
            m_on_peer_found = std::move(on_found);
            m_on_peer_lost = std::move(on_lost);
            
            // Create device watcher for WiFi Direct devices
            auto selector = WiFiDirectDevice::GetDeviceSelector(
                WiFiDirectDeviceSelectorType::AssociationEndpoint);
            
            m_watcher = DeviceInformation::CreateWatcher(selector);
            
            m_watcher.Added([this](DeviceWatcher sender, DeviceInformation info) {
                WifiDirectPeer peer;
                peer.mac_address = winrt::to_string(info.Id());
                peer.device_name = winrt::to_string(info.Name());
                peer.last_seen_ms = now_ms();
                
                {
                    std::lock_guard<std::mutex> lock(m_peers_mutex);
                    m_peers[peer.mac_address] = peer;
                }
                
                if (m_on_peer_found) {
                    m_on_peer_found(peer);
                }
            });
            
            m_watcher.Removed([this](DeviceWatcher sender, DeviceInformationUpdate info) {
                std::string mac = winrt::to_string(info.Id());
                
                {
                    std::lock_guard<std::mutex> lock(m_peers_mutex);
                    m_peers.erase(mac);
                }
                
                if (m_on_peer_lost) {
                    m_on_peer_lost(mac);
                }
            });
            
            m_watcher.Updated([this](DeviceWatcher sender, DeviceInformationUpdate info) {
                std::string mac = winrt::to_string(info.Id());
                std::lock_guard<std::mutex> lock(m_peers_mutex);
                if (m_peers.count(mac)) {
                    m_peers[mac].last_seen_ms = now_ms();
                }
            });
            
            m_watcher.Start();
            set_state(WifiDirectState::Discovering);
            
            LOG_INFO("WiFi Direct discovery started");
            return {};
            
        } catch (const winrt::hresult_error& e) {
            return Error{e.code(), winrt::to_string(e.message())};
        }
    }
    
    void stop_discovery() override {
        if (m_watcher) {
            try {
                m_watcher.Stop();
            } catch (...) {}
            m_watcher = nullptr;
        }
        
        if (m_state == WifiDirectState::Discovering) {
            set_state(WifiDirectState::Idle);
        }
        
        LOG_INFO("WiFi Direct discovery stopped");
    }
    
    Result<void> connect(
        const std::string& mac_address,
        OnWifiDirectConnected on_connected,
        OnWifiDirectError on_error
    ) override {
        if (m_state == WifiDirectState::Connecting || 
            m_state == WifiDirectState::Connected) {
            return Error{-1, "Already connected or connecting"};
        }
        
        m_on_connected = std::move(on_connected);
        m_on_error = std::move(on_error);
        
        set_state(WifiDirectState::Connecting);
        
        // Connect asynchronously
        auto async_op = WiFiDirectDevice::FromIdAsync(winrt::to_hstring(mac_address));
        
        async_op.Completed([this, mac_address](auto&& op, AsyncStatus status) {
            if (status == AsyncStatus::Completed) {
                try {
                    m_device = op.GetResults();
                    
                    // Get connection info
                    auto endpoints = m_device.GetConnectionEndpointPairs();
                    if (endpoints.Size() > 0) {
                        auto endpoint = endpoints.GetAt(0);
                        
                        WifiDirectConnection conn;
                        conn.peer_mac = mac_address;
                        conn.peer_name = winrt::to_string(m_device.DeviceId());
                        conn.group_owner_ip = winrt::to_string(
                            endpoint.RemoteHostName().DisplayName());
                        conn.is_group_owner = false;  // We connected, so we're client
                        
                        {
                            std::lock_guard<std::mutex> lock(m_conn_mutex);
                            m_connection = conn;
                        }
                        
                        set_state(WifiDirectState::Connected);
                        
                        if (m_on_connected) {
                            m_on_connected(conn);
                        }
                        
                        LOG_INFO("WiFi Direct connected to: ", conn.group_owner_ip);
                    }
                } catch (const winrt::hresult_error& e) {
                    handle_error(e.code(), winrt::to_string(e.message()));
                }
            } else {
                handle_error(-1, "Connection failed");
            }
        });
        
        return {};
    }
    
    void disconnect() override {
        if (m_device) {
            m_device.Close();
            m_device = nullptr;
        }
        
        {
            std::lock_guard<std::mutex> lock(m_conn_mutex);
            if (m_connection && m_on_disconnected) {
                m_on_disconnected(m_connection->peer_mac);
            }
            m_connection.reset();
        }
        
        set_state(WifiDirectState::Idle);
        LOG_INFO("WiFi Direct disconnected");
    }
    
    std::optional<WifiDirectConnection> get_connection_info() const override {
        std::lock_guard<std::mutex> lock(m_conn_mutex);
        return m_connection;
    }
    
    std::vector<WifiDirectPeer> get_peers() const override {
        std::lock_guard<std::mutex> lock(m_peers_mutex);
        std::vector<WifiDirectPeer> result;
        for (const auto& [mac, peer] : m_peers) {
            result.push_back(peer);
        }
        return result;
    }
    
    Result<void> start_advertising() override {
        try {
            m_advertiser = WiFiDirectAdvertisement();
            m_advertiser.IsAutonomousGroupOwnerEnabled(true);
            
            auto publisher = WiFiDirectAdvertisementPublisher();
            publisher.Advertisement(m_advertiser);
            publisher.Start();
            
            m_publisher = publisher;
            
            LOG_INFO("WiFi Direct advertising started");
            return {};
            
        } catch (const winrt::hresult_error& e) {
            return Error{e.code(), winrt::to_string(e.message())};
        }
    }
    
    void stop_advertising() override {
        if (m_publisher) {
            try {
                m_publisher.Stop();
            } catch (...) {}
            m_publisher = nullptr;
        }
    }
    
    void set_disconnect_callback(OnWifiDirectDisconnected callback) override {
        m_on_disconnected = std::move(callback);
    }
    
    void set_state_callback(OnWifiDirectStateChanged callback) override {
        m_on_state_changed = std::move(callback);
    }
    
    void cancel_connect() override {
        // Cancel by disconnecting
        if (m_state == WifiDirectState::Connecting) {
            disconnect();
        }
    }

private:
    void set_state(WifiDirectState new_state) {
        m_state = new_state;
        if (m_on_state_changed) {
            m_on_state_changed(new_state);
        }
    }
    
    void handle_error(int code, const std::string& message) {
        set_state(WifiDirectState::Failed);
        if (m_on_error) {
            m_on_error(code, message);
        }
        LOG_ERROR("WiFi Direct error: ", message);
    }
    
    std::atomic<WifiDirectState> m_state{WifiDirectState::Disabled};
    
    // Discovery
    DeviceWatcher m_watcher{nullptr};
    mutable std::mutex m_peers_mutex;
    std::unordered_map<std::string, WifiDirectPeer> m_peers;
    
    // Connection
    WiFiDirectDevice m_device{nullptr};
    mutable std::mutex m_conn_mutex;
    std::optional<WifiDirectConnection> m_connection;
    
    // Advertising
    WiFiDirectAdvertisement m_advertiser{nullptr};
    WiFiDirectAdvertisementPublisher m_publisher{nullptr};
    
    // Callbacks
    OnWifiDirectPeerFound m_on_peer_found;
    OnWifiDirectPeerLost m_on_peer_lost;
    OnWifiDirectConnected m_on_connected;
    OnWifiDirectDisconnected m_on_disconnected;
    OnWifiDirectError m_on_error;
    OnWifiDirectStateChanged m_on_state_changed;
};

std::unique_ptr<WifiDirect> create_wifi_direct() {
    return std::make_unique<WindowsWifiDirect>();
}

bool is_wifi_direct_supported() {
    try {
        auto selector = WiFiDirectDevice::GetDeviceSelector();
        return !selector.empty();
    } catch (...) {
        return false;
    }
}

} // namespace teleport

#else // Non-WinRT fallback (MinGW/GCC builds)

namespace teleport {

/**
 * @brief Stub WiFi Direct implementation for non-WinRT builds
 * 
 * WiFi Direct requires WinRT which is not available in MinGW.
 * This stub allows the code to compile and provides runtime
 * feedback that WiFi Direct is not supported.
 */
class StubWifiDirect : public WifiDirect {
public:
    bool is_available() const override { return false; }
    WifiDirectState state() const override { return WifiDirectState::Disabled; }
    
    Result<void> start_discovery(OnWifiDirectPeerFound, OnWifiDirectPeerLost) override {
        return Error{TELEPORT_ERROR_NOT_SUPPORTED, 
            "WiFi Direct not supported on this build (requires MSVC with WinRT)"};
    }
    
    void stop_discovery() override {}
    
    Result<void> connect(const std::string&, OnWifiDirectConnected, OnWifiDirectError) override {
        return Error{TELEPORT_ERROR_NOT_SUPPORTED, "WiFi Direct not supported"};
    }
    
    void disconnect() override {}
    
    std::optional<WifiDirectConnection> get_connection_info() const override {
        return std::nullopt;
    }
    
    std::vector<WifiDirectPeer> get_peers() const override {
        return {};
    }
    
    Result<void> start_advertising() override {
        return Error{TELEPORT_ERROR_NOT_SUPPORTED, "WiFi Direct not supported"};
    }
    
    void stop_advertising() override {}
    void set_disconnect_callback(OnWifiDirectDisconnected) override {}
    void set_state_callback(OnWifiDirectStateChanged) override {}
    void cancel_connect() override {}
};

std::unique_ptr<WifiDirect> create_wifi_direct() {
    return std::make_unique<StubWifiDirect>();
}

bool is_wifi_direct_supported() {
    return false;
}

} // namespace teleport

#endif // _MSC_VER && WINRT_AVAILABLE

#endif // _WIN32
