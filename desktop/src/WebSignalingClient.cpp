/**
 * @file WebSignalingClient.cpp
 * @brief Bulletproof WebSocket signaling client implementation
 */

#include "WebSignalingClient.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define CLOSE_SOCKET closesocket
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSE_SOCKET close
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

// OpenSSL for SHA-256 (optional - can use simple implementation if not
// available)
#ifdef USE_OPENSSL
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#endif

namespace teleport {

// ============ SHA-256 Implementation (no OpenSSL dependency) ============
namespace {

class SHA256 {
public:
  SHA256() { reset(); }

  void update(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
      m_data[m_blocklen++] = data[i];
      if (m_blocklen == 64) {
        transform();
        m_bitlen += 512;
        m_blocklen = 0;
      }
    }
  }

  std::string finalize() {
    uint8_t hash[32];
    pad();
    revert(hash);

    std::stringstream ss;
    for (int i = 0; i < 32; i++) {
      ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
  }

  void reset() {
    m_blocklen = 0;
    m_bitlen = 0;
    m_state[0] = 0x6a09e667;
    m_state[1] = 0xbb67ae85;
    m_state[2] = 0x3c6ef372;
    m_state[3] = 0xa54ff53a;
    m_state[4] = 0x510e527f;
    m_state[5] = 0x9b05688c;
    m_state[6] = 0x1f83d9ab;
    m_state[7] = 0x5be0cd19;
  }

private:
  uint8_t m_data[64];
  uint32_t m_blocklen;
  uint64_t m_bitlen;
  uint32_t m_state[8];

  static constexpr uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

  static uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
  }
  static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
  }
  static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
  }
  static uint32_t sig0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
  }
  static uint32_t sig1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
  }
  static uint32_t ep0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
  }
  static uint32_t ep1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
  }

  void transform() {
    uint32_t m[64], a, b, c, d, e, f, g, h, t1, t2;
    for (int i = 0, j = 0; i < 16; i++, j += 4)
      m[i] = (m_data[j] << 24) | (m_data[j + 1] << 16) | (m_data[j + 2] << 8) |
             m_data[j + 3];
    for (int i = 16; i < 64; i++)
      m[i] = ep1(m[i - 2]) + m[i - 7] + ep0(m[i - 15]) + m[i - 16];
    a = m_state[0];
    b = m_state[1];
    c = m_state[2];
    d = m_state[3];
    e = m_state[4];
    f = m_state[5];
    g = m_state[6];
    h = m_state[7];
    for (int i = 0; i < 64; i++) {
      t1 = h + sig1(e) + ch(e, f, g) + K[i] + m[i];
      t2 = sig0(a) + maj(a, b, c);
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
  }

  void pad() {
    uint64_t i = m_blocklen;
    uint8_t end = m_blocklen < 56 ? 56 : 64;
    m_data[i++] = 0x80;
    while (i < end)
      m_data[i++] = 0x00;
    if (m_blocklen >= 56) {
      transform();
      memset(m_data, 0, 56);
    }
    m_bitlen += m_blocklen * 8;
    m_data[63] = m_bitlen;
    m_data[62] = m_bitlen >> 8;
    m_data[61] = m_bitlen >> 16;
    m_data[60] = m_bitlen >> 24;
    m_data[59] = m_bitlen >> 32;
    m_data[58] = m_bitlen >> 40;
    m_data[57] = m_bitlen >> 48;
    m_data[56] = m_bitlen >> 56;
    transform();
  }

  void revert(uint8_t *hash) {
    for (int i = 0; i < 4; i++) {
      hash[i] = (m_state[0] >> (24 - i * 8)) & 0xff;
      hash[i + 4] = (m_state[1] >> (24 - i * 8)) & 0xff;
      hash[i + 8] = (m_state[2] >> (24 - i * 8)) & 0xff;
      hash[i + 12] = (m_state[3] >> (24 - i * 8)) & 0xff;
      hash[i + 16] = (m_state[4] >> (24 - i * 8)) & 0xff;
      hash[i + 20] = (m_state[5] >> (24 - i * 8)) & 0xff;
      hash[i + 24] = (m_state[6] >> (24 - i * 8)) & 0xff;
      hash[i + 28] = (m_state[7] >> (24 - i * 8)) & 0xff;
    }
  }
};

constexpr uint32_t SHA256::K[64];

// Base64 encoding
static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = (data[i] << 16);
    if (i + 1 < len)
      n |= (data[i + 1] << 8);
    if (i + 2 < len)
      n |= data[i + 2];
    out += B64_CHARS[(n >> 18) & 0x3F];
    out += B64_CHARS[(n >> 12) & 0x3F];
    out += (i + 1 < len) ? B64_CHARS[(n >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? B64_CHARS[n & 0x3F] : '=';
  }
  return out;
}

std::vector<uint8_t> base64Decode(const std::string &encoded) {
  static const int B64_INDEX[256] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
      52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
      -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
      15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
      -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
      41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};
  std::vector<uint8_t> out;
  out.reserve(encoded.size() * 3 / 4);
  int val = 0, bits = -8;
  for (char c : encoded) {
    if (c == '=')
      break;
    int idx = B64_INDEX[(unsigned char)c];
    if (idx < 0)
      continue;
    val = (val << 6) | idx;
    bits += 6;
    if (bits >= 0) {
      out.push_back((val >> bits) & 0xFF);
      bits -= 8;
    }
  }
  return out;
}

