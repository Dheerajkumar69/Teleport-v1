/**
 * @file hotspot_win.cpp
 * @brief Windows hotspot implementation using netsh wlan hosted network
 * 
 * Uses the legacy Windows Hosted Network feature which works with MinGW.
 * Requires admin privileges and WiFi adapter support.
 */

#include "hotspot.hpp"
#include "teleport/errors.h"
#include "utils/logger.hpp"

#ifdef _WIN32

#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>
#include <algorithm>
#include <random>
#include <sstream>
#include <array>
#include <memory>

namespace teleport {

/**
 * @brief Execute a command and capture output
 */
static std::string exec_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    _pclose(pipe);
    return result;
}

/**
 * @brief Check if running with admin privileges
 */
static bool is_admin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    
    return isAdmin != FALSE;
}

/**
 * @brief Real Hotspot implementation using netsh commands
 */
class NetshHotspot : public Hotspot {
public:
    NetshHotspot() = default;
    ~NetshHotspot() override { destroy(); }
    
    Result<HotspotInfo> create(const HotspotConfig& config) override {
        if (active_) {
            return Error{TELEPORT_ERROR_HOTSPOT_ALREADY_ACTIVE, "Hotspot already active"};
        }
        
        // Check admin rights
        if (!is_admin()) {
            LOG_WARN("Hotspot requires administrator privileges");
            return Error{TELEPORT_ERROR_NOT_SUPPORTED, 
                "Administrator privileges required. Run as admin to create hotspot."};
        }
        
        // Store config
        info_.ssid = config.ssid.empty() ? generate_hotspot_ssid() : config.ssid;
        info_.password = config.password.empty() ? generate_hotspot_password() : config.password;
        
        // Configure hosted network
        std::string configCmd = "netsh wlan set hostednetwork mode=allow ssid=" + 
                                info_.ssid + " key=" + info_.password + " 2>&1";
        std::string configResult = exec_command(configCmd);
        
        if (configResult.find("error") != std::string::npos ||
            configResult.find("Error") != std::string::npos) {
            LOG_ERROR("Failed to configure hotspot: ", configResult);
            return Error{TELEPORT_ERROR_HOTSPOT_FAILED, 
                "Failed to configure hosted network. WiFi adapter may not support it."};
        }
        
        // Start hosted network
        std::string startCmd = "netsh wlan start hostednetwork 2>&1";
        std::string startResult = exec_command(startCmd);
        
        if (startResult.find("started") != std::string::npos ||
            startResult.find("démarré") != std::string::npos) {  // French Windows
            active_ = true;
            
            // Get gateway IP (usually 192.168.137.1 for Windows hosted network)
            info_.gateway_ip = "192.168.137.1";
            
            LOG_INFO("Hotspot started: ", info_.ssid, " / ", info_.password);
            return info_;
        } else {
            LOG_ERROR("Failed to start hotspot: ", startResult);
            return Error{TELEPORT_ERROR_HOTSPOT_FAILED, 
                "Failed to start hosted network: " + startResult};
        }
    }
    
    void destroy() override {
        if (!active_) return;
        
        std::string cmd = "netsh wlan stop hostednetwork 2>&1";
        exec_command(cmd);
        
        active_ = false;
        LOG_INFO("Hotspot stopped");
    }
    
    bool is_active() const override { return active_; }
    
    HotspotInfo get_info() const override { return info_; }
    
    std::string get_gateway_ip() const override { 
        return active_ ? info_.gateway_ip : ""; 
    }
    
    void set_client_callback(OnClientConnected, OnClientDisconnected) override {
        // Not easily available with netsh approach
    }
    
    std::vector<std::string> get_connected_clients() const override {
        std::vector<std::string> clients;
        if (!active_) return clients;
        
        // Query ARP table for clients in hotspot range
        std::string result = exec_command("arp -a 2>&1");
        std::istringstream iss(result);
        std::string line;
        
        while (std::getline(iss, line)) {
            if (line.find("192.168.137.") != std::string::npos && 
                line.find("192.168.137.1") == std::string::npos) {
                // Extract IP from line
                size_t start = line.find("192.168.137.");
                if (start != std::string::npos) {
                    size_t end = line.find(' ', start);
                    if (end != std::string::npos) {
                        clients.push_back(line.substr(start, end - start));
                    }
                }
            }
        }
        
        return clients;
    }
    
private:
    bool active_ = false;
    HotspotInfo info_;
};

std::unique_ptr<Hotspot> create_hotspot() {
    return std::make_unique<NetshHotspot>();
}

std::string detect_hotspot_gateway() {
    // Check default gateway - if it's in 192.168.137.x range, likely hotspot
    ULONG buffer_size = 0;
    GetAdaptersInfo(nullptr, &buffer_size);
    
    if (buffer_size == 0) return "";
    
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

