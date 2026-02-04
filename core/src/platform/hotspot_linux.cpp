/**
 * @file hotspot_linux.cpp
 * @brief Wi-Fi hotspot - Linux stub implementation
 *
 * Linux hotspot functionality requires NetworkManager or hostapd,
 * which is complex to implement. This provides stub implementations.
 */

#include "hotspot.hpp"
#include "teleport/errors.h"

#ifdef TELEPORT_LINUX

#include <random>
#include <string>

namespace teleport {

// Stub hotspot class for Linux
class LinuxHotspot : public Hotspot {
public:
  Result<HotspotInfo> create(const HotspotConfig &config) override {
    (void)config;
    return make_error(TELEPORT_ERROR_NOT_SUPPORTED,
                      "Hotspot not supported on Linux");
  }

  void destroy() override {}

  bool is_active() const override { return false; }

  HotspotInfo get_info() const override { return {}; }

  std::string get_gateway_ip() const override { return ""; }

  void set_client_callback(OnClientConnected, OnClientDisconnected) override {}

  std::vector<std::string> get_connected_clients() const override { return {}; }
};

std::unique_ptr<Hotspot> create_hotspot() {
  return std::make_unique<LinuxHotspot>();
}

std::string detect_hotspot_gateway() {
  // On Linux, we would need to check NetworkManager or similar
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

#endif // TELEPORT_LINUX