std::string generateUUID() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, 15);
  const char *hex = "0123456789abcdef";
  std::string uuid(36, '-');
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23)
      continue;
    uuid[i] = hex[dis(gen)];
  }
  uuid[14] = '4';
  uuid[19] = hex[(dis(gen) & 0x3) | 0x8];
  return uuid;
}

} // anonymous namespace

// ============ SHA-256 Static Methods ============
std::string
WebSignalingClient::computeSHA256(const std::vector<uint8_t> &data) {
  SHA256 sha;
  sha.update(data.data(), data.size());
  return sha.finalize();
}

std::string WebSignalingClient::computeSHA256(const std::string &filePath,
                                              size_t maxBytes) {
  std::ifstream file(filePath, std::ios::binary);
  if (!file)
    return "";

  SHA256 sha;
  char buffer[8192];
  size_t totalRead = 0;
  while (file && (maxBytes == 0 || totalRead < maxBytes)) {
    size_t toRead = sizeof(buffer);
    if (maxBytes > 0 && totalRead + toRead > maxBytes) {
      toRead = maxBytes - totalRead;
    }
    file.read(buffer, toRead);
    size_t read = file.gcount();
    if (read > 0) {
      sha.update(reinterpret_cast<uint8_t *>(buffer), read);
      totalRead += read;
    }
  }
  return sha.finalize();
}

// ============ Constructor / Destructor ============
WebSignalingClient::WebSignalingClient() {
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
  m_receiveBuffer.reserve(SignalingConfig::CHUNK_SIZE * 2);
}

WebSignalingClient::~WebSignalingClient() {
  disconnect();
#ifdef _WIN32
  WSACleanup();
#endif
}

// ============ Connection State ============
ConnectionState WebSignalingClient::getConnectionState() const {
  return m_state.load();
}

std::string WebSignalingClient::getPeerId() const {
  std::lock_guard<std::mutex> lock(m_stateMutex);
  return m_peerId;
}

std::vector<WebPeer> WebSignalingClient::getPeers() const {
  std::lock_guard<std::mutex> lock(m_peersMutex);
  return m_peers;
}

// ============ Socket Timeout Helpers ============
bool WebSignalingClient::setSocketTimeout(int socket, int timeoutMs,
                                          bool isRead) {
#ifdef _WIN32
  DWORD timeout = timeoutMs;
  int option = isRead ? SO_RCVTIMEO : SO_SNDTIMEO;
  return setsockopt(socket, SOL_SOCKET, option, (char *)&timeout,
                    sizeof(timeout)) == 0;
#else
  struct timeval tv;
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  int option = isRead ? SO_RCVTIMEO : SO_SNDTIMEO;
  return setsockopt(socket, SOL_SOCKET, option, &tv, sizeof(tv)) == 0;
#endif
}

