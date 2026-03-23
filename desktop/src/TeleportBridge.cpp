/**
 * @file TeleportBridge.cpp
 * @brief Implementation of bridge between UI and Teleport C API
 */

#include "TeleportBridge.h"
#include "WebSignalingClient.h"
#include <cctype>
#include <filesystem>
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

namespace {

struct SendCallbackContext {
  TeleportBridge *bridge = nullptr;
  std::string transferId;
};

bool IsWebSocketUrl(const std::string &url) {
  return url.rfind("ws://", 0) == 0 || url.rfind("wss://", 0) == 0;
}

std::string SanitizeReceivedFilename(const std::string &name) {
  std::string out;
  out.reserve(name.size());

  for (char ch : name) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' ||
        ch == '"' || ch == '<' || ch == '>' || ch == '|' || c < 0x20) {
      out.push_back('_');
    } else {
      out.push_back(ch);
    }
  }

  // BUG FIX (Bug 9): a single check is correct — the while-loop mutated
  // `out` on each iteration (O(N²) insert + wrong termination condition).
  // "." -> "_."  and ".." -> "_.."  — still safe, not a directory separator.
  if (out == "." || out == "..") {
    out = "_" + out;
  }

  if (out.empty()) {
    out = "received_file";
  }

  return out;
}

std::string GetSupportedSignalingUrl(const std::string &url) {
  if (!IsWebSocketUrl(url)) {
    return url;
  }

#ifndef USE_OPENSSL
  // If TLS support is not available in this build, downgrade wss:// to ws://
  // so configured endpoints still have a chance to connect.
  if (url.rfind("wss://", 0) == 0) {
    return std::string("ws://") + url.substr(6);
  }
#endif

  return url;
}

} // namespace

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

static void SendProgressCallback(const TeleportProgress *progress,
                                 void *userData) {
  auto *ctx = static_cast<SendCallbackContext *>(userData);
  if (!ctx || !ctx->bridge) {
    return;
  }
  ctx->bridge->OnProgress(ctx->transferId, progress);
}

static void SendCompleteCallback(TeleportError error, void *userData) {
  auto *ctx = static_cast<SendCallbackContext *>(userData);
  if (!ctx) {
    return;
  }

  TeleportBridge *bridge = ctx->bridge;
  std::string transferId = ctx->transferId;
  delete ctx;

  if (!bridge) {
    return;
  }
  bridge->OnComplete(transferId, error);
}

static void ReceiveProgressCallback(const TeleportProgress *progress,
                                    void *userData) {
  auto *bridge = static_cast<TeleportBridge *>(userData);
  if (!bridge) {
    return;
  }
  bridge->OnProgress(progress);
}

static void ReceiveCompleteCallback(TeleportError error, void *userData) {
  auto *bridge = static_cast<TeleportBridge *>(userData);
  if (!bridge) {
    return;
  }
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

  // Connect to signaling server (best effort)
  ConnectToPreferredSignaling();

  return true;
}

void TeleportBridge::Shutdown() {
  // Signal all threads to stop
  shuttingDown_.store(true);

  // Join the signaling connection thread first - it may be mid-connect
  // (DNS resolution / TLS handshake) and must observe shuttingDown_ = true.
  if (signalingConnectThread_ && signalingConnectThread_->joinable()) {
    signalingConnectThread_->join();
  }
  signalingConnectThread_.reset();
  isSignalingConnecting_.store(false);

  StopDiscovery();
  StopReceiving();

  // Wait for all active transfer threads to complete before touching the
  // signaling connection they may still be using.
  {
    std::lock_guard<std::mutex> lock(threadsMutex_);
    for (auto &handle : activeThreads_) {
      if (handle.worker.joinable()) {
        handle.worker.join();
      }
    }
    activeThreads_.clear();
  }

  // Release any remaining transfer handles before engine teardown.
  {
    std::lock_guard<std::mutex> lock(activeTransfersMutex_);
    for (auto &entry : activeTransfers_) {
      teleport_transfer_destroy(entry.second);
    }
    activeTransfers_.clear();
  }

  // Disconnect web signaling AFTER all threads are done so no thread tries
  // to use the connection while it is being torn down.
  DisconnectFromWebSignaling();

  if (engine_) {
    teleport_destroy(engine_);
    engine_ = nullptr;
  }

  shuttingDown_.store(false);
}

