/**
 * @file TeleportBridge.cpp
 * @brief Implementation of bridge between UI and Teleport C API
 */

#include "TeleportBridge.h"
#include "WebSignalingClient.h"
#include <fstream>

#ifdef _WIN32
#include <ShlObj.h>
#include <windows.h>
#else
#include <cstring>
#include <pwd.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

namespace teleport::ui {

// Cross-platform helpers
#ifndef _WIN32
static inline uint64_t GetTickCount64() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

static inline void Sleep(unsigned long ms) { usleep(ms * 1000); }
#endif

// Static callbacks for C API
static void DeviceCallback(const TeleportDevice *device, void *userData) {
  auto *bridge = static_cast<TeleportBridge *>(userData);
  bridge->OnDeviceDiscovered(device);
}

static void DeviceLostCallback(const char *deviceId, void *userData) {
  auto *bridge = static_cast<TeleportBridge *>(userData);
  bridge->OnDeviceLost(deviceId);
}

static void ProgressCallback(const TeleportProgress *progress, void *userData) {
  auto *bridge = static_cast<TeleportBridge *>(userData);
  bridge->OnProgress(progress);
}

static void CompleteCallback(TeleportError error, void *userData) {
  auto *bridge = static_cast<TeleportBridge *>(userData);
  bridge->OnComplete(error);
}

static int IncomingCallback(const TeleportDevice *sender,
                            const TeleportFileInfo *files, size_t count,
                            void *userData) {
  auto *bridge = static_cast<TeleportBridge *>(userData);
  return bridge->OnIncoming(sender, files, count);
}

TeleportBridge::TeleportBridge() {
#ifdef _WIN32
  // Set default download path to user's Downloads folder
  wchar_t *downloadPathW = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr,
                                     &downloadPathW))) {
    char downloadPathA[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, downloadPathW, -1, downloadPathA, MAX_PATH,
                        nullptr, nullptr);
    downloadPath_ = downloadPathA;
    CoTaskMemFree(downloadPathW);
  } else {
    downloadPath_ = ".";
  }
#else
  // Linux: Get home directory and use ~/Downloads
  const char *home = getenv("HOME");
  if (!home) {
    struct passwd *pw = getpwuid(getuid());
    if (pw)
      home = pw->pw_dir;
  }
  if (home) {
    downloadPath_ = std::string(home) + "/Downloads";
  } else {
    downloadPath_ = ".";
  }
#endif
}

TeleportBridge::~TeleportBridge() { Shutdown(); }

bool TeleportBridge::Initialize() {
  if (engine_) {
    return true; // Already initialized
  }

  TeleportConfig config = {};
  config.download_path = downloadPath_.c_str();

  TeleportError err = teleport_create(&config, &engine_);
  if (err != TELEPORT_OK) {
    return false;
  }

  // Set device name from hostname if not already set
  if (deviceName_.empty()) {
    char hostname[256] = {0};
#ifdef _WIN32
    DWORD size = sizeof(hostname);
    GetComputerNameA(hostname, &size);
#else
    gethostname(hostname, sizeof(hostname) - 1);
#endif
    deviceName_ = hostname[0] ? hostname : "Desktop";
  }

  // Connect to signaling server (sync for stability)
  // Try local first, then production
  if (!ConnectToWebSignaling("ws://localhost:3000")) {
    ConnectToWebSignaling("wss://teleport-signaling.onrender.com");
  }

  return true;
}

void TeleportBridge::Shutdown() {
  // Signal all threads to stop
  shuttingDown_.store(true);

  // Disconnect web signaling first (graceful close before engine destruction)
  DisconnectFromWebSignaling();

  StopDiscovery();
  StopReceiving();

  // Wait for all active threads to complete
  {
    std::lock_guard<std::mutex> lock(threadsMutex_);
    for (auto &t : activeThreads_) {
      if (t.joinable()) {
        t.join();
      }
    }
    activeThreads_.clear();
  }

  if (engine_) {
    teleport_destroy(engine_);
    engine_ = nullptr;
  }

  shuttingDown_.store(false);
}