int WebSignalingClient::recvWithTimeout(int socket, void *buffer, size_t size,
                                        int timeoutMs) {
#ifdef _WIN32
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(socket, &readSet);
  struct timeval tv = {timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  int result = select(socket + 1, &readSet, nullptr, nullptr, &tv);
  if (result <= 0)
    return result;
#else
  struct pollfd pfd = {socket, POLLIN, 0};
  int result = poll(&pfd, 1, timeoutMs);
  if (result <= 0)
    return result;
#endif

#ifdef USE_OPENSSL
  if (m_ssl) {
    return SSL_read(static_cast<SSL *>(m_ssl), buffer, static_cast<int>(size));
  }
#endif

#ifdef _WIN32
  return recv(socket, (char *)buffer, (int)size, 0);
#else
  return recv(socket, buffer, size, 0);
#endif
}

int WebSignalingClient::sendWithTimeout(int socket, const void *buffer,
                                        size_t size, int timeoutMs) {
#ifdef _WIN32
  fd_set writeSet;
  FD_ZERO(&writeSet);
  FD_SET(socket, &writeSet);
  struct timeval tv = {timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  int result = select(socket + 1, nullptr, &writeSet, nullptr, &tv);
  if (result <= 0)
    return result;
#else
  struct pollfd pfd = {socket, POLLOUT, 0};
  int result = poll(&pfd, 1, timeoutMs);
  if (result <= 0)
    return result;
#endif

#ifdef USE_OPENSSL
  if (m_ssl) {
    return SSL_write(static_cast<SSL *>(m_ssl), buffer, static_cast<int>(size));
  }
#endif

#ifdef _WIN32
  return send(socket, (const char *)buffer, (int)size, 0);
#else
  return send(socket, buffer, size, 0);
#endif
}

// ============ Message Validation ============
bool WebSignalingClient::validateMessage(const std::string &message) {
  // Check size limit
  if (message.size() > SignalingConfig::MAX_MESSAGE_SIZE) {
    if (m_onError) {
      m_onError(SignalingError::MessageTooLarge,
                "Message exceeds maximum size");
    }
    return false;
  }

  // Basic JSON validation - must start with { or [
  size_t start = message.find_first_not_of(" \t\n\r");
  if (start == std::string::npos ||
      (message[start] != '{' && message[start] != '[')) {
    return false;
  }

  return true;
}

bool WebSignalingClient::validateJsonSchema(const std::string &json,
                                            const std::string &expectedType) {
  // Check for required "type" field
  size_t typePos = json.find("\"type\"");
  if (typePos == std::string::npos)
    return false;

  // Verify type matches expected
  if (!expectedType.empty() &&
      json.find("\"" + expectedType + "\"") == std::string::npos) {
    return false;
  }

  return true;
}

// ============ Connect ============
bool WebSignalingClient::connect(const std::string &serverUrl,
                                 const std::string &deviceName,
                                 const std::string &authToken) {
  std::lock_guard<std::mutex> lock(m_stateMutex);

  if (m_state == ConnectionState::Connected ||
      m_state == ConnectionState::Connecting) {
    return m_state == ConnectionState::Connected;
  }

  m_serverUrl = serverUrl;
  m_deviceName = deviceName;
  m_authToken = authToken;
  m_useTLS = serverUrl.find("wss://") == 0;
  m_reconnectAttempt = 0;
  m_reconnectDelayMs = SignalingConfig::RECONNECT_INITIAL_DELAY_MS;

  return connectInternal();
}

bool WebSignalingClient::connectInternal() {
  m_state = ConnectionState::Connecting;

  // Parse URL
  std::string url = m_serverUrl;
  bool useTLS = url.find("wss://") == 0;
  if (url.find("ws://") == 0)
    url = url.substr(5);
  else if (url.find("wss://") == 0)
    url = url.substr(6);

  std::string host, path = "/";
  int port = useTLS ? 443 : 80;

  size_t pathStart = url.find('/');
  if (pathStart != std::string::npos) {
    path = url.substr(pathStart);
    url = url.substr(0, pathStart);
  }

  size_t portStart = url.find(':');
  if (portStart != std::string::npos) {
    port = std::stoi(url.substr(portStart + 1));
    host = url.substr(0, portStart);
  } else {
    host = url;
  }

  // Resolve host
  struct addrinfo hints = {}, *result = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                  &result) != 0) {
    m_state = ConnectionState::Failed;
    if (m_onError)
      m_onError(SignalingError::ConnectionFailed, "DNS resolution failed");
    return false;
  }

  // Create socket
  m_socket =
      socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (m_socket == INVALID_SOCKET) {
    freeaddrinfo(result);
    m_state = ConnectionState::Failed;
    if (m_onError)
      m_onError(SignalingError::ConnectionFailed, "Socket creation failed");
    return false;
  }

  // Set timeouts BEFORE connecting
  setSocketTimeout(m_socket, SignalingConfig::CONNECT_TIMEOUT_MS, true);
  setSocketTimeout(m_socket, SignalingConfig::WRITE_TIMEOUT_MS, false);

  // Connect
  if (::connect(m_socket, result->ai_addr, result->ai_addrlen) != 0) {
    freeaddrinfo(result);
    CLOSE_SOCKET(m_socket);
    m_socket = -1;
    m_state = ConnectionState::Failed;
    if (m_onError)
      m_onError(SignalingError::ConnectionFailed, "Connection failed");
    return false;
  }
  freeaddrinfo(result);

#ifdef USE_OPENSSL
  // Initialize TLS for wss:// connections
  if (useTLS) {
    // Initialize OpenSSL (only once)
    static bool sslInitialized = false;
    if (!sslInitialized) {
      SSL_library_init();
      SSL_load_error_strings();
      OpenSSL_add_all_algorithms();
      sslInitialized = true;
    }

    // Create SSL context
    const SSL_METHOD *method = TLS_client_method();
    m_sslContext = SSL_CTX_new(method);
    if (!m_sslContext) {
      CLOSE_SOCKET(m_socket);
      m_socket = -1;
      m_state = ConnectionState::Failed;
      if (m_onError)
        m_onError(SignalingError::ConnectionFailed,
                  "SSL context creation failed");
      return false;
    }

    // Create SSL connection
    m_ssl = SSL_new(static_cast<SSL_CTX *>(m_sslContext));
    if (!m_ssl) {
      SSL_CTX_free(static_cast<SSL_CTX *>(m_sslContext));
      m_sslContext = nullptr;
      CLOSE_SOCKET(m_socket);
      m_socket = -1;
      m_state = ConnectionState::Failed;
      if (m_onError)
        m_onError(SignalingError::ConnectionFailed, "SSL creation failed");
      return false;
    }

    // Set hostname for SNI (Server Name Indication)
    SSL_set_tlsext_host_name(static_cast<SSL *>(m_ssl), host.c_str());
    SSL_set_fd(static_cast<SSL *>(m_ssl), m_socket);

    // Perform TLS handshake
    if (SSL_connect(static_cast<SSL *>(m_ssl)) <= 0) {
      SSL_free(static_cast<SSL *>(m_ssl));
      SSL_CTX_free(static_cast<SSL_CTX *>(m_sslContext));
      m_ssl = nullptr;
      m_sslContext = nullptr;
      CLOSE_SOCKET(m_socket);
      m_socket = -1;
      m_state = ConnectionState::Failed;
      if (m_onError)
        m_onError(SignalingError::ConnectionFailed, "TLS handshake failed");
      return false;
    }
    m_useTLS = true;
  }
#endif

  // Generate WebSocket key
  uint8_t keyBytes[16];
  std::random_device rd;
  for (int i = 0; i < 16; i++)
    keyBytes[i] = rd() & 0xFF;
  std::string wsKey = base64Encode(keyBytes, 16);

  // Build HTTP upgrade request
  std::ostringstream req;
  req << "GET " << path << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << "Upgrade: websocket\r\n"
      << "Connection: Upgrade\r\n"
      << "Sec-WebSocket-Key: " << wsKey << "\r\n"
      << "Sec-WebSocket-Version: 13\r\n";
  if (!m_authToken.empty()) {
    req << "Authorization: Bearer " << m_authToken << "\r\n";
  }
  req << "\r\n";

  std::string request = req.str();
  if (sendWithTimeout(m_socket, request.c_str(), request.size(),
                      SignalingConfig::WRITE_TIMEOUT_MS) <= 0) {
    CLOSE_SOCKET(m_socket);
    m_socket = -1;
    m_state = ConnectionState::Failed;
    return false;
  }

  // Read HTTP response with timeout
  char buffer[2048] = {0};
  int received = recvWithTimeout(m_socket, buffer, sizeof(buffer) - 1,
                                 SignalingConfig::CONNECT_TIMEOUT_MS);
  if (received <= 0 || strstr(buffer, "101") == nullptr) {
    CLOSE_SOCKET(m_socket);
    m_socket = -1;
    m_state = ConnectionState::Failed;
    if (m_onError)
      m_onError(SignalingError::ConnectionFailed, "WebSocket handshake failed");
    return false;
  }

  // Generate peer ID
  m_peerId = generateUUID();
  m_state = ConnectionState::Connected;
  m_running = true;
  m_stopRequested = false;
  m_lastHeartbeat = std::chrono::steady_clock::now();
  m_lastActivity = m_lastHeartbeat;

  // Set read timeout for message loop
  setSocketTimeout(m_socket, SignalingConfig::READ_TIMEOUT_MS, true);

  // Start message processing thread
  m_messageThread =
      std::make_unique<std::thread>(&WebSignalingClient::processMessages, this);

  // Start heartbeat thread
  m_heartbeatThread =
      std::make_unique<std::thread>(&WebSignalingClient::heartbeatLoop, this);

  // Send join message
  std::ostringstream joinMsg;
  joinMsg << "{\"type\":\"join\",\"room\":\"" << m_room << "\",\"peerId\":\""
          << m_peerId << "\",\"name\":\"" << m_deviceName
          << "\",\"platform\":\"desktop\"}";
  sendMessage(joinMsg.str());

  // Notify callback
  if (m_onConnected) {
    std::lock_guard<std::mutex> cbLock(m_callbackMutex);
    m_onConnected();
  }

  return true;
}