void TeleportBridge::CleanupFinishedThreads() {
  std::lock_guard<std::mutex> lock(threadsMutex_);
  for (auto it = activeThreads_.begin(); it != activeThreads_.end();) {
    if (it->done && it->done->load()) {
      if (it->worker.joinable()) {
        it->worker.join();
      }
      it = activeThreads_.erase(it);
    } else {
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

  // Auto-reconnect to signaling server if disconnected.
  // Done off the UI thread to avoid blocking during DNS resolution / TLS
  // handshake (which can take up to ~10 s on slow networks).
  if (!webSignalingConnected_.load() && !shuttingDown_.load() &&
      !isSignalingConnecting_.load()) {
    auto timeSinceLastAttempt =
        std::chrono::steady_clock::now() - lastReconnectAttempt_;
    // First attempt is immediate; subsequent attempts are rate-limited to
    // once every 30 seconds so we don't hammer the signaling server.
    if (firstReconnectAttempt_.load() ||
        timeSinceLastAttempt > std::chrono::seconds(30)) {
      firstReconnectAttempt_.store(false);
      lastReconnectAttempt_ = std::chrono::steady_clock::now();
      isSignalingConnecting_.store(true);

      // The outer guard (!isSignalingConnecting_.load()) guarantees the
      // previous thread, if any, has already finished executing and stored
      // false.  We MUST join (or detach) it before assigning a new thread
      // object; destroying a joinable std::thread calls std::terminate().
      if (signalingConnectThread_) {
        if (signalingConnectThread_->joinable()) {
          signalingConnectThread_->join();
        }
        signalingConnectThread_.reset();
      }

      // Spawn a background thread so ConnectToPreferredSignaling() (which
      // performs DNS lookup + TLS handshake) never runs on the UI thread.
      signalingConnectThread_ = std::make_unique<std::thread>([this]() {
        ConnectToPreferredSignaling();
        isSignalingConnecting_.store(false);
      });
    }
  } else if (webSignalingConnected_.load()) {
    // Reset so the next disconnect triggers an immediate reconnect attempt.
    firstReconnectAttempt_.store(true);
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
    // Send via web signaling relay - must be done off the UI thread to avoid
    // blocking during requestFileSend() (waits for peer acceptance) and the
    // actual data streaming.
    if (!webSignaling_ || !webSignalingConnected_.load()) {
      return false;
    }

    // Build file info list (fast metadata only, no I/O)
    std::vector<teleport::FileInfo> fileInfos;
    fileInfos.reserve(filePaths.size());
    uint64_t totalBytes = 0;
    for (const auto &path : filePaths) {
      std::error_code ec;
      const auto fsize = std::filesystem::file_size(path, ec);
      if (ec)
        return false;
      teleport::FileInfo info;
      info.size = static_cast<size_t>(fsize);
      const auto slashPos = path.find_last_of("/\\");
      info.name =
          (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
      info.mimeType = "application/octet-stream";
      fileInfos.push_back(std::move(info));
      totalBytes += fsize;
    }

    // Pre-register the transfer so the Transfers view shows it immediately,
    // before the background thread even starts the handshake.
    static std::atomic<uint64_t> webTransferSeq{0};
    const uint64_t seq =
        webTransferSeq.fetch_add(1, std::memory_order_relaxed);
    const std::string transferId =
        deviceId + "_web_send_" + std::to_string(GetTickCount64()) + "_" +
        std::to_string(seq);

    {
      TransferInfo tinfo;
      tinfo.id = transferId;
      tinfo.deviceName = webPeerName;
      tinfo.isSending = true;
      tinfo.state = TELEPORT_STATE_CONNECTING;
      tinfo.bytesTotal = totalBytes;
      tinfo.bytesTransferred = 0;
      tinfo.filesTotal = (uint32_t)filePaths.size();
      tinfo.filesCompleted = 0;
      tinfo.currentFile = "Waiting for acceptance...";
      tinfo.progress = 0.0f;
      std::lock_guard<std::mutex> lock(transfersMutex_);
      transfers_.push_back(std::move(tinfo));
    }

    // Enforce max concurrency; roll back the pre-registered entry if full.
    CleanupFinishedThreads();
    {
      std::lock_guard<std::mutex> lock(threadsMutex_);
      if (activeThreads_.size() >= kMaxTransferThreads) {
        std::lock_guard<std::mutex> tlock(transfersMutex_);
        transfers_.erase(
            std::remove_if(transfers_.begin(), transfers_.end(),
                           [&](const TransferInfo &t) {
                             return t.id == transferId;
                           }),
            transfers_.end());
        return false;
      }
    }

    // Capture everything the background thread needs by value.
    auto pathsCopy = std::make_shared<std::vector<std::string>>(filePaths);
    auto fileInfosCopy =
        std::make_shared<std::vector<teleport::FileInfo>>(fileInfos);
    auto transferIdCopy = std::make_shared<std::string>(transferId);
    auto deviceIdCopy = std::make_shared<std::string>(deviceId);
    auto doneFlag = std::make_shared<std::atomic<bool>>(false);
    TeleportBridge *self = this;

    // Helper lambda: update transfer state under transfersMutex_.
    // Must NOT be called while transfersMutex_ is already held.
    auto updateState = [self, transferIdCopy](TeleportTransferState state,
                                              const std::string &file = "") {
      std::lock_guard<std::mutex> lock(self->transfersMutex_);
      auto it = std::find_if(
          self->transfers_.begin(), self->transfers_.end(),
          [&](const TransferInfo &t) { return t.id == *transferIdCopy; });
      if (it != self->transfers_.end()) {
        it->state = state;
        if (!file.empty())
          it->currentFile = file;
        if (state == TELEPORT_STATE_COMPLETE) {
          it->progress = 1.0f;
          it->filesCompleted = it->filesTotal;
          it->bytesTransferred = it->bytesTotal;
        }
      }
    };

    {
      std::lock_guard<std::mutex> lock(threadsMutex_);
      TransferThreadHandle handle;
      handle.done = doneFlag;
      handle.worker = std::thread(
          [self, pathsCopy, fileInfosCopy, transferIdCopy, deviceIdCopy,
           doneFlag, updateState]() {
            // RAII guard: mark done even if we return early via exception.
            struct DoneGuard {
              std::shared_ptr<std::atomic<bool>> done;
              ~DoneGuard() {
                if (done)
                  done->store(true);
              }
            } guard{doneFlag};

            if (self->shuttingDown_.load()) {
              updateState(TELEPORT_STATE_FAILED, "Cancelled");
              return;
            }

            if (!self->webSignaling_ ||
                !self->webSignalingConnected_.load()) {
              updateState(TELEPORT_STATE_FAILED, "Not connected");
              return;
            }

            // Request acceptance - blocks until peer responds or timeout.
            if (!self->webSignaling_->requestFileSend(*deviceIdCopy,
                                                      *fileInfosCopy)) {
              updateState(TELEPORT_STATE_FAILED, "Transfer rejected");
              return;
            }

            updateState(TELEPORT_STATE_TRANSFERRING, "Sending...");

            // Stream each file sequentially.
            for (size_t i = 0; i < pathsCopy->size(); ++i) {
              if (self->shuttingDown_.load())
                break;

              const auto &path = (*pathsCopy)[i];
              const std::string fname = [&path]() {
                auto pos = path.find_last_of("/\\");
                return pos == std::string::npos ? path : path.substr(pos + 1);
              }();
              updateState(TELEPORT_STATE_TRANSFERRING, fname);

              if (!self->webSignaling_ ||
                  !self->webSignalingConnected_.load()) {
                updateState(TELEPORT_STATE_FAILED, "Connection lost");
                return;
              }

              if (!self->webSignaling_->streamFileOnline(*deviceIdCopy,
                                                         path)) {
                updateState(TELEPORT_STATE_FAILED, "Send failed: " + fname);
                return;
              }

              // Increment files-completed counter (separate lock from updateState).
              {
                std::lock_guard<std::mutex> lock(self->transfersMutex_);
                auto it = std::find_if(
                    self->transfers_.begin(), self->transfers_.end(),
                    [&](const TransferInfo &t) {
                      return t.id == *transferIdCopy;
                    });
                if (it != self->transfers_.end()) {
                  it->filesCompleted = (uint32_t)(i + 1);
                }
              }
            }

            if (!self->shuttingDown_.load()) {
              updateState(TELEPORT_STATE_COMPLETE, "Complete");
            }
          });
      activeThreads_.emplace_back(std::move(handle));
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

  // Keep thread usage bounded and reclaim any completed workers.
  CleanupFinishedThreads();
  {
    std::lock_guard<std::mutex> lock(threadsMutex_);
    if (activeThreads_.size() >= kMaxTransferThreads) {
      return false;
    }
  }

  static std::atomic<uint64_t> transferSeq{0};
  const uint64_t seq = transferSeq.fetch_add(1, std::memory_order_relaxed);
  const std::string transferId = deviceId + "_send_" +
                                 std::to_string(GetTickCount64()) + "_" +
                                 std::to_string(seq);

  // Add to transfers list BEFORE starting the thread
  TransferInfo info;
  info.id = transferId;
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

  // Make copies of data needed for the thread
  auto pathsCopy = std::make_shared<std::vector<std::string>>(filePaths);
  auto transferIdCopy = std::make_shared<std::string>(transferId);
  TeleportEngine *engineCopy = engine_;
  TeleportBridge *self = this;
  std::atomic<bool> *shuttingDownPtr = &shuttingDown_;
  auto doneFlag = std::make_shared<std::atomic<bool>>(false);

  // Create tracked thread instead of detached
  {
    std::lock_guard<std::mutex> lock(threadsMutex_);
    TransferThreadHandle handle;
    handle.done = doneFlag;
    handle.worker =
        std::thread([engineCopy, targetDevice, pathsCopy, transferIdCopy, self,
                     shuttingDownPtr, doneFlag]() {
          // Mark completion on every exit path.
          struct DoneGuard {
            std::shared_ptr<std::atomic<bool>> done;
            ~DoneGuard() {
              if (done) {
                done->store(true);
              }
            }
          } guard{doneFlag};

          // Check for shutdown before starting.
          if (shuttingDownPtr->load()) {
            return;
          }

          // Build paths array for C API.
          std::vector<const char *> pathPtrs;
          pathPtrs.reserve(pathsCopy->size());
          for (const auto &path : *pathsCopy) {
            pathPtrs.push_back(path.c_str());
          }

          auto *callbackCtx = new SendCallbackContext{self, *transferIdCopy};

          TeleportTransfer *transfer = nullptr;
          TeleportError err =
              teleport_send_files(engineCopy, &targetDevice, pathPtrs.data(),
                                  pathPtrs.size(), SendProgressCallback,
                                  SendCompleteCallback, callbackCtx, &transfer);

          if (err != TELEPORT_OK) {
            // teleport_send_files failed before transfer start. Complete
            // callback will not fire, so process failure and clean context now.
            self->OnComplete(*transferIdCopy, err);
            delete callbackCtx;
            return;
          }

          // Track transfer pointer for pause/resume/cancel controls.
          {
            std::lock_guard<std::mutex> transferLock(self->activeTransfersMutex_);
            self->activeTransfers_[*transferIdCopy] = transfer;
          }
        });
    activeThreads_.emplace_back(std::move(handle));
  }

  return true;
}

bool TeleportBridge::StartReceiving(const std::string &outputDir) {
  if (!engine_ || isReceiving_.load() || outputDir.empty()) {
    return false;
  }

  downloadPath_ = outputDir;

  {
    std::lock_guard<std::mutex> lock(receiveTransferMutex_);
    activeReceiveTransferId_.clear();
  }

  TeleportError err =
      teleport_start_receiving(engine_, outputDir.c_str(), IncomingCallback,
                               ReceiveProgressCallback, ReceiveCompleteCallback,
                               this);

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
    std::lock_guard<std::mutex> lock(receiveTransferMutex_);
    activeReceiveTransferId_.clear();
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
    teleport_transfer_destroy(transfer);

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
  std::string webSenderId;
  {
    std::lock_guard<std::mutex> lock(requestMutex_);
    if (pendingRequest_.sender.isWeb) {
      webSenderId = pendingRequest_.sender.id;
    }
  }

  if (!webSenderId.empty() && webSignaling_) {
    webSignaling_->acceptFileRequest(webSenderId);
  }

  pendingRequestResponse_.store(1);
  hasPendingRequest_.store(false);
}

void TeleportBridge::RejectPendingRequest() {
  std::string webSenderId;
  {
    std::lock_guard<std::mutex> lock(requestMutex_);
    if (pendingRequest_.sender.isWeb) {
      webSenderId = pendingRequest_.sender.id;
    }
  }

  if (!webSenderId.empty() && webSignaling_) {
    webSignaling_->rejectFileRequest(webSenderId);
  }

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

  std::string receiveTransferId;
  {
    std::lock_guard<std::mutex> lock(receiveTransferMutex_);
    receiveTransferId = activeReceiveTransferId_;
  }

  if (!receiveTransferId.empty()) {
    OnProgress(receiveTransferId, progress);
    return;
  }

  std::lock_guard<std::mutex> lock(transfersMutex_);

  // Fallback path for receive progress if transfer ID is not tracked yet.
  for (auto it = transfers_.rbegin(); it != transfers_.rend(); ++it) {
    if (!it->isSending && it->state != TELEPORT_STATE_COMPLETE &&
        it->state != TELEPORT_STATE_FAILED &&
        it->state != TELEPORT_STATE_CANCELLED) {
      it->currentFile = progress->file_name ? progress->file_name : "";
      it->bytesTransferred = progress->total_bytes_transferred;
      it->bytesTotal = progress->total_bytes_total;
      it->filesCompleted = progress->files_completed;
      it->filesTotal = progress->files_total;
      it->speedBps = progress->speed_bytes_per_sec;
      it->etaSeconds = progress->eta_seconds;
      it->state = TELEPORT_STATE_TRANSFERRING;
      return;
    }
  }
}

void TeleportBridge::OnComplete(TeleportError error) {
  std::string receiveTransferId;
  {
    std::lock_guard<std::mutex> lock(receiveTransferMutex_);
    receiveTransferId = activeReceiveTransferId_;
    activeReceiveTransferId_.clear();
  }

  if (!receiveTransferId.empty()) {
    OnComplete(receiveTransferId, error);
    return;
  }

  std::lock_guard<std::mutex> lock(transfersMutex_);

  // Fallback path for receive completion if transfer ID is not tracked.
  for (auto it = transfers_.rbegin(); it != transfers_.rend(); ++it) {
    if (!it->isSending && it->state != TELEPORT_STATE_COMPLETE &&
        it->state != TELEPORT_STATE_FAILED &&
        it->state != TELEPORT_STATE_CANCELLED) {
      it->state =
          (error == TELEPORT_OK) ? TELEPORT_STATE_COMPLETE : TELEPORT_STATE_FAILED;
      if (error == TELEPORT_OK) {
        it->progress = 1.0f;
      }
      return;
    }
  }
}

void TeleportBridge::OnProgress(const std::string &transferId,
                                const TeleportProgress *progress) {
  if (!progress || transferId.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(transfersMutex_);
  for (auto it = transfers_.rbegin(); it != transfers_.rend(); ++it) {
    if (it->id == transferId) {
      it->currentFile = progress->file_name ? progress->file_name : "";
      it->bytesTransferred = progress->total_bytes_transferred;
      it->bytesTotal = progress->total_bytes_total;
      it->filesCompleted = progress->files_completed;
      it->filesTotal = progress->files_total;
      it->speedBps = progress->speed_bytes_per_sec;
      it->etaSeconds = progress->eta_seconds;
      it->state = TELEPORT_STATE_TRANSFERRING;
      return;
    }
  }
}

void TeleportBridge::OnComplete(const std::string &transferId,
                                TeleportError error) {
  if (transferId.empty()) {
    return;
  }

  TeleportTransfer *transfer = nullptr;
  {
    std::lock_guard<std::mutex> lock(activeTransfersMutex_);
    auto it = activeTransfers_.find(transferId);
    if (it != activeTransfers_.end()) {
      transfer = it->second;
      activeTransfers_.erase(it);
    }
  }

  if (transfer) {
    teleport_transfer_destroy(transfer);
  }

  std::lock_guard<std::mutex> lock(transfersMutex_);
  for (auto it = transfers_.rbegin(); it != transfers_.rend(); ++it) {
    if (it->id == transferId) {
      it->state =
          (error == TELEPORT_OK) ? TELEPORT_STATE_COMPLETE : TELEPORT_STATE_FAILED;
      if (error == TELEPORT_OK) {
        it->progress = 1.0f;
      }
      return;
    }
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
    uint64_t totalSize = 0;
    {
      std::lock_guard<std::mutex> lock(requestMutex_);
      totalSize = pendingRequest_.totalSize;
    }

    // Add to transfers list
    TransferInfo info;
    info.id =
        sender->id + std::string("_recv_") + std::to_string(GetTickCount64());
    info.deviceName = sender->name;
    info.isSending = false;
    info.state = TELEPORT_STATE_HANDSHAKING;
    info.bytesTotal = totalSize;
    info.bytesTransferred = 0;
    info.filesTotal = (uint32_t)count;
    info.filesCompleted = 0;

    {
      std::lock_guard<std::mutex> lock(transfersMutex_);
      transfers_.push_back(info);
    }
    {
      std::lock_guard<std::mutex> lock(receiveTransferMutex_);
      activeReceiveTransferId_ = info.id;
    }
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

void TeleportBridge::SetSignalingServerUrl(const std::string &url) {
  if (!url.empty() && IsWebSocketUrl(url)) {
    signalingServerUrl_ = url;
  }
}

void TeleportBridge::SetDownloadPath(const std::string &path) {
  downloadPath_ = path;
  if (webSignaling_) {
    webSignaling_->setDownloadPath(path);
  }
}

bool TeleportBridge::ConnectToPreferredSignaling() {
  if (webSignalingConnected_.load()) {
    return true;
  }

  // Build the ordered list of servers to try — mirrors the web version's
  // getRankedSignalingServers() logic:
  //   1. User-configured preferred URL (saved in settings)
  //   2. Render primary (the live production server the web app uses)
  //   3. Render backup
  //   4. Local dev server (localhost:3000)
  static const char *kRenderPrimary  = "wss://teleport-signaling.onrender.com";
  static const char *kRenderBackup   = "wss://teleport-signaling-backup.onrender.com";
  static const char *kLocalDev       = "ws://localhost:3000";

  std::vector<std::string> candidates;

  const std::string preferredUrl = GetSupportedSignalingUrl(signalingServerUrl_);
  if (!preferredUrl.empty()) {
    candidates.push_back(preferredUrl);
  }

  // Always include Render primary & backup unless they are already the preferred URL
  if (preferredUrl != kRenderPrimary) {
    candidates.push_back(GetSupportedSignalingUrl(kRenderPrimary));
  }
  if (preferredUrl != kRenderBackup) {
    candidates.push_back(GetSupportedSignalingUrl(kRenderBackup));
  }

  // Local dev as last resort
  candidates.push_back(kLocalDev);

  for (const auto &url : candidates) {
    if (url.empty())
      continue;
    if (webSignalingConnected_.load())
      return true; // connected by a previous iteration
    if (shuttingDown_.load())
      return false;
    if (ConnectToWebSignaling(url)) {
      return true;
    }
  }

  return false;
}

bool TeleportBridge::ConnectToWebSignaling(const std::string &serverUrl) {
  const std::string effectiveUrl = GetSupportedSignalingUrl(serverUrl);
  if (effectiveUrl.empty() || !IsWebSocketUrl(effectiveUrl)) {
    return false;
  }

  if (webSignalingConnected_.load()) {
    return true; // Already connected
  }

  webSignaling_ = std::make_unique<teleport::WebSignalingClient>();
  webSignaling_->setDownloadPath(downloadPath_);

  // Set up callbacks
  webSignaling_->setOnConnected(
      [this]() { webSignalingConnected_.store(true); });

  webSignaling_->setOnDisconnected([this](const std::string &reason) {
    (void)reason;
    webSignalingConnected_.store(false);
    firstReconnectAttempt_.store(true);
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

  webSignaling_->setOnTransferProgress(
      [this](const teleport::TransferProgress &progress) {
        const bool isSending = !progress.targetPeerId.empty();
        const std::string prefix = isSending ? "web_send_" : "web_recv_";
        const std::string transferKey = prefix + progress.transferId;

        auto mapState = [](teleport::TransferState state) {
          switch (state) {
          case teleport::TransferState::Completed:
            return TELEPORT_STATE_COMPLETE;
          case teleport::TransferState::Failed:
          case teleport::TransferState::Cancelled:
            return TELEPORT_STATE_FAILED;
          case teleport::TransferState::Paused:
          case teleport::TransferState::Pending:
          case teleport::TransferState::InProgress:
          default:
            return TELEPORT_STATE_TRANSFERRING;
          }
        };

        std::lock_guard<std::mutex> lock(transfersMutex_);
        auto it = std::find_if(transfers_.begin(), transfers_.end(),
                               [&](const TransferInfo &item) {
                                 return item.id == transferKey;
                               });

        if (it == transfers_.end()) {
          TransferInfo info;
          info.id = transferKey;
          info.deviceName = isSending ? "Web Peer" : "Incoming Web Peer";
          info.currentFile = progress.filename;
          info.bytesTransferred = progress.transferredBytes;
          info.bytesTotal = progress.totalBytes;
          info.filesCompleted = 0;
          info.filesTotal = std::max(1, progress.totalFiles);
          info.speedBps = progress.speedBytesPerSecond;
          info.etaSeconds = 0;
          info.state = mapState(progress.state);
          info.isSending = isSending;
          info.progress =
              (progress.totalBytes > 0)
                  ? static_cast<float>(progress.transferredBytes) /
                        static_cast<float>(progress.totalBytes)
                  : 0.0f;
          if (info.state == TELEPORT_STATE_COMPLETE) {
            info.progress = 1.0f;
            info.filesCompleted = info.filesTotal;
          }
          transfers_.push_back(std::move(info));
          return;
        }

        it->currentFile = progress.filename;
        it->bytesTransferred = progress.transferredBytes;
        it->bytesTotal = progress.totalBytes;
        it->filesTotal = std::max(1, progress.totalFiles);
        it->speedBps = progress.speedBytesPerSecond;
        it->state = mapState(progress.state);
        it->progress =
            (progress.totalBytes > 0)
                ? static_cast<float>(progress.transferredBytes) /
                      static_cast<float>(progress.totalBytes)
                : it->progress;
        if (it->state == TELEPORT_STATE_COMPLETE) {
          it->progress = 1.0f;
          it->filesCompleted = it->filesTotal;
        }
      });

  webSignaling_->setOnError(
      [this](teleport::SignalingError error, const std::string &message) {
        if (error == teleport::SignalingError::None) {
          return;
        }

        TransferInfo info;
        info.id = "web_error_" + std::to_string(GetTickCount64());
        info.deviceName = "Web Transfer";
        info.currentFile = message;
        info.bytesTransferred = 0;
        info.bytesTotal = 0;
        info.filesCompleted = 0;
        info.filesTotal = 1;
        info.speedBps = 0.0;
        info.etaSeconds = 0;
        info.state = TELEPORT_STATE_FAILED;
        info.isSending = true;
        info.progress = 0.0f;

        std::lock_guard<std::mutex> lock(transfersMutex_);
        transfers_.push_back(std::move(info));
      });

  webSignaling_->setOnTransferComplete(
      [this](const std::string &transferId, const std::string &filename,
             const std::vector<uint8_t> &data, bool verified) {
        if (!verified) {
          // Integrity check failed — log to transfers list as failed
          std::lock_guard<std::mutex> lock(transfersMutex_);
          const std::string transferKey = "web_recv_" + transferId;
          auto it =
              std::find_if(transfers_.begin(), transfers_.end(),
                           [&](const TransferInfo &item) {
                             return item.id == transferKey;
                           });
          if (it != transfers_.end()) {
            it->state = TELEPORT_STATE_FAILED;
            it->currentFile = "Integrity check failed";
          } else {
            TransferInfo info;
            info.id = transferKey;
            info.deviceName = "Web Peer";
            info.currentFile = "Integrity check failed";
            info.state = TELEPORT_STATE_FAILED;
            info.isSending = false;
            transfers_.push_back(std::move(info));
          }
          return;
        }
        if (filename.empty()) {
          return;
        }

        // ── Streaming path: data is empty because the file is already on disk ──
        // In streaming mode, WebSignalingClient saved the file and passes its
        // final path as `filename`. We just need to update the transfer state.
        if (data.empty()) {
          // The file has already been saved by WebSignalingClient.
          // Extract display name for UI (last component of path).
          std::string displayName = filename;
          {
            size_t sl = filename.find_last_of("/\\");
            if (sl != std::string::npos)
              displayName = filename.substr(sl + 1);
          }

          std::lock_guard<std::mutex> lock(transfersMutex_);
          const std::string transferKey = "web_recv_" + transferId;
          auto it =
              std::find_if(transfers_.begin(), transfers_.end(),
                           [&](const TransferInfo &item) {
                             return item.id == transferKey;
                           });
          if (it != transfers_.end()) {
            it->currentFile = displayName;
            it->state = TELEPORT_STATE_COMPLETE;
            it->progress = 1.0f;
            it->filesCompleted = it->filesTotal;
          } else {
            TransferInfo info;
            info.id = transferKey;
            info.deviceName = "Web Peer";
            info.currentFile = displayName;
            info.bytesTransferred = 0; // unknown at this point for large files
            info.bytesTotal = 0;
            info.filesCompleted = 1;
            info.filesTotal = 1;
            info.speedBps = 0.0;
            info.etaSeconds = 0;
            info.state = TELEPORT_STATE_COMPLETE;
            info.isSending = false;
            info.progress = 1.0f;
            transfers_.push_back(std::move(info));
          }
          return;
        }

        // ── In-memory path: small file, data vector contains the bytes ──
        const std::string safeName = SanitizeReceivedFilename(filename);
        std::string outputPath = downloadPath_ + "/" + safeName;

        // Ensure download directory exists before writing
#ifdef _WIN32
        SHCreateDirectoryExA(nullptr, downloadPath_.c_str(), nullptr);
#else
        std::filesystem::create_directories(downloadPath_);
#endif

        std::ofstream file(outputPath, std::ios::binary);
        if (file) {
          file.write(reinterpret_cast<const char *>(data.data()), data.size());

          std::lock_guard<std::mutex> lock(transfersMutex_);
          const std::string transferKey = "web_recv_" + transferId;
          auto it =
              std::find_if(transfers_.begin(), transfers_.end(),
                           [&](const TransferInfo &item) {
                             return item.id == transferKey;
                           });

          if (it == transfers_.end()) {
            TransferInfo info;
            info.id = transferKey;
            info.deviceName = "Web Peer";
            info.currentFile = safeName;
            info.bytesTransferred = data.size();
            info.bytesTotal = data.size();
            info.filesCompleted = 1;
            info.filesTotal = 1;
            info.speedBps = 0.0;
            info.etaSeconds = 0;
            info.state = TELEPORT_STATE_COMPLETE;
            info.isSending = false;
            info.progress = 1.0f;
            transfers_.push_back(std::move(info));
          } else {
            it->currentFile = safeName;
            it->bytesTransferred = data.size();
            it->bytesTotal = data.size();
            it->filesCompleted = 1;
            it->filesTotal = std::max<uint32_t>(1, it->filesTotal);
            it->state = TELEPORT_STATE_COMPLETE;
            it->progress = 1.0f;
          }
        } else {
          // File write failed
          TransferInfo info;
          info.id = "web_recv_" + transferId;
          info.deviceName = "Web Peer";
          info.currentFile = safeName;
          info.bytesTransferred = 0;
          info.bytesTotal = data.size();
          info.filesCompleted = 0;
          info.filesTotal = 1;
          info.speedBps = 0.0;
          info.etaSeconds = 0;
          info.state = TELEPORT_STATE_FAILED;
          info.isSending = false;
          info.progress = 0.0f;

          std::lock_guard<std::mutex> lock(transfersMutex_);
          transfers_.push_back(std::move(info));
        }
      });

  bool success = webSignaling_->connect(effectiveUrl, deviceName_);
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

  return webSignaling_->streamFileOnline(peerId, filePath);
}

} // namespace teleport::ui