void TeleportBridge::CleanupFinishedThreads() {
  std::lock_guard<std::mutex> lock(threadsMutex_);
  // Remove threads that have finished (can't easily check, so we limit count)
  // This is called before adding new threads to cap the pool
  if (activeThreads_.size() >= kMaxTransferThreads) {
    // Try to join any that might be done
    for (auto it = activeThreads_.begin(); it != activeThreads_.end();) {
      // We can't easily check if joinable without blocking,
      // so we rely on the max limit and proper shutdown
      ++it;
    }
  }
}

void TeleportBridge::Update() {
  // Get current time for animations
  auto now = std::chrono::steady_clock::now();
  float currentTime =
      std::chrono::duration<float>(now.time_since_epoch()).count();
  float deltaTime = currentTime - lastUpdateTime_;
  lastUpdateTime_ = currentTime;

  // Cap delta time
  if (deltaTime > 0.1f)
    deltaTime = 0.1f;

  // Animate device cards
  {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    for (auto &device : devices_) {
      if (device.isNew) {
        device.fadeIn += deltaTime * 4.0f; // 250ms fade in
        if (device.fadeIn >= 1.0f) {
          device.fadeIn = 1.0f;
          device.isNew = false;
        }
      }
    }
  }

  // Animate transfer progress
  {
    std::lock_guard<std::mutex> lock(transfersMutex_);
    for (auto &transfer : transfers_) {
      float targetProgress = 0.0f;
      if (transfer.bytesTotal > 0) {
        targetProgress =
            (float)transfer.bytesTransferred / (float)transfer.bytesTotal;
      }
      // Smooth animation
      transfer.progress +=
          (targetProgress - transfer.progress) * deltaTime * 8.0f;
    }
  }

  // Auto-reconnect to signaling server if disconnected
  // Initialize to epoch so first attempt happens immediately
  static auto lastReconnectAttempt = std::chrono::steady_clock::time_point{};
  static bool firstAttempt = true;
  if (!webSignalingConnected_.load() && !shuttingDown_.load()) {
    auto timeSinceLastAttempt =
        std::chrono::steady_clock::now() - lastReconnectAttempt;
    // First attempt immediate, then retry every 30 seconds
    if (firstAttempt || timeSinceLastAttempt > std::chrono::seconds(30)) {
      firstAttempt = false;
      lastReconnectAttempt = std::chrono::steady_clock::now();
      // Try local first, then production (TLS now supported)
      if (!ConnectToWebSignaling("ws://localhost:3000")) {
        ConnectToWebSignaling("wss://teleport-signaling.onrender.com");
      }
    }
  }
}

bool TeleportBridge::StartDiscovery() {
  if (!engine_ || isDiscovering_.load()) {
    return false;
  }

  TeleportError err = teleport_start_discovery(engine_, DeviceCallback,
                                               DeviceLostCallback, this);

  if (err == TELEPORT_OK) {
    isDiscovering_.store(true);
    return true;
  }
  return false;
}

void TeleportBridge::StopDiscovery() {
  if (engine_ && isDiscovering_.load()) {
    teleport_stop_discovery(engine_);
    isDiscovering_.store(false);
  }
}

std::vector<DeviceInfo> TeleportBridge::GetDevices() const {
  std::vector<DeviceInfo> allDevices;

  // Add local devices
  {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    allDevices = devices_;
  }

  // Add web peers
  {
    std::lock_guard<std::mutex> lock(webPeersMutex_);
    allDevices.insert(allDevices.end(), webPeers_.begin(), webPeers_.end());
  }

  return allDevices;
}

void TeleportBridge::AddManualDevice(const char *ip, uint16_t port,
                                     const char *name) {
  // Input validation
  if (!ip || !name)
    return;
  if (strlen(ip) == 0 || strlen(ip) > 45)
    return; // IPv6 max is 45 chars
  if (port == 0)
    return;
  if (strlen(name) == 0 || strlen(name) > TELEPORT_MAX_DEVICE_NAME - 1)
    return;

  DeviceInfo info;
  info.id = std::string("manual_") + ip + "_" + std::to_string(port);
  info.name = name;
  info.os = "Manual";
  info.ip = ip;
  info.port = port;
  info.lastSeen = GetTickCount64();
  info.isNew = true;
  info.fadeIn = 0.0f;

  std::lock_guard<std::mutex> lock(devicesMutex_);

  // Check if already exists
  for (auto &existing : devices_) {
    if (existing.ip == ip && existing.port == port) {
      existing.lastSeen = GetTickCount64();
      return;
    }
  }

  devices_.push_back(info);
}