void WebSignalingClient::disconnect() {
  m_stopRequested = true;
  m_running = false;
  m_autoReconnect = false; // Disable reconnect on explicit disconnect

#ifdef USE_OPENSSL
  // Clean up SSL before closing socket
  if (m_ssl) {
    SSL_shutdown(static_cast<SSL *>(m_ssl));
    SSL_free(static_cast<SSL *>(m_ssl));
    m_ssl = nullptr;
  }
  if (m_sslContext) {
    SSL_CTX_free(static_cast<SSL_CTX *>(m_sslContext));
    m_sslContext = nullptr;
  }
  m_useTLS = false;
#endif

  if (m_socket != -1) {
    // Send close frame
    uint8_t closeFrame[4] = {0x88, 0x02, 0x03, 0xE8}; // Close with code 1000
    sendWithTimeout(m_socket, closeFrame, 4, 1000);
    CLOSE_SOCKET(m_socket);
    m_socket = -1;
  }

  if (m_messageThread && m_messageThread->joinable()) {
    m_messageThread->join();
  }
  if (m_heartbeatThread && m_heartbeatThread->joinable()) {
    m_heartbeatThread->join();
  }
  if (m_reconnectThread && m_reconnectThread->joinable()) {
    m_reconnectThread->join();
  }

  m_state = ConnectionState::Disconnected;

  if (m_onDisconnected) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onDisconnected("Disconnected by user");
  }
}

// ============ Reconnect Loop ============
void WebSignalingClient::reconnectLoop() {
  while (m_autoReconnect && !m_stopRequested &&
         m_reconnectAttempt < SignalingConfig::RECONNECT_MAX_ATTEMPTS) {

    m_state = ConnectionState::Reconnecting;
    m_reconnectAttempt++;

    if (m_onReconnecting) {
      std::lock_guard<std::mutex> lock(m_callbackMutex);
      m_onReconnecting(m_reconnectAttempt,
                       SignalingConfig::RECONNECT_MAX_ATTEMPTS);
    }

    // Wait with exponential backoff
    std::this_thread::sleep_for(
        std::chrono::milliseconds(m_reconnectDelayMs.load()));

    // Increase delay for next attempt
    int newDelay = static_cast<int>(
        m_reconnectDelayMs * SignalingConfig::RECONNECT_BACKOFF_MULTIPLIER);
    m_reconnectDelayMs =
        std::min(newDelay, SignalingConfig::RECONNECT_MAX_DELAY_MS);

    // Try to connect
    if (connectInternal()) {
      m_reconnectAttempt = 0;
      m_reconnectDelayMs = SignalingConfig::RECONNECT_INITIAL_DELAY_MS;
      return;
    }
  }

  // Max attempts reached
  m_state = ConnectionState::Failed;
  if (m_onError) {
    m_onError(SignalingError::ConnectionFailed,
              "Max reconnection attempts reached");
  }
}

