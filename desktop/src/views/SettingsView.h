/**
 * @file SettingsView.h
 * @brief Application settings view
 */

#pragma once

#include "Config.h"
#include "TeleportBridge.h"
#include "Theme.h"
#include <string>

namespace teleport::ui {

class SettingsView {
public:
  SettingsView(TeleportBridge *bridge, Theme *theme, Config *config);
  ~SettingsView() = default;

  void Update();
  void Render();

private:
  void RenderHeader();
  void RenderDeviceSettings();
  void RenderTransferSettings();
  void RenderAppearanceSettings();
  void RenderAbout();

  void SaveSettings();
  void LoadSettings();

  TeleportBridge *bridge_;
  Theme *theme_;
  Config *config_;

  // Settings state
  char deviceName_[64] = "Windows PC";
  std::string downloadPath_;
  bool darkMode_ = true;
  bool autoStart_ = false;
  bool showNotifications_ = true;

  // Animation
  float toggleAnim_[4] = {1.0f, 0.0f, 1.0f, 0.0f}; // For toggle switches
};

} // namespace teleport::ui
