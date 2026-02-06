/**
 * @file TeleportBridge.h
 * @brief Bridge between UI and Teleport C core API
 */

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <teleport/teleport.h>
#include <thread>
#include <vector>

// Forward declare WebSignalingClient
namespace teleport {
class WebSignalingClient;
}

namespace teleport::ui {

/**
 * @brief Device information for UI display
 */
struct DeviceInfo {
  std::string id;
  std::string name;
  std::string os;
  std::string ip;
  uint16_t port;
  int64_t lastSeen;
  bool isNew = false;  // For animation
  float fadeIn = 0.0f; // Animation progress
  bool isWeb = false;  // True if this is a web peer (via signaling)
};

/**
 * @brief Transfer information for UI display
 */
struct TransferInfo {
  std::string id;
  std::string deviceName;
  std::string currentFile;
  uint64_t bytesTransferred;
  uint64_t bytesTotal;
  uint32_t filesCompleted;
  uint32_t filesTotal;
  double speedBps;
  int32_t etaSeconds;
  TeleportTransferState state;
  bool isSending;
  float progress = 0.0f; // Animated progress 0-1
};

/**
 * @brief Incoming transfer request
 */
struct IncomingRequest {
  DeviceInfo sender;
  std::vector<std::pair<std::string, uint64_t>> files; // name, size
  uint64_t totalSize;
};

/**
 * @brief Bridge wrapping Teleport C API for the UI
 */
class TeleportBridge {
public:
  TeleportBridge();
  ~TeleportBridge();

  /**
   * @brief Initialize the Teleport engine
   */
  bool Initialize();

  /**
   * @brief Update state (call each frame)
   */
  void Update();

  /**
   * @brief Shutdown the engine
   */
  void Shutdown();

  // ============ Discovery ============

  /**
   * @brief Start device discovery
   */
  bool StartDiscovery();

  /**
   * @brief Stop device discovery
   */
  void StopDiscovery();

  /**
   * @brief Check if discovery is active
   */
  bool IsDiscovering() const { return isDiscovering_.load(); }

  /**
   * @brief Get list of discovered devices
   */
  std::vector<DeviceInfo> GetDevices() const;

  /**
   * @brief Add a device manually by IP address
   */
  void AddManualDevice(const char *ip, uint16_t port, const char *name);

  // ============ Sending ============

  /**
   * @brief Send files to a device
   */
  bool SendFiles(const std::string &deviceId,
                 const std::vector<std::string> &filePaths);

  // ============ Receiving ============

  /**
   * @brief Start receiving mode
   */
  bool StartReceiving(const std::string &outputDir);

  /**
   * @brief Stop receiving mode
   */
  void StopReceiving();

  /**
   * @brief Check if receiving is active
   */
  bool IsReceiving() const { return isReceiving_.load(); }

  /**
   * @brief Get download directory
   */
  std::string GetDownloadPath() const { return downloadPath_; }

  /**
   * @brief Set download directory
   */
  void SetDownloadPath(const std::string &path) { downloadPath_ = path; }

  /**
   * @brief Set device name
   */
  void SetDeviceName(const char *name) { deviceName_ = name ? name : ""; }

  /**
   * @brief Get device name
   */
  std::string GetDeviceName() const { return deviceName_; }

  // ============ Transfers ============

  /**
   * @brief Get active transfers
   */
  std::vector<TransferInfo> GetTransfers() const;

  /**
   * @brief Pause a transfer
   */
  void PauseTransfer(const std::string &transferId);

  /**
   * @brief Resume a transfer
   */
  void ResumeTransfer(const std::string &transferId);

  /**
   * @brief Cancel a transfer
   */
  void CancelTransfer(const std::string &transferId);

  // ============ Incoming Requests ============

  /**
   * @brief Check for pending incoming request
   */
  bool HasPendingRequest() const { return hasPendingRequest_.load(); }

  /**
   * @brief Get pending incoming request
   */
  IncomingRequest GetPendingRequest() const;