// ============ Heartbeat Loop ============
void WebSignalingClient::heartbeatLoop() {
  while (m_running && !m_stopRequested) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(SignalingConfig::HEARTBEAT_INTERVAL_MS));

    if (!m_running || m_stopRequested)
      break;

    // Send ping
    if (m_state == ConnectionState::Connected) {
      // WebSocket ping frame
      uint8_t pingFrame[2] = {0x89, 0x00};
      if (sendWithTimeout(m_socket, pingFrame, 2,
                          SignalingConfig::WRITE_TIMEOUT_MS) <= 0) {
        // Connection lost
        if (m_autoReconnect) {
          m_running = false;
          m_reconnectThread = std::make_unique<std::thread>(
              &WebSignalingClient::reconnectLoop, this);
        }
      }
      m_lastHeartbeat = std::chrono::steady_clock::now();
    }

    // Cleanup stale transfers
    cleanupStaleTransfers();
  }
}

void WebSignalingClient::cleanupStaleTransfers() {
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(m_transfersMutex);

  for (auto it = m_incomingTransfers.begin();
       it != m_incomingTransfers.end();) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - it->second.lastActivity)
                       .count();
    if (elapsed > SignalingConfig::TRANSFER_TIMEOUT_MS) {
      it = m_incomingTransfers.erase(it);
    } else {
      ++it;
    }
  }
}

// ============ Send Message with Retry ============
bool WebSignalingClient::sendMessage(const std::string &message) {
  if (m_socket == -1 || m_state != ConnectionState::Connected)
    return false;

  std::lock_guard<std::mutex> lock(m_sendMutex);

  // Validate message
  if (message.size() > SignalingConfig::MAX_MESSAGE_SIZE) {
    if (m_onError)
      m_onError(SignalingError::MessageTooLarge, "Message too large to send");
    return false;
  }

  // Build WebSocket frame
  std::vector<uint8_t> frame;
  frame.push_back(0x81); // Text frame, FIN

  if (message.size() < 126) {
    frame.push_back(0x80 | message.size()); // Masked
  } else if (message.size() < 65536) {
    frame.push_back(0x80 | 126);
    frame.push_back((message.size() >> 8) & 0xFF);
    frame.push_back(message.size() & 0xFF);
  } else {
    frame.push_back(0x80 | 127);
    for (int i = 7; i >= 0; i--) {
      frame.push_back((message.size() >> (i * 8)) & 0xFF);
    }
  }

  // Add mask
  uint8_t mask[4];
  std::random_device rd;
  for (int i = 0; i < 4; i++)
    mask[i] = rd() & 0xFF;
  frame.insert(frame.end(), mask, mask + 4);

  // Mask and add payload
  for (size_t i = 0; i < message.size(); i++) {
    frame.push_back(message[i] ^ mask[i % 4]);
  }

  // Send with timeout
  int sent = sendWithTimeout(m_socket, frame.data(), frame.size(),
                             SignalingConfig::WRITE_TIMEOUT_MS);
  return sent == static_cast<int>(frame.size());
}

bool WebSignalingClient::sendMessageWithRetry(const std::string &message,
                                              int maxRetries) {
  for (int i = 0; i < maxRetries; i++) {
    if (sendMessage(message))
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(100 * (i + 1)));
  }
  return false;
}

// ============ Callback Setters ============
void WebSignalingClient::setOnConnected(OnConnectedCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onConnected = std::move(cb);
}

void WebSignalingClient::setOnDisconnected(OnDisconnectedCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onDisconnected = std::move(cb);
}

void WebSignalingClient::setOnReconnecting(OnReconnectingCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onReconnecting = std::move(cb);
}

void WebSignalingClient::setOnPeersUpdated(OnPeersUpdatedCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onPeersUpdated = std::move(cb);
}

void WebSignalingClient::setOnFileRequest(OnFileRequestCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onFileRequest = std::move(cb);
}

void WebSignalingClient::setOnTransferProgress(OnTransferProgressCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onTransferProgress = std::move(cb);
}

void WebSignalingClient::setOnTransferComplete(OnTransferCompleteCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onTransferComplete = std::move(cb);
}

void WebSignalingClient::setOnError(OnErrorCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onError = std::move(cb);
}

