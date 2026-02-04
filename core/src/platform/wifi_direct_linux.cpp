/**
 * @file wifi_direct_linux.cpp
 * @brief Wi-Fi Direct - Linux stub implementation
 *
 * Wi-Fi Direct on Linux requires wpa_supplicant p2p functionality,
 * which is complex. This provides stub implementations.
 */

#include "teleport/errors.h"
#include "wifi_direct.hpp"

#ifdef TELEPORT_LINUX

#include <optional>

namespace teleport {

bool is_wifi_direct_supported() {
  // Wi-Fi Direct not implemented on Linux
  return false;
}

class LinuxWifiDirect : public WifiDirect {
public:
  bool is_available() const override { return false; }

  WifiDirectState state() const override { return WifiDirectState::Disabled; }

  Result<void> start_discovery(OnWifiDirectPeerFound on_found,
                               OnWifiDirectPeerLost on_lost) override {
    (void)on_found;
    (void)on_lost;
    return make_error(TELEPORT_ERROR_NOT_SUPPORTED,
                      "Wi-Fi Direct not supported on Linux");
  }

  void stop_discovery() override {}

  Result<void> connect(const std::string &mac_address,
                       OnWifiDirectConnected on_connected,
                       OnWifiDirectError on_error) override {
    (void)mac_address;
    (void)on_connected;
    (void)on_error;
    return make_error(TELEPORT_ERROR_NOT_SUPPORTED,
                      "Wi-Fi Direct not supported on Linux");
  }

  void disconnect() override {}

  std::optional<WifiDirectConnection> get_connection_info() const override {
    return std::nullopt;
  }

  std::vector<WifiDirectPeer> get_peers() const override { return {}; }

  Result<void> start_advertising() override {
    return make_error(TELEPORT_ERROR_NOT_SUPPORTED,
                      "Wi-Fi Direct not supported on Linux");
  }

  void stop_advertising() override {}

  void set_disconnect_callback(OnWifiDirectDisconnected callback) override {
    (void)callback;
  }

  void set_state_callback(OnWifiDirectStateChanged callback) override {
    (void)callback;
  }

  void cancel_connect() override {}
};

std::unique_ptr<WifiDirect> create_wifi_direct() {
  return std::make_unique<LinuxWifiDirect>();
}

} // namespace teleport

#endif // TELEPORT_LINUX
