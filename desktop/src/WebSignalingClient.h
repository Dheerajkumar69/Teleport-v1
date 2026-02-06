/**
 * @file WebSignalingClient.h
 * @brief Bulletproof WebSocket signaling client
 *
 * Features:
 * - Auto-reconnect with exponential backoff
 * - Socket timeouts (no blocking forever)
 * - Buffer size limits (DoS protection)
 * - Thread-safe operations with mutex protection
 * - Chunked file streaming (no full RAM load)
 * - SHA-256 integrity verification
 * - Transfer resume capability
 * - Token-based authentication
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace teleport {

// ============ Configuration Constants ============
struct SignalingConfig {
  // Connection settings
  static constexpr int CONNECT_TIMEOUT_MS = 10000;
  static constexpr int READ_TIMEOUT_MS = 30000;
  static constexpr int WRITE_TIMEOUT_MS = 10000;
  static constexpr int HEARTBEAT_INTERVAL_MS = 15000;

  // Reconnection settings
  static constexpr int RECONNECT_INITIAL_DELAY_MS = 1000;
  static constexpr int RECONNECT_MAX_DELAY_MS = 60000;
  static constexpr float RECONNECT_BACKOFF_MULTIPLIER = 2.0f;
  static constexpr int RECONNECT_MAX_ATTEMPTS = 10;

  // Buffer limits (DoS protection)
  static constexpr size_t MAX_MESSAGE_SIZE = 16 * 1024 * 1024;        // 16MB
  static constexpr size_t MAX_RECEIVE_BUFFER_SIZE = 32 * 1024 * 1024; // 32MB
  static constexpr size_t MAX_SEND_QUEUE_SIZE = 100;

  // File transfer settings
  static constexpr size_t CHUNK_SIZE = 64 * 1024; // 64KB chunks
  static constexpr size_t MAX_FILE_SIZE = 2ULL * 1024 * 1024 * 1024; // 2GB
  static constexpr int TRANSFER_TIMEOUT_MS = 300000; // 5 minutes
};

// ============ Connection State ============
enum class ConnectionState {
  Disconnected,
  Connecting,
  Connected,
  Reconnecting,
  Failed
};

// ============ Transfer State ============
enum class TransferState {
  Pending,
  InProgress,
  Paused,
  Completed,
  Failed,
  Cancelled
};

// ============ Peer Info ============
struct WebPeer {
  std::string id;
  std::string name;
  std::string platform; // "web", "desktop", "mobile"
  bool isWeb = true;
  std::chrono::steady_clock::time_point lastSeen;
};

// ============ File Info ============
struct FileInfo {
  std::string name;
  size_t size = 0;
  std::string mimeType;
  std::string relativePath;
  std::string sha256; // Integrity verification
};

// ============ Transfer Progress ============
struct TransferProgress {
  std::string transferId;
  std::string filename;
  size_t totalBytes = 0;
  size_t transferredBytes = 0;
  TransferState state = TransferState::Pending;
  std::string errorMessage;
  std::string sha256Expected;
  std::string sha256Actual;
  int fileIndex = 0;
  int totalFiles = 0;
  float speedBytesPerSecond = 0.0f;
  std::chrono::steady_clock::time_point startTime;
  std::chrono::steady_clock::time_point lastChunkTime;

  // Resume support
  size_t resumeOffset = 0;
  bool resumable = true;
};

// ============ Relay Transfer (Incoming) ============
struct RelayTransfer {
  std::string transferId;
  std::string fromPeerId;
  std::string filename;
  size_t totalSize = 0;
  size_t receivedBytes = 0;
  std::vector<uint8_t> data;
  int fileIndex = 0;
  int totalFiles = 0;
  std::string sha256Expected;
  TransferState state = TransferState::Pending;
  std::chrono::steady_clock::time_point lastActivity;
};

// ============ Error Codes ============
enum class SignalingError {
  None,
  ConnectionFailed,
  ConnectionTimeout,
  Disconnected,
  InvalidMessage,
  MessageTooLarge,
  BufferOverflow,
  SendFailed,
  TransferFailed,
  IntegrityCheckFailed,
  AuthenticationFailed,
  InvalidState
};

// ============ Callbacks ============
using OnConnectedCallback = std::function<void()>;
using OnDisconnectedCallback = std::function<void(const std::string &reason)>;
using OnReconnectingCallback =
    std::function<void(int attempt, int maxAttempts)>;
using OnPeersUpdatedCallback =
    std::function<void(const std::vector<WebPeer> &peers)>;
using OnFileRequestCallback =
    std::function<void(const std::string &fromId, const std::string &fromName,
                       const std::vector<FileInfo> &files)>;
using OnTransferProgressCallback =
    std::function<void(const TransferProgress &progress)>;
using OnTransferCompleteCallback = std::function<void(
    const std::string &transferId, const std::string &filename,
    const std::vector<uint8_t> &data, bool verified)>;
using OnErrorCallback =
    std::function<void(SignalingError error, const std::string &message)>;

/**
 * Bulletproof WebSocket signaling client
 */
class WebSignalingClient {
public:
  WebSignalingClient();
  ~WebSignalingClient();

  // Non-copyable
  WebSignalingClient(const WebSignalingClient &) = delete;
  WebSignalingClient &operator=(const WebSignalingClient &) = delete;

  // ============ Connection Management ============
  bool connect(const std::string &serverUrl, const std::string &deviceName,
               const std::string &authToken = "");
  void disconnect();