// ============ Process Messages Loop ============
void WebSignalingClient::processMessages() {
  std::vector<uint8_t> frameBuffer;
  frameBuffer.reserve(SignalingConfig::CHUNK_SIZE);

  while (m_running && !m_stopRequested) {
    uint8_t header[2];
    int received =
        recvWithTimeout(m_socket, header, 2, SignalingConfig::READ_TIMEOUT_MS);

    if (received <= 0) {
      if (received == 0 || !m_running)
        break;  // Connection closed or timeout
      continue; // Timeout, keep trying
    }

    // Parse WebSocket frame
    bool fin = (header[0] & 0x80) != 0;
    int opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    size_t payloadLen = header[1] & 0x7F;

    // Extended payload length
    if (payloadLen == 126) {
      uint8_t ext[2];
      if (recvWithTimeout(m_socket, ext, 2, SignalingConfig::READ_TIMEOUT_MS) !=
          2)
        continue;
      payloadLen = (ext[0] << 8) | ext[1];
    } else if (payloadLen == 127) {
      uint8_t ext[8];
      if (recvWithTimeout(m_socket, ext, 8, SignalingConfig::READ_TIMEOUT_MS) !=
          8)
        continue;
      payloadLen = 0;
      for (int i = 0; i < 8; i++)
        payloadLen = (payloadLen << 8) | ext[i];
    }

    // Buffer size limit check
    if (payloadLen > SignalingConfig::MAX_MESSAGE_SIZE) {
      if (m_onError)
        m_onError(SignalingError::MessageTooLarge,
                  "Received message too large");
      continue;
    }

    // Read mask if present
    uint8_t mask[4] = {0};
    if (masked && recvWithTimeout(m_socket, mask, 4,
                                  SignalingConfig::READ_TIMEOUT_MS) != 4)
      continue;

    // Read payload in chunks
    std::vector<uint8_t> payload(payloadLen);
    size_t totalRead = 0;
    while (totalRead < payloadLen && m_running) {
      size_t toRead =
          std::min(SignalingConfig::CHUNK_SIZE, payloadLen - totalRead);
      int read = recvWithTimeout(m_socket, payload.data() + totalRead, toRead,
                                 SignalingConfig::READ_TIMEOUT_MS);
      if (read <= 0)
        break;
      totalRead += read;
    }

    if (totalRead < payloadLen)
      continue; // Incomplete frame

    // Unmask if needed
    if (masked) {
      for (size_t i = 0; i < payloadLen; i++) {
        payload[i] ^= mask[i % 4];
      }
    }

    m_lastActivity = std::chrono::steady_clock::now();

    // Handle based on opcode
    if (opcode == 0x01) { // Text frame
      std::string message(payload.begin(), payload.end());
      if (validateMessage(message)) {
        handleMessage(message);
      }
    } else if (opcode == 0x08) { // Close frame
      m_running = false;
      if (m_autoReconnect) {
        m_reconnectThread = std::make_unique<std::thread>(
            &WebSignalingClient::reconnectLoop, this);
      }
    } else if (opcode == 0x09) { // Ping
      uint8_t pongFrame[2] = {0x8A, 0x00};
      sendWithTimeout(m_socket, pongFrame, 2, 1000);
    }
  }

  // Disconnected
  if (m_autoReconnect && !m_stopRequested) {
    m_reconnectThread =
        std::make_unique<std::thread>(&WebSignalingClient::reconnectLoop, this);
  }
}

