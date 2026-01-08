/**
 * @file hotspot_win.cpp
 * @brief Windows hotspot implementation
 * 
 * Note: MinGW doesn't fully support WlanHostedNetwork API,
 * so we provide a stub implementation that returns "not supported"
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

namespace teleport {

// Hotspot is not fully supported in this build configuration
// Windows Mobile Hotspot requires MSVC or specific Windows SDK

class StubHotspot : public Hotspot {
public:
    Result<HotspotInfo> create(const HotspotConfig&) override {
        LOG_WARN("Hotspot not available in this build");
        return Error{TELEPORT_ERROR_NOT_SUPPORTED, "Hotspot requires Windows SDK with WlanAPI"};
    }
    void destroy() override {}
    bool is_active() const override { return false; }
    HotspotInfo get_info() const override { return HotspotInfo{}; }
    std::string get_gateway_ip() const override { return ""; }
    void set_client_callback(OnClientConnected, OnClientDisconnected) override {}
    std::vector<std::string> get_connected_clients() const override { return {}; }
};

std::unique_ptr<Hotspot> create_hotspot() {
    return std::make_unique<StubHotspot>();
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
