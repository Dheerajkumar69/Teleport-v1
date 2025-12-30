/**
 * @file hotspot_win.cpp
 * @brief Windows hotspot implementation using Mobile Hotspot API
 * 
 * Uses Windows 10+ Mobile Hotspot feature via WlanHostedNetwork API
 * or the newer TetheringManager for Windows 10 1607+
 */

#include "hotspot.hpp"
#include "utils/logger.hpp"
#include "utils/uuid.hpp"

#ifdef _WIN32

#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>
#include <wlanapi.h>
#include <netcon.h>
#include <algorithm>
#include <random>
#include <sstream>

#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace teleport {

class WindowsHotspot : public Hotspot {
public:
    WindowsHotspot() = default;
    
    ~WindowsHotspot() override {
        destroy();
    }
    
    Result<HotspotInfo> create(const HotspotConfig& config) override {
        if (m_active) {
            return make_error(TELEPORT_ERROR_ALREADY_RUNNING, "Hotspot already active");
        }
        
        m_config = config;
        
        // Generate SSID and password if not provided
        if (m_config.ssid.empty()) {
            m_config.ssid = generate_hotspot_ssid();
        }
        if (m_config.password.empty()) {
            m_config.password = generate_hotspot_password();
        }
        
        // Open WLAN handle
        DWORD negotiated_version;
        DWORD result = WlanOpenHandle(2, nullptr, &negotiated_version, &m_wlan_handle);
        if (result != ERROR_SUCCESS) {
            return make_error(TELEPORT_ERROR_INTERNAL, "Failed to open WLAN handle");
        }
        
        // Set hosted network settings
        WLAN_HOSTED_NETWORK_REASON failure_reason;
        
        // Convert SSID to wide string for API
        std::wstring wide_ssid(m_config.ssid.begin(), m_config.ssid.end());
        
        WLAN_HOSTED_NETWORK_CONNECTION_SETTINGS settings = {};
        settings.dwMaxNumberOfPeers = 10;
        memcpy(settings.hostedNetworkSSID.ucSSID, m_config.ssid.c_str(), 
               std::min(m_config.ssid.size(), size_t(32)));
        settings.hostedNetworkSSID.uSSIDLength = 
            static_cast<ULONG>(std::min(m_config.ssid.size(), size_t(32)));
        
        result = WlanHostedNetworkSetProperty(
            m_wlan_handle,
            wlan_hosted_network_opcode_connection_settings,
            sizeof(settings),
            &settings,
            &failure_reason,
            nullptr
        );
        
        if (result != ERROR_SUCCESS) {
            LOG_ERROR("Failed to set hosted network settings: ", result);
            WlanCloseHandle(m_wlan_handle, nullptr);
            return make_error(TELEPORT_ERROR_INTERNAL, "Failed to configure hotspot");
        }
        
        // Set security key (password)
        WLAN_HOSTED_NETWORK_SET_SECONDARY_KEY key_data = {};
        key_data.ucKeyLength = static_cast<DWORD>(m_config.password.size() + 1);
        key_data.bIsPassPhrase = TRUE;
        key_data.bPersistent = FALSE;
        memcpy(key_data.ucSecondaryKey, m_config.password.c_str(), 
               std::min(m_config.password.size(), size_t(63)));
        
        result = WlanHostedNetworkSetSecondaryKey(
            m_wlan_handle,
            static_cast<DWORD>(m_config.password.size() + 1),
            reinterpret_cast<PUCHAR>(const_cast<char*>(m_config.password.c_str())),
            TRUE,  // passphrase
            FALSE, // not persistent
            &failure_reason,
            nullptr
        );
        
        if (result != ERROR_SUCCESS) {
            LOG_WARN("Failed to set hotspot password, using default");
        }
        
        // Start hosted network
        result = WlanHostedNetworkStartUsing(m_wlan_handle, &failure_reason, nullptr);
        if (result != ERROR_SUCCESS) {
            LOG_ERROR("Failed to start hosted network: ", result, " reason: ", failure_reason);
            WlanCloseHandle(m_wlan_handle, nullptr);
            return make_error(TELEPORT_ERROR_INTERNAL, "Failed to start hotspot");
        }
        
        m_active = true;
        
        // Get gateway IP (our IP on the hosted network)
        m_gateway_ip = find_hotspot_ip();
        
        LOG_INFO("Hotspot started: ", m_config.ssid, " (", m_gateway_ip, ")");
        
        HotspotInfo info;
        info.ssid = m_config.ssid;
        info.password = m_config.password;
        info.gateway_ip = m_gateway_ip;
        info.is_active = true;
        info.client_count = 0;
        
        return info;
    }
    
