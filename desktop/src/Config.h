/**
 * @file Config.h
 * @brief Settings persistence for Teleport desktop application
 *
 * Manages application settings with JSON file storage at:
 * - Linux: ~/.config/teleport/settings.json
 * - Windows: %APPDATA%/Teleport/settings.json
 */

#ifndef TELEPORT_CONFIG_H
#define TELEPORT_CONFIG_H

#include <string>

namespace teleport::ui {

/**
 * @brief Application configuration manager
 *
 * Handles persistent storage of user preferences including:
 * - Device name
 * - Download path
 * - Theme settings
 * - Notification preferences
 */
class Config {
public:
  Config();
  ~Config() = default;

  /**
   * @brief Load settings from disk
   * @return true if loaded successfully, false if using defaults
   */
  bool Load();

  /**
   * @brief Save settings to disk
   * @return true if saved successfully
   */
  bool Save();

  // Getters
  const std::string &GetDeviceName() const { return deviceName_; }
  const std::string &GetDownloadPath() const { return downloadPath_; }
  bool GetDarkMode() const { return darkMode_; }
  bool GetShowNotifications() const { return showNotifications_; }
  bool GetAutoStart() const { return autoStart_; }
  const std::string &GetSignalingServerUrl() const {
    return signalingServerUrl_;
  }

  // Setters (auto-save after change)
  void SetDeviceName(const std::string &name);
  void SetDownloadPath(const std::string &path);
  void SetDarkMode(bool enabled);
  void SetShowNotifications(bool enabled);
  void SetAutoStart(bool enabled);
  void SetSignalingServerUrl(const std::string &url);

  /**
   * @brief Get the config file path
   */
  static std::string GetConfigPath();

  /**
   * @brief Get the config directory path
   */
  static std::string GetConfigDir();

  /**
   * @brief Get default download path
   */
  static std::string GetDefaultDownloadPath();

private:
  std::string deviceName_;
  std::string downloadPath_;
  bool darkMode_ = true;
  bool showNotifications_ = true;
  bool autoStart_ = false;
  std::string signalingServerUrl_ = "wss://teleport-signaling.onrender.com";

  bool dirty_ = false;

  void SetDefaults();
  void BackupCorruptedConfig(const std::string &path);
  static bool CreateDirectory(const std::string &path);
};

} // namespace teleport::ui

#endif // TELEPORT_CONFIG_H