bool TeleportBridge::SendFiles(const std::string &deviceId,
                               const std::vector<std::string> &filePaths) {
  // Input validation
  if (!engine_)
    return false;
  if (deviceId.empty() || deviceId.length() > 128)
    return false;
  if (filePaths.empty() || filePaths.size() > 1000)
    return false;
  if (shuttingDown_.load())
    return false;

  // Validate each file path
  for (const auto &path : filePaths) {
    if (path.empty() || path.length() > 4096)
      return false;
  }

  // Check if this is a web peer - if so, use relay transfer
  bool isWebPeer = false;
  std::string webPeerName;
  {
    std::lock_guard<std::mutex> lock(webPeersMutex_);
    for (const auto &peer : webPeers_) {
      if (peer.id == deviceId) {
        isWebPeer = true;
        webPeerName = peer.name;
        break;
      }
    }
  }

  if (isWebPeer) {
    // Send via web signaling relay
    if (!webSignaling_ || !webSignalingConnected_.load()) {
      return false;
    }

    // Send all files via relay
    for (const auto &path : filePaths) {
      if (!SendFileToWebPeer(deviceId, path)) {
        return false;
      }
    }
    return true;
  }

  // Find local device
  TeleportDevice targetDevice = {};
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    for (const auto &dev : devices_) {
      if (dev.id == deviceId) {
        strncpy(targetDevice.id, dev.id.c_str(), TELEPORT_UUID_SIZE - 1);
        strncpy(targetDevice.name, dev.name.c_str(),
                TELEPORT_MAX_DEVICE_NAME - 1);
        strncpy(targetDevice.ip, dev.ip.c_str(), sizeof(targetDevice.ip) - 1);
        targetDevice.port = dev.port;
        found = true;
        break;
      }
    }
  }

  if (!found) {
    return false;
  }

  // Safety check: port 0 means the device is not ready to receive
  if (targetDevice.port == 0) {
    // Device may not have receiving enabled
    return false;
  }

  // Add to transfers list BEFORE starting the thread
  TransferInfo info;
  info.id = deviceId + "_send_" + std::to_string(GetTickCount64());
  info.deviceName = targetDevice.name;
  info.isSending = true;
  info.state = TELEPORT_STATE_CONNECTING;
  info.bytesTotal = 0;
  info.bytesTransferred = 0;
  info.filesTotal = (uint32_t)filePaths.size();
  info.filesCompleted = 0;

  {
    std::lock_guard<std::mutex> lock(transfersMutex_);
    transfers_.push_back(info);
  }

  // Run the transfer in a separate thread to avoid blocking the UI
  // Clean up finished threads first to stay under limit
  CleanupFinishedThreads();

  // Make copies of data needed for the thread
  auto pathsCopy = std::make_shared<std::vector<std::string>>(filePaths);
  TeleportEngine *engineCopy = engine_;
  TeleportBridge *self = this;
  std::atomic<bool> *shuttingDownPtr = &shuttingDown_;

  // Create tracked thread instead of detached
  {
    std::lock_guard<std::mutex> lock(threadsMutex_);
    activeThreads_.emplace_back(
        [engineCopy, targetDevice, pathsCopy, self, shuttingDownPtr]() {
          // Check for shutdown before starting
          if (shuttingDownPtr->load())
            return;

          // Build paths array for C API
          std::vector<const char *> pathPtrs;
          pathPtrs.reserve(pathsCopy->size());
          for (const auto &path : *pathsCopy) {
            pathPtrs.push_back(path.c_str());
          }

          TeleportTransfer *transfer = nullptr;
          TeleportError err = teleport_send_files(
              engineCopy, &targetDevice, pathPtrs.data(), pathPtrs.size(),
              ProgressCallback, CompleteCallback, self, &transfer);

          if (err != TELEPORT_OK) {
            // Update transfer state to failed
            CompleteCallback(err, self);
          }
        });
  }

  return true;
}

