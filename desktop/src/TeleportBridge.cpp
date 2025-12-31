/**
 * @file TeleportBridge.cpp
 * @brief Implementation of bridge between UI and Teleport C API
 */

#include "TeleportBridge.h"
#include <windows.h>
#include <ShlObj.h>
#include <chrono>
#include <algorithm>

namespace teleport::ui {

// Static callbacks for C API
static void DeviceCallback(const TeleportDevice* device, void* userData) {
    auto* bridge = static_cast<TeleportBridge*>(userData);
    bridge->OnDeviceDiscovered(device);
}

static void DeviceLostCallback(const char* deviceId, void* userData) {
    auto* bridge = static_cast<TeleportBridge*>(userData);
    bridge->OnDeviceLost(deviceId);
}

static void ProgressCallback(const TeleportProgress* progress, void* userData) {
    auto* bridge = static_cast<TeleportBridge*>(userData);
    bridge->OnProgress(progress);
}

static void CompleteCallback(TeleportError error, void* userData) {
    auto* bridge = static_cast<TeleportBridge*>(userData);
    bridge->OnComplete(error);
}

static int IncomingCallback(const TeleportDevice* sender, const TeleportFileInfo* files, 
                           size_t count, void* userData) {
    auto* bridge = static_cast<TeleportBridge*>(userData);
    return bridge->OnIncoming(sender, files, count);
}

TeleportBridge::TeleportBridge() {
    // Set default download path to user's Downloads folder
    wchar_t* downloadPathW = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloadPathW))) {
        char downloadPathA[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, downloadPathW, -1, downloadPathA, MAX_PATH, nullptr, nullptr);
        downloadPath_ = downloadPathA;
        CoTaskMemFree(downloadPathW);
    } else {
        downloadPath_ = ".";
    }
}

TeleportBridge::~TeleportBridge() {
    Shutdown();
}

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

    return true;
}

void TeleportBridge::Shutdown() {
    StopDiscovery();
    StopReceiving();
    
    if (engine_) {
        teleport_destroy(engine_);
        engine_ = nullptr;
    }
}

void TeleportBridge::Update() {
    // Get current time for animations
    auto now = std::chrono::steady_clock::now();
    float currentTime = std::chrono::duration<float>(now.time_since_epoch()).count();
    float deltaTime = currentTime - lastUpdateTime_;
    lastUpdateTime_ = currentTime;
    
    // Cap delta time
    if (deltaTime > 0.1f) deltaTime = 0.1f;
    
    // Animate device cards
    {
        std::lock_guard<std::mutex> lock(devicesMutex_);
        for (auto& device : devices_) {
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
        for (auto& transfer : transfers_) {
            float targetProgress = 0.0f;
            if (transfer.bytesTotal > 0) {
                targetProgress = (float)transfer.bytesTransferred / (float)transfer.bytesTotal;
            }
            // Smooth animation
            transfer.progress += (targetProgress - transfer.progress) * deltaTime * 8.0f;
        }
    }
}

bool TeleportBridge::StartDiscovery() {
    if (!engine_ || isDiscovering_.load()) {
        return false;
    }

    TeleportError err = teleport_start_discovery(
        engine_,
        DeviceCallback,
        DeviceLostCallback,
        this
    );

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
    std::lock_guard<std::mutex> lock(devicesMutex_);
    return devices_;
}

bool TeleportBridge::SendFiles(const std::string& deviceId, 
                               const std::vector<std::string>& filePaths) {
    if (!engine_ || filePaths.empty()) {
        return false;
    }

    // Find device
    TeleportDevice targetDevice = {};
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(devicesMutex_);
        for (const auto& dev : devices_) {
            if (dev.id == deviceId) {
                strncpy_s(targetDevice.id, dev.id.c_str(), TELEPORT_UUID_SIZE - 1);
                strncpy_s(targetDevice.name, dev.name.c_str(), TELEPORT_MAX_DEVICE_NAME - 1);
                strncpy_s(targetDevice.ip, dev.ip.c_str(), sizeof(targetDevice.ip) - 1);
                targetDevice.port = dev.port;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        return false;
    }

    // Prepare file paths array
    std::vector<const char*> paths;
    for (const auto& path : filePaths) {
        paths.push_back(path.c_str());
    }

    TeleportError err = teleport_send_files(
        engine_,
        &targetDevice,
        paths.data(),
        paths.size(),
        ProgressCallback,
        CompleteCallback,
        this,
        &currentTransfer_
    );

    if (err == TELEPORT_OK) {
        // Add to transfers list
        TransferInfo info;
        info.id = deviceId + "_send_" + std::to_string(GetTickCount64());
        info.deviceName = targetDevice.name;
        info.isSending = true;
        info.state = TELEPORT_STATE_CONNECTING;
        info.bytesTotal = 0;
        info.bytesTransferred = 0;
        info.filesTotal = (uint32_t)filePaths.size();
        info.filesCompleted = 0;
        
        std::lock_guard<std::mutex> lock(transfersMutex_);
        transfers_.push_back(info);
        
        return true;
    }

    return false;
}

bool TeleportBridge::StartReceiving(const std::string& outputDir) {
    if (!engine_ || isReceiving_.load()) {
        return false;
    }

    downloadPath_ = outputDir;

    TeleportError err = teleport_start_receiving(
        engine_,
        outputDir.c_str(),
        IncomingCallback,
        ProgressCallback,
        CompleteCallback,
        this
    );

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

void TeleportBridge::PauseTransfer(const std::string& transferId) {
    if (currentTransfer_) {
        teleport_transfer_pause(currentTransfer_);
    }
}

void TeleportBridge::ResumeTransfer(const std::string& transferId) {
    if (currentTransfer_) {
        teleport_transfer_resume(currentTransfer_);
    }
}

void TeleportBridge::CancelTransfer(const std::string& transferId) {
    if (currentTransfer_) {
        teleport_transfer_cancel(currentTransfer_);
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

void TeleportBridge::OnDeviceDiscovered(const TeleportDevice* device) {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    
    // Check if device already exists
    for (auto& existing : devices_) {
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

void TeleportBridge::OnDeviceLost(const char* deviceId) {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    
    devices_.erase(
        std::remove_if(devices_.begin(), devices_.end(),
            [deviceId](const DeviceInfo& d) { return d.id == deviceId; }),
        devices_.end()
    );
}

void TeleportBridge::OnProgress(const TeleportProgress* progress) {
    std::lock_guard<std::mutex> lock(transfersMutex_);
    
    if (!transfers_.empty()) {
        auto& transfer = transfers_.back();
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
        auto& transfer = transfers_.back();
        transfer.state = (error == TELEPORT_OK) 
            ? TELEPORT_STATE_COMPLETE 
            : TELEPORT_STATE_FAILED;
        transfer.progress = (error == TELEPORT_OK) ? 1.0f : transfer.progress;
    }
    
    currentTransfer_ = nullptr;
}

int TeleportBridge::OnIncoming(const TeleportDevice* sender, 
                               const TeleportFileInfo* files, size_t count) {
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
    
    // Wait for user response (with timeout)
    auto start = std::chrono::steady_clock::now();
    while (pendingRequestResponse_.load() == -1) {
        Sleep(100);
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::seconds(30)) {
            return 0; // Timeout - reject
        }
    }
    
    int response = pendingRequestResponse_.load();
    
    if (response == 1) {
        // Add to transfers list
        TransferInfo info;
        info.id = sender->id + std::string("_recv_") + std::to_string(GetTickCount64());
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

} // namespace teleport::ui
