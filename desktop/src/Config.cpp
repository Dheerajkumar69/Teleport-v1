/**
 * @file Config.cpp
 * @brief Settings persistence implementation - BULLETPROOF VERSION
 *
 * Features:
 * - Atomic file writes (temp file + rename)
 * - Parent directory creation
 * - Exception handling for all I/O
 * - Input validation
 * - Corrupted JSON recovery with backup
 */

#include "Config.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <shlobj.h>
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace teleport::ui {

// ============================================================================
// Simple JSON Parser/Writer (no external dependency)
// ============================================================================

namespace {

std::string EscapeJson(const std::string &s) {
  std::string result;
  result.reserve(s.size() + 10);
  for (char c : s) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      // Skip control characters
      if (static_cast<unsigned char>(c) >= 32) {
        result += c;
      }
    }
  }
  return result;
}

std::string UnescapeJson(const std::string &s) {
  std::string result;
  result.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      switch (s[i + 1]) {
      case '"':
        result += '"';
        i++;
        break;
      case '\\':
        result += '\\';
        i++;
        break;
      case 'n':
        result += '\n';
        i++;
        break;
      case 'r':
        result += '\r';
        i++;
        break;
      case 't':
        result += '\t';
        i++;
        break;
      default:
        result += s[i];
      }
    } else {
      result += s[i];
    }
  }
  return result;
}

std::string ExtractJsonString(const std::string &json, const std::string &key) {
  if (json.empty() || key.empty())
    return "";

  std::string pattern = "\"" + key + "\"";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos)
    return "";

  pos = json.find(':', pos);
  if (pos == std::string::npos)
    return "";

  pos = json.find('"', pos);
  if (pos == std::string::npos)
    return "";

  size_t start = pos + 1;
  if (start >= json.size())
    return "";

  size_t end = start;
  while (end < json.size()) {
    if (json[end] == '"' && (end == start || json[end - 1] != '\\')) {
      break;
    }
    end++;
  }

  if (end > json.size())
    end = json.size();
  return UnescapeJson(json.substr(start, end - start));
}

bool ExtractJsonBool(const std::string &json, const std::string &key,
                     bool defaultVal) {
  if (json.empty() || key.empty())
    return defaultVal;

  std::string pattern = "\"" + key + "\"";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos)
    return defaultVal;

  pos = json.find(':', pos);
  if (pos == std::string::npos)
    return defaultVal;

  size_t start = json.find_first_not_of(" \t\n\r", pos + 1);
  if (start == std::string::npos)
    return defaultVal;

  // Bounds check before substr
  if (start + 4 <= json.size() && json.substr(start, 4) == "true")
    return true;
  if (start + 5 <= json.size() && json.substr(start, 5) == "false")
    return false;
  return defaultVal;
}

// Check if file exists
bool FileExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

// Check if directory exists
bool DirectoryExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Create directory recursively (handles ~/.config/teleport)
bool CreateDirectoryRecursive(const std::string &path) {
  if (path.empty())
    return false;
  if (DirectoryExists(path))
    return true;

  // Find parent directory
  size_t lastSlash = path.find_last_of("/\\");
  if (lastSlash != std::string::npos && lastSlash > 0) {
    std::string parent = path.substr(0, lastSlash);
    if (!DirectoryExists(parent)) {
      if (!CreateDirectoryRecursive(parent)) {
        return false;
      }
    }
  }

#ifdef _WIN32
  return CreateDirectoryA(path.c_str(), nullptr) ||
         GetLastError() == ERROR_ALREADY_EXISTS;
#else
  return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

// Atomic file write: write to temp, then rename
bool WriteFileAtomic(const std::string &path, const std::string &content) {
  std::string tempPath = path + ".tmp";

  try {
    // Write to temp file
    std::ofstream file(tempPath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
      return false;
    }

    file << content;
    file.flush();

    if (!file.good()) {
      file.close();
      std::remove(tempPath.c_str());
      return false;
    }

    file.close();

    // Rename temp to target (atomic on POSIX)
#ifdef _WIN32
    // Windows: need to delete target first
    if (FileExists(path)) {
      if (!DeleteFileA(path.c_str())) {
        std::remove(tempPath.c_str());
        return false;
      }
    }
    if (!MoveFileA(tempPath.c_str(), path.c_str())) {
      std::remove(tempPath.c_str());
      return false;
    }
#else
    if (rename(tempPath.c_str(), path.c_str()) != 0) {
      std::remove(tempPath.c_str());
      return false;
    }
#endif
    return true;
  } catch (...) {
    std::remove(tempPath.c_str());
    return false;
  }
}

// Read file safely with size limit
std::string ReadFileSafe(const std::string &path,
                         size_t maxSize = 1024 * 1024) {
  try {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
      return "";
    }

    // Check file size
    file.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(file.tellg());
    if (size > maxSize) {
      return ""; // File too large, likely corrupted
    }
    file.seekg(0, std::ios::beg);

    std::string content(size, '\0');
    file.read(&content[0], static_cast<std::streamsize>(size));

    return content;
  } catch (...) {
    return "";
  }
}

} // anonymous namespace

