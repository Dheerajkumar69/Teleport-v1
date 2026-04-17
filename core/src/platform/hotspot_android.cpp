/**
 * @file hotspot_android.cpp
 * @brief Wi-Fi hotspot - Android stub implementation
 *
 * On Android, hotspot management is handled entirely by HotspotManager.kt
 * using the WifiManager.LocalOnlyHotspot API (Android 8.0+).
 * This C++ stub satisfies the linker while delegating all real work
 * to the Java/Kotlin layer via JNI callbacks.
 */

#include "hotspot.hpp"
#include "teleport/errors.h"

#if defined(TELEPORT_PLATFORM_ANDROID) || defined(__ANDROID__)

#include <random>
#include <string>

namespace teleport {

/**
 * Android hotspot stub — the real implementation is in HotspotManager.kt.
 * The C++ core does not control Android hotspots directly; instead:
 *   1. React Native layer calls HotspotManager.startHotspot()
 *   2. Kotlin uses WifiManager.startLocalOnlyHotspot()
 *   3. SSID/password/IP returned to JS for display to user
 *   4. Other device manually connects to the hotspot SSID
 *   5. Once connected, both devices are on same subnet → normal LAN discovery kicks in
 */
class AndroidHotspot : public Hotspot {
public:
    Result<HotspotInfo> create(const HotspotConfig& config) override {
        (void)config;
        // On Android, hotspot is created via HotspotManager.kt (Java layer).
        // Return a stub info — actual data comes from the Java callback.
        return make_error(TELEPORT_ERROR_NOT_SUPPORTED,
                          "Use HotspotManager native module for Android hotspot");
    }

    void destroy() override {}

    bool is_active() const override { return false; }

    HotspotInfo get_info() const override { return {}; }

    std::string get_gateway_ip() const override { return ""; }

    void set_client_callback(OnClientConnected, OnClientDisconnected) override {}

    std::vector<std::string> get_connected_clients() const override { return {}; }
};

std::unique_ptr<Hotspot> create_hotspot() {
    return std::make_unique<AndroidHotspot>();
}

std::string detect_hotspot_gateway() {
    // Detection is done in Java - WifiDirectManager checks network interfaces
    return "";
}

std::string generate_hotspot_ssid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    return "Teleport-" + std::to_string(dis(gen));
}

std::string generate_hotspot_password() {
    static const char charset[] = "abcdefghijkmnpqrstuvwxyz"
                                  "ABCDEFGHJKLMNPQRSTUVWXYZ"
                                  "23456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    std::string password;
    for (int i = 0; i < 12; ++i) {
        password += charset[dis(gen)];
    }
    return password;
}

} // namespace teleport

#endif // TELEPORT_PLATFORM_ANDROID || __ANDROID__