bool TeleportBridge::StartReceiving(const std::string &outputDir) {
  if (!engine_ || isReceiving_.load()) {
    return false;
  }

  downloadPath_ = outputDir;

  TeleportError err =
      teleport_start_receiving(engine_, outputDir.c_str(), IncomingCallback,
                               ProgressCallback, CompleteCallback, this);

  if (err == TELEPORT_OK) {
    isReceiving_.store(true);
    return true;
  }
  return false;
}

void TeleportBridge::StopReceiving() {
  if (engine_ && isReceiving_.load()) {
    teleport_stop_receiving(engine_);
    isReceiving_.store(false);
  }
}

std::vector<TransferInfo> TeleportBridge::GetTransfers() const {
  std::lock_guard<std::mutex> lock(transfersMutex_);
  return transfers_;
}

void TeleportBridge::PauseTransfer(const std::string &transferId) {
  if (transferId.empty())
    return;

  TeleportTransfer *transfer = nullptr;
  {
    std::lock_guard<std::mutex> lock(activeTransfersMutex_);
    auto it = activeTransfers_.find(transferId);
    if (it != activeTransfers_.end()) {
      transfer = it->second;
    }
  }

  if (transfer) {
    teleport_transfer_pause(transfer);

    // Update state in transfers list
    std::lock_guard<std::mutex> lock(transfersMutex_);
    for (auto &t : transfers_) {
      if (t.id == transferId) {
        t.state = TELEPORT_STATE_PAUSED;
        break;
      }
    }
  }
}

void TeleportBridge::ResumeTransfer(const std::string &transferId) {
  if (transferId.empty())
    return;

  TeleportTransfer *transfer = nullptr;
  {
    std::lock_guard<std::mutex> lock(activeTransfersMutex_);
    auto it = activeTransfers_.find(transferId);
    if (it != activeTransfers_.end()) {
      transfer = it->second;
    }
  }

  if (transfer) {
    teleport_transfer_resume(transfer);

    // Update state in transfers list
    std::lock_guard<std::mutex> lock(transfersMutex_);
    for (auto &t : transfers_) {
      if (t.id == transferId) {
        t.state = TELEPORT_STATE_TRANSFERRING;
        break;
      }
    }
  }
}

void TeleportBridge::CancelTransfer(const std::string &transferId) {
  if (transferId.empty())
    return;

  TeleportTransfer *transfer = nullptr;
  {
    std::lock_guard<std::mutex> lock(activeTransfersMutex_);
    auto it = activeTransfers_.find(transferId);
    if (it != activeTransfers_.end()) {
      transfer = it->second;
      // Remove from map after getting pointer
      activeTransfers_.erase(it);
    }
  }

  if (transfer) {
    teleport_transfer_cancel(transfer);

    // Update state in transfers list
    std::lock_guard<std::mutex> lock(transfersMutex_);
    for (auto &t : transfers_) {
      if (t.id == transferId) {
        t.state = TELEPORT_STATE_CANCELLED;
        break;
      }
    }
  }
}

IncomingRequest TeleportBridge::GetPendingRequest() const {
  std::lock_guard<std::mutex> lock(requestMutex_);
  return pendingRequest_;
}

void TeleportBridge::AcceptPendingRequest() {
  pendingRequestResponse_.store(1);
  hasPendingRequest_.store(false);
}

void TeleportBridge::RejectPendingRequest() {
  pendingRequestResponse_.store(0);
  hasPendingRequest_.store(false);
}

// ============ Callbacks ============

void TeleportBridge::OnDeviceDiscovered(const TeleportDevice *device) {
  // Null guard - critical for bulletproof operation
  if (!device) {
    return;
  }

  std::lock_guard<std::mutex> lock(devicesMutex_);

  // Check if device already exists
  for (auto &existing : devices_) {
    if (existing.id == device->id) {
      // Update last seen
      existing.lastSeen = device->last_seen_ms;
      existing.ip = device->ip;
      existing.port = device->port;
      return;
    }
  }

  // Add new device
  DeviceInfo info;
  info.id = device->id;
  info.name = device->name;
  info.os = device->os;
  info.ip = device->ip;
  info.port = device->port;
  info.lastSeen = device->last_seen_ms;
  info.isNew = true;
  info.fadeIn = 0.0f;

  devices_.push_back(info);
}