  ConnectionState getConnectionState() const;
  bool isConnected() const {
    return getConnectionState() == ConnectionState::Connected;
  }

  // Enable/disable auto-reconnect
  void setAutoReconnect(bool enabled) { m_autoReconnect = enabled; }
  bool isAutoReconnectEnabled() const { return m_autoReconnect; }

  // ============ Peer Info ============
  std::string getPeerId() const;
  std::vector<WebPeer> getPeers() const;

  // ============ File Transfer ============

  // Request to send files (waits for acceptance)
  bool requestFileSend(const std::string &targetPeerId,
                       const std::vector<FileInfo> &files);

  // Accept/reject incoming requests
  void acceptFileRequest(const std::string &fromPeerId);
  void rejectFileRequest(const std::string &fromPeerId);

  // Send file via relay (chunked streaming)
  bool
  sendFileViaRelay(const std::string &targetPeerId, const std::string &filename,
                   const std::vector<uint8_t> &data,
                   const std::string &mimeType = "application/octet-stream");

  // Stream file from disk (memory efficient)
  bool streamFileViaRelay(const std::string &targetPeerId,
                          const std::string &filePath);

  // Cancel ongoing transfer
  void cancelTransfer(const std::string &transferId);

  // Resume interrupted transfer
  bool resumeTransfer(const std::string &transferId, size_t fromOffset);

  // Get transfer progress
  TransferProgress getTransferProgress(const std::string &transferId) const;
  std::vector<TransferProgress> getAllTransfers() const;

  // ============ Callbacks ============
  void setOnConnected(OnConnectedCallback cb);
  void setOnDisconnected(OnDisconnectedCallback cb);
  void setOnReconnecting(OnReconnectingCallback cb);
  void setOnPeersUpdated(OnPeersUpdatedCallback cb);
  void setOnFileRequest(OnFileRequestCallback cb);
  void setOnTransferProgress(OnTransferProgressCallback cb);
  void setOnTransferComplete(OnTransferCompleteCallback cb);
  void setOnError(OnErrorCallback cb);

private:
  // ============ Internal Methods ============
  void reconnectLoop();
  void processMessages();
  void heartbeatLoop();
  void cleanupStaleTransfers();

  bool connectInternal();
  void handleMessage(const std::string &message);
  bool sendMessage(const std::string &message);
  bool sendMessageWithRetry(const std::string &message, int maxRetries = 3);

  // Message validation
  bool validateMessage(const std::string &message);
  bool validateJsonSchema(const std::string &json, const std::string &type);

  // Socket operations with timeouts
  bool setSocketTimeout(int socket, int timeoutMs, bool isRead);
  int recvWithTimeout(int socket, void *buffer, size_t size, int timeoutMs);
  int sendWithTimeout(int socket, const void *buffer, size_t size,
                      int timeoutMs);

  // Integrity verification
  static std::string computeSHA256(const std::vector<uint8_t> &data);
  static std::string computeSHA256(const std::string &filePath,
                                   size_t maxBytes = 0);

  // ============ State ============
  std::atomic<ConnectionState> m_state{ConnectionState::Disconnected};
  std::string m_serverUrl;
  std::string m_deviceName;
  std::string m_authToken;
  std::string m_peerId;
  std::string m_room = "teleport-default";

  // ============ Connection ============
  std::atomic<bool> m_autoReconnect{true};
  std::atomic<int> m_reconnectAttempt{0};
  std::atomic<int> m_reconnectDelayMs{
      SignalingConfig::RECONNECT_INITIAL_DELAY_MS};

  // ============ Threads ============
  std::unique_ptr<std::thread> m_messageThread;
  std::unique_ptr<std::thread> m_heartbeatThread;
  std::unique_ptr<std::thread> m_reconnectThread;
  std::atomic<bool> m_running{false};
  std::atomic<bool> m_stopRequested{false};

  // ============ Thread Safety ============
  mutable std::mutex m_stateMutex;
  mutable std::mutex m_peersMutex;
  mutable std::mutex m_transfersMutex;
  mutable std::mutex m_sendMutex;
  mutable std::mutex m_callbackMutex;

  // ============ Data ============
  std::vector<WebPeer> m_peers;
  std::map<std::string, RelayTransfer> m_incomingTransfers;
  std::map<std::string, TransferProgress> m_outgoingTransfers;

  // ============ Buffers ============
  std::vector<uint8_t> m_receiveBuffer;
  std::queue<std::string> m_sendQueue;

  // ============ Callbacks ============
  OnConnectedCallback m_onConnected;
  OnDisconnectedCallback m_onDisconnected;
  OnReconnectingCallback m_onReconnecting;
  OnPeersUpdatedCallback m_onPeersUpdated;
  OnFileRequestCallback m_onFileRequest;
  OnTransferProgressCallback m_onTransferProgress;
  OnTransferCompleteCallback m_onTransferComplete;
  OnErrorCallback m_onError;

  // ============ Timing ============
  std::chrono::steady_clock::time_point m_lastHeartbeat;
  std::chrono::steady_clock::time_point m_lastActivity;

  // ============ Socket Handle ============
  int m_socket = -1;
  bool m_useTLS = false;
  void *m_sslContext = nullptr; // OpenSSL SSL_CTX*
  void *m_ssl = nullptr;        // OpenSSL SSL*
};

} // namespace teleport