    void destroy() override {
        if (m_active && m_wlan_handle) {
            WLAN_HOSTED_NETWORK_REASON reason;
            WlanHostedNetworkStopUsing(m_wlan_handle, &reason, nullptr);
            WlanCloseHandle(m_wlan_handle, nullptr);
            m_wlan_handle = nullptr;
            m_active = false;
            LOG_INFO("Hotspot stopped");
        }
    }
    
    bool is_active() const override {
        return m_active;
    }
    
    HotspotInfo get_info() const override {
        HotspotInfo info;
        info.ssid = m_config.ssid;
        info.password = m_config.password;
        info.gateway_ip = m_gateway_ip;
        info.is_active = m_active;
        info.client_count = static_cast<int>(m_clients.size());
        return info;
    }
    
    std::string get_gateway_ip() const override {
        return m_gateway_ip;
    }
    
    void set_client_callback(OnClientConnected on_connect,
                            OnClientDisconnected on_disconnect) override {
        m_on_connect = std::move(on_connect);
        m_on_disconnect = std::move(on_disconnect);
    }
    
    std::vector<std::string> get_connected_clients() const override {
        return m_clients;
    }

private:
    std::string find_hotspot_ip() {
        // Find IP of the hosted network adapter
        ULONG buffer_size = 0;
        GetAdaptersInfo(nullptr, &buffer_size);
        
        std::vector<uint8_t> buffer(buffer_size);
        PIP_ADAPTER_INFO adapters = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
        
        if (GetAdaptersInfo(adapters, &buffer_size) == NO_ERROR) {
            for (auto adapter = adapters; adapter; adapter = adapter->Next) {
                std::string desc(adapter->Description);
                // Look for Microsoft Hosted Network Virtual Adapter
                if (desc.find("Microsoft Hosted Network") != std::string::npos ||
                    desc.find("Virtual") != std::string::npos) {
                    std::string ip = adapter->IpAddressList.IpAddress.String;
                    if (!ip.empty() && ip != "0.0.0.0") {
                        return ip;
                    }
                }
            }
        }
        
        // Fallback to typical hotspot subnet
        return "192.168.137.1";
    }
    
    HANDLE m_wlan_handle = nullptr;
    HotspotConfig m_config;
    std::string m_gateway_ip;
    bool m_active = false;
    std::vector<std::string> m_clients;
    OnClientConnected m_on_connect;
    OnClientDisconnected m_on_disconnect;
};

std::unique_ptr<Hotspot> create_hotspot() {
    return std::make_unique<WindowsHotspot>();
}

std::string detect_hotspot_gateway() {
    // Check default gateway - if it's in 192.168.137.x range, likely hotspot
    ULONG buffer_size = 0;
    GetAdaptersInfo(nullptr, &buffer_size);
    
    std::vector<uint8_t> buffer(buffer_size);
    PIP_ADAPTER_INFO adapters = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
    
    if (GetAdaptersInfo(adapters, &buffer_size) == NO_ERROR) {
        for (auto adapter = adapters; adapter; adapter = adapter->Next) {
            std::string gateway = adapter->GatewayList.IpAddress.String;
            if (gateway.find("192.168.137.") == 0 ||
                gateway.find("192.168.43.") == 0) {
                return gateway;
            }
        }
    }
    
    return "";
}

std::string generate_hotspot_ssid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    return "Teleport-" + std::to_string(dis(gen));
}

std::string generate_hotspot_password() {
    static const char chars[] = 
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(chars) - 2);
    
    std::string password;
    password.reserve(12);
    for (int i = 0; i < 12; i++) {
        password += chars[dis(gen)];
    }
    return password;
}

} // namespace teleport

#endif // _WIN32