// ============================================================================
// Config Implementation
// ============================================================================

Config::Config() { SetDefaults(); }

void Config::SetDefaults() {
#ifdef _WIN32
  // Get computer name
  char name[256] = {0};
  DWORD size = sizeof(name);
  if (GetComputerNameA(name, &size) && size > 0) {
    deviceName_ = std::string(name, size);
  } else {
    deviceName_ = "Windows PC";
  }
#else
  // Get hostname
  char hostname[256] = {0};
  if (gethostname(hostname, sizeof(hostname)) == 0 && hostname[0] != '\0') {
    // Ensure null termination
    hostname[sizeof(hostname) - 1] = '\0';
    deviceName_ = hostname;
  } else {
    deviceName_ = "Linux PC";
  }
#endif

  downloadPath_ = GetDefaultDownloadPath();
  darkMode_ = true;
  showNotifications_ = true;
  autoStart_ = false;
  signalingServerUrl_ = "wss://teleport-signaling.onrender.com";
}

std::string Config::GetConfigDir() {
#ifdef _WIN32
  char path[MAX_PATH] = {0};
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
    return std::string(path) + "\\Teleport";
  }
  // Fallback to current directory
  char cwd[MAX_PATH] = {0};
  if (GetCurrentDirectoryA(MAX_PATH, cwd)) {
    return std::string(cwd);
  }
  return ".";
#else
  const char *configHome = std::getenv("XDG_CONFIG_HOME");
  if (configHome && configHome[0] != '\0') {
    return std::string(configHome) + "/teleport";
  }

  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    struct passwd *pw = getpwuid(getuid());
    home = (pw && pw->pw_dir) ? pw->pw_dir : nullptr;
  }

  if (home && home[0] != '\0') {
    return std::string(home) + "/.config/teleport";
  }

  // Last resort: current directory
  return ".";
#endif
}

std::string Config::GetConfigPath() {
  return GetConfigDir() +
#ifdef _WIN32
         "\\settings.json";
#else
         "/settings.json";
#endif
}

std::string Config::GetDefaultDownloadPath() {
#ifdef _WIN32
  char path[MAX_PATH] = {0};
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, path))) {
    return std::string(path) + "\\Teleport Downloads";
  }
  return ".\\Teleport Downloads";
#else
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    struct passwd *pw = getpwuid(getuid());
    home = (pw && pw->pw_dir) ? pw->pw_dir : nullptr;
  }

  if (!home || home[0] == '\0') {
    return "./Teleport";
  }

  // Try XDG Downloads first
  std::string downloads = std::string(home) + "/Downloads";
  if (DirectoryExists(downloads)) {
    return downloads + "/Teleport";
  }

  return std::string(home) + "/Teleport";
#endif
}

bool Config::CreateDirectory(const std::string &path) {
  return CreateDirectoryRecursive(path);
}