// ============ Handle Message ============
void WebSignalingClient::handleMessage(const std::string &message) {
  // Simple JSON parsing for type field
  size_t typeStart = message.find("\"type\":");
  if (typeStart == std::string::npos)
    return;

  size_t valueStart = message.find('"', typeStart + 7);
  if (valueStart == std::string::npos)
    return;
  size_t valueEnd = message.find('"', valueStart + 1);
  if (valueEnd == std::string::npos)
    return;

  std::string type = message.substr(valueStart + 1, valueEnd - valueStart - 1);

  if (type == "peers") {
    // Parse peers list
    std::vector<WebPeer> peers;
    size_t pos = 0;
    while ((pos = message.find("\"id\":", pos)) != std::string::npos) {
      WebPeer peer;
      size_t idStart = message.find('"', pos + 5);
      size_t idEnd = message.find('"', idStart + 1);
      if (idStart != std::string::npos && idEnd != std::string::npos) {
        peer.id = message.substr(idStart + 1, idEnd - idStart - 1);
      }
      size_t namePos = message.find("\"name\":", pos);
      if (namePos != std::string::npos && namePos < pos + 200) {
        size_t nameStart = message.find('"', namePos + 7);
        size_t nameEnd = message.find('"', nameStart + 1);
        if (nameStart != std::string::npos && nameEnd != std::string::npos) {
          peer.name = message.substr(nameStart + 1, nameEnd - nameStart - 1);
        }
      }
      peer.isWeb = true;
      peer.lastSeen = std::chrono::steady_clock::now();
      if (!peer.id.empty() && peer.id != m_peerId) {
        peers.push_back(peer);
      }
      pos = idEnd + 1;
    }
    {
      std::lock_guard<std::mutex> lock(m_peersMutex);
      m_peers = std::move(peers);
    }
    if (m_onPeersUpdated) {
      std::lock_guard<std::mutex> lock(m_callbackMutex);
      m_onPeersUpdated(m_peers);
    }
  } else if (type == "file-request") {
    // Parse file request
    std::string fromId, fromName;
    std::vector<FileInfo> files;
    // Extract fromId
    size_t fromPos = message.find("\"from\":");
    if (fromPos != std::string::npos) {
      size_t start = message.find('"', fromPos + 7);
      size_t end = message.find('"', start + 1);
      if (start != std::string::npos && end != std::string::npos) {
        fromId = message.substr(start + 1, end - start - 1);
      }
    }
    if (m_onFileRequest) {
      std::lock_guard<std::mutex> lock(m_callbackMutex);
      m_onFileRequest(fromId, fromName, files);
    }
  } else if (type == "relay-start") {
    // Start new incoming transfer
    std::string transferId, filename;
    size_t totalSize = 0;
    // Parse fields...
    size_t tidPos = message.find("\"transferId\":");
    if (tidPos != std::string::npos) {
      size_t start = message.find('"', tidPos + 13);
      size_t end = message.find('"', start + 1);
      if (start != std::string::npos && end != std::string::npos) {
        transferId = message.substr(start + 1, end - start - 1);
      }
    }
    RelayTransfer transfer;
    transfer.transferId = transferId;
    transfer.state = TransferState::InProgress;
    transfer.lastActivity = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    m_incomingTransfers[transferId] = transfer;
  } else if (type == "relay-chunk") {
    // Receive chunk
    std::string transferId;
    size_t tidPos = message.find("\"transferId\":");
    if (tidPos != std::string::npos) {
      size_t start = message.find('"', tidPos + 13);
      size_t end = message.find('"', start + 1);
      if (start != std::string::npos && end != std::string::npos) {
        transferId = message.substr(start + 1, end - start - 1);
      }
    }
    size_t dataPos = message.find("\"data\":");
    if (dataPos != std::string::npos) {
      size_t start = message.find('"', dataPos + 7);
      size_t end = message.find('"', start + 1);
      if (start != std::string::npos && end != std::string::npos) {
        std::string b64 = message.substr(start + 1, end - start - 1);
        std::vector<uint8_t> chunk = base64Decode(b64);
        std::lock_guard<std::mutex> lock(m_transfersMutex);
        auto it = m_incomingTransfers.find(transferId);
        if (it != m_incomingTransfers.end()) {
          // Buffer limit check
          if (it->second.data.size() + chunk.size() >
              SignalingConfig::MAX_RECEIVE_BUFFER_SIZE) {
            if (m_onError)
              m_onError(SignalingError::BufferOverflow,
                        "Transfer buffer overflow");
            m_incomingTransfers.erase(it);
          } else {
            it->second.data.insert(it->second.data.end(), chunk.begin(),
                                   chunk.end());
            it->second.receivedBytes += chunk.size();
            it->second.lastActivity = std::chrono::steady_clock::now();
          }
        }
      }
    }
  } else if (type == "relay-end") {
    std::string transferId;
    size_t tidPos = message.find("\"transferId\":");
    if (tidPos != std::string::npos) {
      size_t start = message.find('"', tidPos + 13);
      size_t end = message.find('"', start + 1);
      if (start != std::string::npos && end != std::string::npos) {
        transferId = message.substr(start + 1, end - start - 1);
      }
    }
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    auto it = m_incomingTransfers.find(transferId);
    if (it != m_incomingTransfers.end()) {
      // Verify integrity if hash provided
      bool verified = true;
      if (!it->second.sha256Expected.empty()) {
        std::string actualHash = computeSHA256(it->second.data);
        verified = (actualHash == it->second.sha256Expected);
        if (!verified && m_onError) {
          m_onError(SignalingError::IntegrityCheckFailed, "SHA-256 mismatch");
        }
      }
      if (m_onTransferComplete) {
        m_onTransferComplete(transferId, it->second.filename, it->second.data,
                             verified);
      }
      m_incomingTransfers.erase(it);
    }
  }
}

// ============ File Transfer Methods ============
bool WebSignalingClient::requestFileSend(const std::string &targetPeerId,
                                         const std::vector<FileInfo> &files) {
  if (!isConnected())
    return false;

  std::ostringstream msg;
  msg << "{\"type\":\"file-request\",\"to\":\"" << targetPeerId
      << "\",\"from\":\"" << m_peerId << "\",\"files\":[";
  for (size_t i = 0; i < files.size(); i++) {
    if (i > 0)
      msg << ",";
    msg << "{\"name\":\"" << files[i].name << "\",\"size\":" << files[i].size
        << "}";
  }
  msg << "]}";
  return sendMessage(msg.str());
}

void WebSignalingClient::acceptFileRequest(const std::string &fromPeerId) {
  std::ostringstream msg;
  msg << "{\"type\":\"file-response\",\"to\":\"" << fromPeerId
      << "\",\"accepted\":true}";
  sendMessage(msg.str());
}

void WebSignalingClient::rejectFileRequest(const std::string &fromPeerId) {
  std::ostringstream msg;
  msg << "{\"type\":\"file-response\",\"to\":\"" << fromPeerId
      << "\",\"accepted\":false}";
  sendMessage(msg.str());
}

