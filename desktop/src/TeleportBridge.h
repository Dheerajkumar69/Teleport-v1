/**
 * @file TeleportBridge.h
 * @brief Bridge between UI and Teleport C core API
 */

#pragma once

#include <teleport/teleport.h>
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>

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
    bool isNew = false;    // For animation
    float fadeIn = 0.0f;   // Animation progress
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

    // ============ Sending ============

    /**
     * @brief Send files to a device
     */
    bool SendFiles(const std::string& deviceId, const std::vector<std::string>& filePaths);

    // ============ Receiving ============

    /**
     * @brief Start receiving mode
     */
    bool StartReceiving(const std::string& outputDir);

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
    void SetDownloadPath(const std::string& path) { downloadPath_ = path; }

    // ============ Transfers ============

    /**
     * @brief Get active transfers
     */
    std::vector<TransferInfo> GetTransfers() const;

    /**
     * @brief Pause a transfer
     */
    void PauseTransfer(const std::string& transferId);

    /**
     * @brief Resume a transfer
     */
    void ResumeTransfer(const std::string& transferId);

    /**
     * @brief Cancel a transfer
     */
    void CancelTransfer(const std::string& transferId);

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

    void OnDeviceDiscovered(const TeleportDevice* device);
    void OnDeviceLost(const char* deviceId);
    void OnProgress(const TeleportProgress* progress);
    void OnComplete(TeleportError error);
    int OnIncoming(const TeleportDevice* sender, const TeleportFileInfo* files, size_t count);

private:
    TeleportEngine* engine_ = nullptr;
    TeleportTransfer* currentTransfer_ = nullptr;
    
    std::atomic<bool> isDiscovering_{false};
    std::atomic<bool> isReceiving_{false};
    std::atomic<bool> hasPendingRequest_{false};
    
    mutable std::mutex devicesMutex_;
    std::vector<DeviceInfo> devices_;
    
    mutable std::mutex transfersMutex_;
    std::vector<TransferInfo> transfers_;
    
    mutable std::mutex requestMutex_;
    IncomingRequest pendingRequest_;
    std::atomic<int> pendingRequestResponse_{-1}; // -1=pending, 0=reject, 1=accept
    
    std::string downloadPath_;
    
    // Animation state
    float lastUpdateTime_ = 0.0f;
};

} // namespace teleport::ui