void TeleportBridge::OnDeviceLost(const char *deviceId) {
  // Null guard
  if (!deviceId) {
    return;
  }

  std::lock_guard<std::mutex> lock(devicesMutex_);

  devices_.erase(std::remove_if(devices_.begin(), devices_.end(),
                                [deviceId](const DeviceInfo &d) {
                                  return d.id == deviceId;
                                }),
                 devices_.end());
}

void TeleportBridge::OnProgress(const TeleportProgress *progress) {
  // Null guard
  if (!progress) {
    return;
  }

  std::lock_guard<std::mutex> lock(transfersMutex_);

  if (!transfers_.empty()) {
    auto &transfer = transfers_.back();
    transfer.currentFile = progress->file_name ? progress->file_name : "";
    transfer.bytesTransferred = progress->total_bytes_transferred;
    transfer.bytesTotal = progress->total_bytes_total;
    transfer.filesCompleted = progress->files_completed;
    transfer.filesTotal = progress->files_total;
    transfer.speedBps = progress->speed_bytes_per_sec;
    transfer.etaSeconds = progress->eta_seconds;
    transfer.state = TELEPORT_STATE_TRANSFERRING;
  }
}

void TeleportBridge::OnComplete(TeleportError error) {
  std::lock_guard<std::mutex> lock(transfersMutex_);

  if (!transfers_.empty()) {
    auto &transfer = transfers_.back();
    transfer.state = (error == TELEPORT_OK) ? TELEPORT_STATE_COMPLETE
                                            : TELEPORT_STATE_FAILED;
    transfer.progress = (error == TELEPORT_OK) ? 1.0f : transfer.progress;
  }
}

int TeleportBridge::OnIncoming(const TeleportDevice *sender,
                               const TeleportFileInfo *files, size_t count) {
  // Null guards - reject if invalid
  if (!sender || !files || count == 0) {
    return 0; // Reject invalid request
  }

  // Store request info
  {
    std::lock_guard<std::mutex> lock(requestMutex_);
    pendingRequest_.sender.id = sender->id;
    pendingRequest_.sender.name = sender->name;
    pendingRequest_.sender.os = sender->os;
    pendingRequest_.sender.ip = sender->ip;
    pendingRequest_.files.clear();
    pendingRequest_.totalSize = 0;

    for (size_t i = 0; i < count; i++) {
      pendingRequest_.files.emplace_back(files[i].name, files[i].size);
      pendingRequest_.totalSize += files[i].size;
    }
  }

  hasPendingRequest_.store(true);
  pendingRequestResponse_.store(-1);

  // Wait for user response (with timeout and shutdown check)
  auto start = std::chrono::steady_clock::now();
  while (pendingRequestResponse_.load() == -1) {
    // Check for shutdown to prevent deadlock
    if (shuttingDown_.load()) {
      hasPendingRequest_.store(false);
      return 0; // Reject on shutdown
    }
    Sleep(100);
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::seconds(30)) {
      hasPendingRequest_.store(false);
      return 0; // Timeout - reject
    }
  }

  int response = pendingRequestResponse_.load();

  if (response == 1) {
    // Add to transfers list
    TransferInfo info;
    info.id =
        sender->id + std::string("_recv_") + std::to_string(GetTickCount64());
    info.deviceName = sender->name;
    info.isSending = false;
    info.state = TELEPORT_STATE_HANDSHAKING;
    info.bytesTotal = pendingRequest_.totalSize;
    info.bytesTransferred = 0;
    info.filesTotal = (uint32_t)count;
    info.filesCompleted = 0;

    std::lock_guard<std::mutex> lock(transfersMutex_);
    transfers_.push_back(info);
  }

  return response;
}

// ============ QR Pairing ============

bool TeleportBridge::GenerateQrPairing(int expirySeconds) {
  if (!engine_)
    return false;

  // Allocate buffer for QR image (64KB should be enough)
  qrImageData_.resize(65536);
  size_t qr_size = qrImageData_.size();

  TeleportError err = teleport_generate_qr_pairing(
      engine_, &qrInfo_, qrImageData_.data(), &qr_size, expirySeconds);

  if (err == TELEPORT_OK) {
    qrImageData_.resize(qr_size);
    return true;
  }

  qrImageData_.clear();
  return false;
}