bool WebSignalingClient::sendFileViaRelay(const std::string &targetPeerId,
                                          const std::string &filename,
                                          const std::vector<uint8_t> &data,
                                          const std::string &mimeType) {
  if (!isConnected())
    return false;
  if (data.size() > SignalingConfig::MAX_FILE_SIZE) {
    if (m_onError)
      m_onError(SignalingError::MessageTooLarge, "File too large");
    return false;
  }

  std::string transferId = generateUUID();
  std::string sha256 = computeSHA256(data);

  // Send relay-start
  std::ostringstream startMsg;
  startMsg << "{\"type\":\"relay-start\",\"to\":\"" << targetPeerId
           << "\",\"transferId\":\"" << transferId << "\",\"filename\":\""
           << filename << "\",\"size\":" << data.size() << ",\"sha256\":\""
           << sha256 << "\"}";
  if (!sendMessageWithRetry(startMsg.str()))
    return false;

  // Send chunks
  size_t offset = 0;
  while (offset < data.size()) {
    size_t chunkSize =
        std::min(SignalingConfig::CHUNK_SIZE, data.size() - offset);
    std::string b64 = base64Encode(data.data() + offset, chunkSize);

    std::ostringstream chunkMsg;
    chunkMsg << "{\"type\":\"relay-chunk\",\"to\":\"" << targetPeerId
             << "\",\"transferId\":\"" << transferId
             << "\",\"offset\":" << offset << ",\"data\":\"" << b64 << "\"}";
    if (!sendMessage(chunkMsg.str()))
      return false;
    offset += chunkSize;

    // Small delay to avoid overwhelming
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Send relay-end
  std::ostringstream endMsg;
  endMsg << "{\"type\":\"relay-end\",\"to\":\"" << targetPeerId
         << "\",\"transferId\":\"" << transferId << "\"}";
  return sendMessageWithRetry(endMsg.str());
}

bool WebSignalingClient::streamFileViaRelay(const std::string &targetPeerId,
                                            const std::string &filePath) {
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file)
    return false;

  size_t fileSize = file.tellg();
  if (fileSize > SignalingConfig::MAX_FILE_SIZE) {
    if (m_onError)
      m_onError(SignalingError::MessageTooLarge, "File too large");
    return false;
  }

  file.seekg(0);

  // Extract filename
  std::string filename = filePath;
  size_t lastSlash = filePath.find_last_of("/\\");
  if (lastSlash != std::string::npos) {
    filename = filePath.substr(lastSlash + 1);
  }

  std::string transferId = generateUUID();
  std::string sha256 = computeSHA256(filePath);

  // Send relay-start
  std::ostringstream startMsg;
  startMsg << "{\"type\":\"relay-start\",\"to\":\"" << targetPeerId
           << "\",\"transferId\":\"" << transferId << "\",\"filename\":\""
           << filename << "\",\"size\":" << fileSize << ",\"sha256\":\""
           << sha256 << "\"}";
  if (!sendMessageWithRetry(startMsg.str()))
    return false;

  // Stream chunks from file
  char buffer[SignalingConfig::CHUNK_SIZE];
  size_t offset = 0;
  while (file && offset < fileSize) {
    file.read(buffer, SignalingConfig::CHUNK_SIZE);
    size_t read = file.gcount();
    if (read == 0)
      break;

    std::string b64 = base64Encode(reinterpret_cast<uint8_t *>(buffer), read);

    std::ostringstream chunkMsg;
    chunkMsg << "{\"type\":\"relay-chunk\",\"to\":\"" << targetPeerId
             << "\",\"transferId\":\"" << transferId
             << "\",\"offset\":" << offset << ",\"data\":\"" << b64 << "\"}";
    if (!sendMessage(chunkMsg.str()))
      return false;
    offset += read;

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  std::ostringstream endMsg;
  endMsg << "{\"type\":\"relay-end\",\"to\":\"" << targetPeerId
         << "\",\"transferId\":\"" << transferId << "\"}";
  return sendMessageWithRetry(endMsg.str());
}

void WebSignalingClient::cancelTransfer(const std::string &transferId) {
  std::lock_guard<std::mutex> lock(m_transfersMutex);
  m_incomingTransfers.erase(transferId);
  m_outgoingTransfers.erase(transferId);
}

bool WebSignalingClient::resumeTransfer(const std::string &transferId,
                                        size_t fromOffset) {
  // Not fully implemented - would require server support
  return false;
}

TransferProgress
WebSignalingClient::getTransferProgress(const std::string &transferId) const {
  std::lock_guard<std::mutex> lock(m_transfersMutex);

  auto it = m_outgoingTransfers.find(transferId);
  if (it != m_outgoingTransfers.end()) {
    return it->second;
  }

  auto inIt = m_incomingTransfers.find(transferId);
  if (inIt != m_incomingTransfers.end()) {
    TransferProgress progress;
    progress.transferId = inIt->second.transferId;
    progress.filename = inIt->second.filename;
    progress.totalBytes = inIt->second.totalSize;
    progress.transferredBytes = inIt->second.receivedBytes;
    progress.state = inIt->second.state;
    return progress;
  }

  return TransferProgress{};
}

std::vector<TransferProgress> WebSignalingClient::getAllTransfers() const {
  std::vector<TransferProgress> all;
  std::lock_guard<std::mutex> lock(m_transfersMutex);

  for (const auto &[id, t] : m_outgoingTransfers) {
    all.push_back(t);
  }
  for (const auto &[id, t] : m_incomingTransfers) {
    TransferProgress progress;
    progress.transferId = t.transferId;
    progress.filename = t.filename;
    progress.totalBytes = t.totalSize;
    progress.transferredBytes = t.receivedBytes;
    progress.state = t.state;
    all.push_back(progress);
  }

  return all;
}

} // namespace teleport
