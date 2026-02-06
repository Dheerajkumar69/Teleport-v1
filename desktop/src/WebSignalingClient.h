/**
 * @file WebSignalingClient.h
 * @brief WebSocket client for signaling server connection
 * 
 * Enables desktop app to communicate with web version via shared signaling server.
 * Uses server relay mode for file transfer (no WebRTC needed).
 */

#pragma once

#include <string>
#include <functional>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>

namespace teleport {

// Peer info from signaling server
struct WebPeer {
    std::string id;
    std::string name;
    bool isWeb = true;
};

// File info for transfer
struct FileInfo {
    std::string name;
    size_t size;
    std::string mimeType;
    std::string relativePath;
};

// Incoming relay transfer state
struct RelayTransfer {
    std::string transferId;
    std::string fromPeerId;
    std::string filename;
    size_t totalSize;
    size_t receivedBytes;
    std::vector<uint8_t> data;
    int fileIndex;
    int totalFiles;
};

// Callbacks for signaling events
using OnConnectedCallback = std::function<void()>;
using OnDisconnectedCallback = std::function<void(const std::string& reason)>;
using OnPeersUpdatedCallback = std::function<void(const std::vector<WebPeer>& peers)>;
using OnFileRequestCallback = std::function<void(const std::string& fromId, const std::string& fromName, const std::vector<FileInfo>& files)>;
using OnRelayDataCallback = std::function<void(const std::string& transferId, const std::vector<uint8_t>& data, size_t offset, size_t total)>;
using OnTransferCompleteCallback = std::function<void(const std::string& transferId, const std::string& filename, const std::vector<uint8_t>& data)>;

/**
 * WebSocket signaling client for connecting to web version
 */
class WebSignalingClient {
public:
    WebSignalingClient();
    ~WebSignalingClient();

    // Connection management
    bool connect(const std::string& serverUrl, const std::string& deviceName);
    void disconnect();
    bool isConnected() const { return m_connected; }
    
    // Get our peer ID
    std::string getPeerId() const { return m_peerId; }
    
    // Get connected peers
    std::vector<WebPeer> getPeers() const;
    
    // File transfer via relay
    bool requestFileSend(const std::string& targetPeerId, const std::vector<FileInfo>& files);
    void acceptFileRequest(const std::string& fromPeerId);
    void rejectFileRequest(const std::string& fromPeerId);
    
    // Send file via relay (call after request accepted)
    bool sendFileViaRelay(const std::string& targetPeerId, 
                          const std::string& filename,
                          const std::vector<uint8_t>& data,
                          const std::string& mimeType = "application/octet-stream");

    // Callbacks
    void setOnConnected(OnConnectedCallback cb) { m_onConnected = std::move(cb); }
    void setOnDisconnected(OnDisconnectedCallback cb) { m_onDisconnected = std::move(cb); }
    void setOnPeersUpdated(OnPeersUpdatedCallback cb) { m_onPeersUpdated = std::move(cb); }
    void setOnFileRequest(OnFileRequestCallback cb) { m_onFileRequest = std::move(cb); }
    void setOnRelayData(OnRelayDataCallback cb) { m_onRelayData = std::move(cb); }
    void setOnTransferComplete(OnTransferCompleteCallback cb) { m_onTransferComplete = std::move(cb); }

private:
    void processMessages();
    void handleMessage(const std::string& message);
    void sendMessage(const std::string& message);
    
    // Connection state
    std::atomic<bool> m_connected{false};
    std::string m_serverUrl;
    std::string m_deviceName;
    std::string m_peerId;
    std::string m_room = "teleport-default";
    
    // Thread management
    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_running{false};
    
    // Peers
    mutable std::mutex m_peersMutex;
    std::vector<WebPeer> m_peers;
    
    // Incoming transfers
    std::mutex m_transfersMutex;
    std::map<std::string, RelayTransfer> m_incomingTransfers;
    
    // Message queue
    std::mutex m_sendMutex;
    std::queue<std::string> m_sendQueue;
    
    // Callbacks
    OnConnectedCallback m_onConnected;
    OnDisconnectedCallback m_onDisconnected;
    OnPeersUpdatedCallback m_onPeersUpdated;
    OnFileRequestCallback m_onFileRequest;
    OnRelayDataCallback m_onRelayData;
    OnTransferCompleteCallback m_onTransferComplete;
    
    // Platform-specific WebSocket handle (implemented per platform)
    void* m_wsHandle = nullptr;
};

} // namespace teleport