  /**
   * @brief Accept pending request
   */
  void AcceptPendingRequest();

  /**
   * @brief Reject pending request
   */
  void RejectPendingRequest();

  // ============ Callbacks for internal use ============

  void OnDeviceDiscovered(const TeleportDevice *device);
  void OnDeviceLost(const char *deviceId);
  void OnProgress(const TeleportProgress *progress);
  void OnComplete(TeleportError error);
  int OnIncoming(const TeleportDevice *sender, const TeleportFileInfo *files,
                 size_t count);

  // ============ QR Pairing ============

  /**
   * @brief Generate QR pairing info and bitmap
   */
  bool GenerateQrPairing(int expirySeconds = 300);

  /**
   * @brief Get current QR pairing info
   */
  TeleportQrPairingInfo GetQrPairingInfo() const { return qrInfo_; }

  /**
   * @brief Get QR bitmap data (BMP format)
   */
  const std::vector<uint8_t> &GetQrImageData() const { return qrImageData_; }

  // ============ Hotspot Mode ============

  /**
   * @brief Create and start hotspot
   */
  bool StartHotspot();

  /**
   * @brief Stop hotspot
   */
  void StopHotspot();

  /**
   * @brief Check if hotspot is active
   */
  bool IsHotspotActive() const { return hotspotActive_.load(); }

  /**
   * @brief Get hotspot info
   */
  TeleportHotspotInfo GetHotspotInfo() const { return hotspotInfo_; }

  // ============ Web Signaling (for web <-> desktop transfer) ============

  /**
   * @brief Connect to web signaling server
   */
  bool ConnectToWebSignaling(
      const std::string &serverUrl = "ws://teleport-signaling.onrender.com");

  /**
   * @brief Disconnect from web signaling
   */
  void DisconnectFromWebSignaling();

  /**
   * @brief Check if connected to web signaling
   */
  bool IsWebSignalingConnected() const { return webSignalingConnected_.load(); }

  /**
   * @brief Send file to a web peer via relay
   */
  bool SendFileToWebPeer(const std::string &peerId,
                         const std::string &filePath);

private:
  TeleportEngine *engine_ = nullptr;
  TeleportTransfer *currentTransfer_ = nullptr;

  std::atomic<bool> isDiscovering_{false};
  std::atomic<bool> isReceiving_{false};
  std::atomic<bool> hasPendingRequest_{false};
  std::atomic<bool> shuttingDown_{false}; // Graceful shutdown flag

  mutable std::mutex devicesMutex_;
  std::vector<DeviceInfo> devices_;

  mutable std::mutex transfersMutex_;
  std::vector<TransferInfo> transfers_;

  // Thread management for safe shutdown
  mutable std::mutex threadsMutex_;
  std::vector<std::thread> activeThreads_;
  void CleanupFinishedThreads();

  mutable std::mutex requestMutex_;
  IncomingRequest pendingRequest_;
  std::atomic<int> pendingRequestResponse_{
      -1}; // -1=pending, 0=reject, 1=accept
  std::chrono::steady_clock::time_point
      requestStartTime_; // For timeout tracking

  std::string downloadPath_;
  std::string deviceName_;

  // Animation state
  float lastUpdateTime_ = 0.0f;

  // QR Pairing state
  TeleportQrPairingInfo qrInfo_ = {};
  std::vector<uint8_t> qrImageData_;

  // Hotspot state
  std::atomic<bool> hotspotActive_{false};
  TeleportHotspotInfo hotspotInfo_ = {};

  // Web Signaling state
  std::unique_ptr<teleport::WebSignalingClient> webSignaling_;
  std::atomic<bool> webSignalingConnected_{false};
  mutable std::mutex webPeersMutex_;
  std::vector<DeviceInfo> webPeers_;

  // Constants for timeouts and limits
  static constexpr int kMaxTransferThreads = 8;
  static constexpr int kRequestTimeoutMs = 30000;
};

} // namespace teleport::ui