bool Config::Load() {
  try {
    SetDefaults(); // Always start with safe defaults

    std::string path = GetConfigPath();
    std::string json = ReadFileSafe(path);

    if (json.empty()) {
      // No config file or read error - use defaults (already set)
      return false;
    }

    // Basic JSON validation - must have opening brace
    if (json.find('{') == std::string::npos) {
      // Corrupted file - backup and use defaults
      BackupCorruptedConfig(path);
      return false;
    }

    // Parse JSON fields - gracefully handle missing fields
    std::string name = ExtractJsonString(json, "deviceName");
    if (!name.empty() && name.length() <= 63) {
      deviceName_ = name;
    }

    std::string download = ExtractJsonString(json, "downloadPath");
    if (!download.empty() && download.length() <= 4096) {
      downloadPath_ = download;
    }

    std::string signaling = ExtractJsonString(json, "signalingServerUrl");
    if (!signaling.empty() && signaling.length() <= 512) {
      signalingServerUrl_ = signaling;
    }

    darkMode_ = ExtractJsonBool(json, "darkMode", darkMode_);
    showNotifications_ =
        ExtractJsonBool(json, "showNotifications", showNotifications_);
    autoStart_ = ExtractJsonBool(json, "autoStart", autoStart_);

    dirty_ = false;
    return true;
  } catch (...) {
    // Any exception - fall back to defaults
    SetDefaults();
    return false;
  }
}

void Config::BackupCorruptedConfig(const std::string &path) {
  if (FileExists(path)) {
    std::string backupPath = path + ".corrupted";
    std::rename(path.c_str(), backupPath.c_str());
  }
}

bool Config::Save() {
  try {
    std::string dir = GetConfigDir();

    // Create config directory recursively
    if (!CreateDirectory(dir)) {
      return false;
    }

    std::string path = GetConfigPath();

    // Build JSON content
    std::ostringstream json;
    json << "{\n";
    json << "  \"deviceName\": \"" << EscapeJson(deviceName_) << "\",\n";
    json << "  \"downloadPath\": \"" << EscapeJson(downloadPath_) << "\",\n";
    json << "  \"signalingServerUrl\": \"" << EscapeJson(signalingServerUrl_)
         << "\",\n";
    json << "  \"darkMode\": " << (darkMode_ ? "true" : "false") << ",\n";
    json << "  \"showNotifications\": "
         << (showNotifications_ ? "true" : "false") << ",\n";
    json << "  \"autoStart\": " << (autoStart_ ? "true" : "false") << "\n";
    json << "}\n";

    // Atomic write
    if (!WriteFileAtomic(path, json.str())) {
      return false;
    }

    dirty_ = false;
    return true;
  } catch (...) {
    return false;
  }
}

void Config::SetDeviceName(const std::string &name) {
  // Validate: non-empty, max 63 chars
  if (name.empty() || name.length() > 63) {
    return;
  }
  if (deviceName_ != name) {
    deviceName_ = name;
    Save();
  }
}

void Config::SetDownloadPath(const std::string &path) {
  // Validate: non-empty, max 4096 chars
  if (path.empty() || path.length() > 4096) {
    return;
  }
  if (downloadPath_ != path) {
    downloadPath_ = path;
    Save();
  }
}

void Config::SetDarkMode(bool enabled) {
  if (darkMode_ != enabled) {
    darkMode_ = enabled;
    Save();
  }
}

void Config::SetShowNotifications(bool enabled) {
  if (showNotifications_ != enabled) {
    showNotifications_ = enabled;
    Save();
  }
}

void Config::SetAutoStart(bool enabled) {
  if (autoStart_ != enabled) {
    autoStart_ = enabled;
    Save();
  }
}

void Config::SetSignalingServerUrl(const std::string &url) {
  // Validate: non-empty, max 512 chars, starts with ws:// or wss://
  if (url.empty() || url.length() > 512) {
    return;
  }
  if (url.find("ws://") != 0 && url.find("wss://") != 0) {
    return; // Invalid URL scheme
  }
  if (signalingServerUrl_ != url) {
    signalingServerUrl_ = url;
    Save();
  }
}

} // namespace teleport::ui