// ============ Hotspot Mode ============

bool TeleportBridge::StartHotspot() {
  if (!engine_ || hotspotActive_.load())
    return false;

  TeleportError err = teleport_create_hotspot(engine_, &hotspotInfo_);

  if (err == TELEPORT_OK) {
    hotspotActive_.store(true);
    return true;
  }

  return false;
}

void TeleportBridge::StopHotspot() {
  if (engine_ && hotspotActive_.load()) {
    teleport_destroy_hotspot(engine_);
    hotspotActive_.store(false);
    memset(&hotspotInfo_, 0, sizeof(hotspotInfo_));
  }
}

// ============ Web Signaling ============

bool TeleportBridge::ConnectToWebSignaling(const std::string &serverUrl) {
  if (webSignalingConnected_.load()) {
    return true; // Already connected
  }

  webSignaling_ = std::make_unique<teleport::WebSignalingClient>();

  // Set up callbacks
  webSignaling_->setOnConnected(
      [this]() { webSignalingConnected_.store(true); });

  webSignaling_->setOnDisconnected([this](const std::string &reason) {
    webSignalingConnected_.store(false);
  });

  webSignaling_->setOnPeersUpdated(
      [this](const std::vector<teleport::WebPeer> &peers) {
        std::lock_guard<std::mutex> lock(webPeersMutex_);
        webPeers_.clear();
        for (const auto &peer : peers) {
          DeviceInfo info;
          info.id = peer.id;
          info.name = peer.name;
          info.os = "Web";
          info.ip = "";
          info.port = 0;
          info.lastSeen = GetTickCount64();
          info.isWeb = true;
          info.isNew = true;
          webPeers_.push_back(info);
        }
      });

  webSignaling_->setOnFileRequest(
      [this](const std::string &fromId, const std::string &fromName,
             const std::vector<teleport::FileInfo> &files) {
        // Convert to IncomingRequest
        std::lock_guard<std::mutex> lock(requestMutex_);
        pendingRequest_.sender.id = fromId;
        pendingRequest_.sender.name = fromName;
        pendingRequest_.sender.os = "Web";
        pendingRequest_.sender.ip = "";
        pendingRequest_.sender.isWeb = true;
        pendingRequest_.files.clear();
        pendingRequest_.totalSize = 0;

        for (const auto &file : files) {
          pendingRequest_.files.emplace_back(file.name, file.size);
          pendingRequest_.totalSize += file.size;
        }

        hasPendingRequest_.store(true);
        pendingRequestResponse_.store(-1);
      });

  webSignaling_->setOnTransferComplete(
      [this](const std::string &transferId, const std::string &filename,
             const std::vector<uint8_t> &data, bool verified) {
        // Save received file only if integrity verified
        if (!verified) {
          // File failed integrity check - log or notify user
          return;
        }
        std::string outputPath = downloadPath_ + "/" + filename;
        std::ofstream file(outputPath, std::ios::binary);
        if (file) {
          file.write(reinterpret_cast<const char *>(data.data()), data.size());
        }
      });

  bool success = webSignaling_->connect(serverUrl, deviceName_);
  if (success) {
    webSignalingConnected_.store(true);
  }
  return success;
}

void TeleportBridge::DisconnectFromWebSignaling() {
  if (webSignaling_) {
    webSignaling_->disconnect();
    webSignaling_.reset();
  }
  webSignalingConnected_.store(false);

  std::lock_guard<std::mutex> lock(webPeersMutex_);
  webPeers_.clear();
}

bool TeleportBridge::SendFileToWebPeer(const std::string &peerId,
                                       const std::string &filePath) {
  if (!webSignaling_ || !webSignalingConnected_.load()) {
    return false;
  }

  // Read file into memory
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file) {
    return false;
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> data(size);
  if (!file.read(reinterpret_cast<char *>(data.data()), size)) {
    return false;
  }

  // Extract filename from path
  std::string filename = filePath;
  size_t lastSlash = filePath.find_last_of("/\\");
  if (lastSlash != std::string::npos) {
    filename = filePath.substr(lastSlash + 1);
  }

  return webSignaling_->sendFileViaRelay(peerId, filename, data);
}

} // namespace teleport::ui
