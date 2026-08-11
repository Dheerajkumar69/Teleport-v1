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
#include <cctype>
#include <cstdio>
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

std::string JsonGetStringField(const std::string &json, const std::string &key,
                               size_t startPos = 0) {
  const std::string pattern = "\"" + key + "\"";
  size_t keyPos = json.find(pattern, startPos);
  if (keyPos == std::string::npos) {
    return "";
  }

  size_t colonPos = json.find(':', keyPos + pattern.size());
  if (colonPos == std::string::npos) {
    return "";
  }

  size_t valueStart = json.find('"', colonPos);
  if (valueStart == std::string::npos) {
    return "";
  }

  size_t valueEnd = valueStart + 1;
  while (valueEnd < json.size()) {
    if (json[valueEnd] == '"' && json[valueEnd - 1] != '\\') {
      break;
    }
    ++valueEnd;
  }

  if (valueEnd >= json.size()) {
    return "";
  }

  return json.substr(valueStart + 1, valueEnd - valueStart - 1);
}

size_t JsonGetSizeField(const std::string &json, const std::string &key,
                        size_t defaultValue = 0, size_t startPos = 0) {
  const std::string pattern = "\"" + key + "\"";
  size_t keyPos = json.find(pattern, startPos);
  if (keyPos == std::string::npos) {
    return defaultValue;
  }

  size_t colonPos = json.find(':', keyPos + pattern.size());
  if (colonPos == std::string::npos) {
    return defaultValue;
  }

  size_t valuePos = colonPos + 1;
  while (valuePos < json.size() &&
         std::isspace(static_cast<unsigned char>(json[valuePos]))) {
    ++valuePos;
  }

  if (valuePos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[valuePos]))) {
    return defaultValue;
  }

  size_t endPos = valuePos;
  while (endPos < json.size() &&
         std::isdigit(static_cast<unsigned char>(json[endPos]))) {
    ++endPos;
  }

  try {
    return static_cast<size_t>(std::stoull(json.substr(valuePos, endPos - valuePos)));
  } catch (...) {
    return defaultValue;
  }
}

bool JsonGetBoolField(const std::string &json, const std::string &key,
                      bool defaultValue = false, size_t startPos = 0) {
  const std::string pattern = "\"" + key + "\"";
  size_t keyPos = json.find(pattern, startPos);
  if (keyPos == std::string::npos) {
    return defaultValue;
  }

  size_t colonPos = json.find(':', keyPos + pattern.size());
  if (colonPos == std::string::npos) {
    return defaultValue;
  }

  size_t valuePos = colonPos + 1;
  while (valuePos < json.size() &&
         std::isspace(static_cast<unsigned char>(json[valuePos]))) {
    ++valuePos;
  }

  if (json.compare(valuePos, 4, "true") == 0) {
    return true;
  }
  if (json.compare(valuePos, 5, "false") == 0) {
    return false;
  }
  return defaultValue;
}

std::vector<FileInfo> JsonParseFileArray(const std::string &json) {
  std::vector<FileInfo> files;
  size_t searchPos = 0;

  while (true) {
    size_t namePos = json.find("\"name\"", searchPos);
    if (namePos == std::string::npos) {
      break;
    }

    FileInfo file;
    file.name = JsonGetStringField(json, "name", namePos);
    file.size = JsonGetSizeField(json, "size", 0, namePos);
    file.mimeType = JsonGetStringField(json, "mimeType", namePos);
    file.sha256 = JsonGetStringField(json, "sha256", namePos);

    if (!file.name.empty()) {
      files.push_back(file);
    }

    searchPos = namePos + 6;
  }

  return files;
}

std::string JsonEscape(const std::string &input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (char c : input) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[7];
        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
        out += buf;
      } else {
        out += c;
      }
      break;
    }
  }
  return out;
}

} // anonymous namespace

bool WebSignalingClient::isValidSha256Hex(const std::string &value) {
  if (value.empty()) {
    return true;
  }
  if (value.size() != 64) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isxdigit(c) != 0;
  });
}

RelayVerificationResult
WebSignalingClient::waitForRelayVerification(const std::string &transferId,
                                             int timeoutMs) {
  std::unique_lock<std::mutex> lock(m_verificationMutex);
  const bool available = m_verificationCv.wait_for(
      lock, std::chrono::milliseconds(timeoutMs), [&]() {
        return m_relayVerifications.find(transferId) !=
               m_relayVerifications.end();
      });

  if (!available) {
    return RelayVerificationResult{};
  }

  RelayVerificationResult result = m_relayVerifications[transferId];
  m_relayVerifications.erase(transferId);
  return result;
}

bool WebSignalingClient::waitForFileResponse(const std::string &targetPeerId,
                                             int timeoutMs,
                                             bool &accepted) {
  std::unique_lock<std::mutex> lock(m_fileResponseMutex);
  const bool available = m_fileResponseCv.wait_for(
      lock, std::chrono::milliseconds(timeoutMs), [&]() {
        return m_pendingFileResponses.find(targetPeerId) !=
               m_pendingFileResponses.end();
      });

  if (!available) {
    return false;
  }

  accepted = m_pendingFileResponses[targetPeerId];
  m_pendingFileResponses.erase(targetPeerId);
  return true;
}

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

#ifndef USE_OPENSSL
// ─────────────────────────────────────────────────────────────────────────────
// COMPILE-TIME SECURITY GUARD
// USE_OPENSSL is not defined.  This binary cannot connect to wss:// endpoints.
// All relay signaling will be unencrypted.  This is acceptable ONLY for local
// development over a trusted LAN.  Do NOT ship this build to end users.
// ─────────────────────────────────────────────────────────────────────────────
#pragma message("[SECURITY] Building WebSignalingClient without TLS. "\
               "wss:// is disabled; all signaling is PLAINTEXT. "\
               "Set USE_OPENSSL or install OpenSSL to enable TLS.")
  if (useTLS) {
    m_state = ConnectionState::Failed;
    if (m_onError) {
      m_onError(SignalingError::ConnectionFailed,
                "wss:// requires TLS support (USE_OPENSSL not enabled)");
    }
    return false;
  }
#endif

  // ─── Runtime plaintext warning ──────────────────────────────────────────
  // If we reach here with useTLS == false, the connection is UNENCRYPTED.
  // Emit a loud warning so it appears in every ops log, even if no UI is
  // attached. This is NEVER silent.
  if (!useTLS) {
    fputs("[TELEPORT SECURITY WARNING] Connecting over PLAINTEXT ws://. "
          "All signaling and relay transfers are UNENCRYPTED. "
          "Configure a wss:// URL in production.\n", stderr);
    fflush(stderr);
    // Also deliver through the error callback at severity None so the UI
    // can surface a visible banner (non-fatal — LAN use is intentional).
    OnErrorCallback warnCb;
    {
      std::lock_guard<std::mutex> cbLock(m_callbackMutex);
      warnCb = m_onError;
    }
    if (warnCb) {
      warnCb(SignalingError::None,
             "SECURITY: Signaling over plaintext ws://. "
             "Relay transfers will be unencrypted.");
    }
  }

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

  // Reconnect path can leave a finished but joinable thread object behind.
  if (m_messageThread && m_messageThread->joinable()) {
    m_messageThread->join();
  }

  // Start message processing thread
  m_messageThread =
      std::make_unique<std::thread>(&WebSignalingClient::processMessages, this);

  // BUG FIX (Bug W1): Heartbeat was disabled, leaving zombie connections alive
  // indefinitely.  Re-enable so ping failures trigger scheduleReconnect().
  if (m_heartbeatThread && m_heartbeatThread->joinable()) {
    m_heartbeatThread->join();
  }
  m_heartbeatThread =
      std::make_unique<std::thread>(&WebSignalingClient::heartbeatLoop, this);

  // Send join — server assigns peerId via 'welcome', do NOT send our local UUID
  // Include fingerprint:null so web peers can identify us as a non-E2E peer
  std::ostringstream joinMsg;
  joinMsg << "{\"type\":\"join\",\"room\":\"" << JsonEscape(m_room)
          << "\",\"name\":\"" << JsonEscape(m_deviceName)
          << "\",\"platform\":\"desktop\",\"fingerprint\":null}";
  sendMessage(joinMsg.str());

  // Notify callback
  if (m_onConnected) {
    std::lock_guard<std::mutex> cbLock(m_callbackMutex);
    m_onConnected();
  }

  // Announce any transfers that were interrupted before the last disconnect.
  // This lets the remote browser peer know the desktop is ready to resume.
  announceReconnectHints();

  return true;
}

void WebSignalingClient::disconnect() {
  m_stopRequested = true;
  m_running = false;
  m_autoReconnect = false; // Disable reconnect on explicit disconnect

  // Close socket FIRST to unblock any blocking recv/send calls in threads
  if (m_socket != -1) {
    // Try to send close frame (may fail if already closed)
    uint8_t closeFrame[4] = {0x88, 0x02, 0x03, 0xE8}; // Close with code 1000
    sendWithTimeout(m_socket, closeFrame, 4, 100);    // Short timeout
    CLOSE_SOCKET(m_socket);
    m_socket = -1;
  }

  // Now join threads (they should exit since socket is closed)
  if (m_messageThread && m_messageThread->joinable()) {
    m_messageThread->join();
  }
  if (m_heartbeatThread && m_heartbeatThread->joinable()) {
    m_heartbeatThread->join();
  }
  if (m_reconnectThread && m_reconnectThread->joinable()) {
    m_reconnectThread->join();
  }
  m_reconnectInProgress = false;

#ifdef USE_OPENSSL
  // Clean up SSL AFTER threads are stopped
  if (m_ssl) {
    SSL_free(static_cast<SSL *>(m_ssl));
    m_ssl = nullptr;
  }
  if (m_sslContext) {
    SSL_CTX_free(static_cast<SSL_CTX *>(m_sslContext));
    m_sslContext = nullptr;
  }
  m_useTLS = false;
#endif

  m_state = ConnectionState::Disconnected;

  {
    std::lock_guard<std::mutex> verificationLock(m_verificationMutex);
    m_relayVerifications.clear();
  }
  m_verificationCv.notify_all();

  {
    std::lock_guard<std::mutex> responseLock(m_fileResponseMutex);
    m_pendingFileResponses.clear();
  }
  m_fileResponseCv.notify_all();

  if (m_onDisconnected) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onDisconnected("Disconnected by user");
  }
}

// ============ Reconnect Loop ============
void WebSignalingClient::scheduleReconnect() {
  if (!m_autoReconnect || m_stopRequested) {
    return;
  }

  bool expected = false;
  if (!m_reconnectInProgress.compare_exchange_strong(expected, true)) {
    return;
  }

  if (m_reconnectThread && m_reconnectThread->joinable()) {
    if (m_reconnectThread->get_id() != std::this_thread::get_id()) {
      m_reconnectThread->join();
    }
  }

  m_reconnectThread =
      std::make_unique<std::thread>(&WebSignalingClient::reconnectLoop, this);
}

void WebSignalingClient::reconnectLoop() {
  struct ReconnectGuard {
    std::atomic<bool> &flag;
    ~ReconnectGuard() { flag.store(false); }
  } guard{m_reconnectInProgress};

  while (m_autoReconnect && !m_stopRequested &&
         m_reconnectAttempt < SignalingConfig::RECONNECT_MAX_ATTEMPTS) {

    m_state = ConnectionState::Reconnecting;
    m_reconnectAttempt++;

    // BUG FIX (Bug W2): Clear stale peer list on each reconnect attempt so
    // the UI never shows peers that are no longer reachable.
    {
      std::lock_guard<std::mutex> pLock(m_peersMutex);
      m_peers.clear();
    }

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
          scheduleReconnect();
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

  for (auto it = m_outgoingTransfers.begin(); it != m_outgoingTransfers.end();) {
    if (it->second.state != TransferState::InProgress) {
      ++it;
      continue;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - it->second.lastChunkTime)
                       .count();
    if (elapsed > SignalingConfig::TRANSFER_TIMEOUT_MS) {
      it = m_outgoingTransfers.erase(it);
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

void WebSignalingClient::setOnOffer(OnOfferCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onOffer = std::move(cb);
}

void WebSignalingClient::setOnAnswer(OnAnswerCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onAnswer = std::move(cb);
}

void WebSignalingClient::setOnIceCandidate(OnIceCandidateCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onIceCandidate = std::move(cb);
}

void WebSignalingClient::setOnError(OnErrorCallback cb) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_onError = std::move(cb);
}

// ============ Process Messages Loop ============
void WebSignalingClient::processMessages() {
  while (m_running && !m_stopRequested) {
    // Check if socket is still valid
    if (m_socket == -1)
      break;

    uint8_t header[2];
    int received =
        recvWithTimeout(m_socket, header, 2, SignalingConfig::READ_TIMEOUT_MS);

    // Handle receive result
    if (received == 0) {
      // Connection closed by server
      break;
    }
    if (received < 0) {
      // Timeout or error - check if still running, then continue waiting
      if (!m_running || m_stopRequested || m_socket == -1)
        break;
      continue;
    }

    // Parse WebSocket frame
    int opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    size_t payloadLen = header[1] & 0x7F;

    // Extended payload length
    // BUG FIX (Bug W3): a partial recv of the length extension bytes is a
    // fatal framing error — break (not continue) so we reconnect rather than
    // trying to re-parse starting at a wrong position.
    if (payloadLen == 126) {
      uint8_t ext[2];
      if (recvWithTimeout(m_socket, ext, 2, SignalingConfig::READ_TIMEOUT_MS) !=
          2)
        break;
      payloadLen = (ext[0] << 8) | ext[1];
    } else if (payloadLen == 127) {
      uint8_t ext[8];
      if (recvWithTimeout(m_socket, ext, 8, SignalingConfig::READ_TIMEOUT_MS) !=
          8)
        break;
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
    } else if (opcode == 0x02) { // Binary frame — not used; skip gracefully
      // No-op: binary frames are not part of our protocol.
    } else if (opcode == 0x08) { // Close frame
      // BUG FIX (Bug W4): break out of the loop immediately so the
      // post-loop reconnect logic runs — previously we only set m_running=false
      // and called scheduleReconnect() inside the loop, but continued to the
      // next iteration where the socket was already closed by the server.
      m_running = false;
      break;
    } else if (opcode == 0x09) { // Ping
      uint8_t mask[4];
      std::random_device rd;
      for (int i = 0; i < 4; i++) mask[i] = rd() & 0xFF;
      uint8_t pongFrame[6] = {0x8A, 0x80, mask[0], mask[1], mask[2], mask[3]};
      sendWithTimeout(m_socket, pongFrame, 6, 1000);
    } else if (opcode == 0x0A) { // Pong — server reply to our heartbeat ping
      m_lastActivity = std::chrono::steady_clock::now();
    }
  }

  // Disconnected
  if (m_autoReconnect && !m_stopRequested) {
    scheduleReconnect();
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

  if (type == "welcome") {
    // Server assigns us a stable peerId (format: peer_xxxxxxxx).
    // We must capture this because it is what all other peers see us as.
    std::string serverPeerId = JsonGetStringField(message, "peerId");
    if (!serverPeerId.empty()) {
      std::lock_guard<std::mutex> lock(m_peersMutex); // avoids using stateMutex (held by connect())
      m_peerId = serverPeerId;
    }
    return; // nothing else to do for welcome
  } else if (type == "peers") {
    std::vector<WebPeer> peers;
    size_t pos = 0;
    while ((pos = message.find("{\"id\":", pos)) != std::string::npos) {
      size_t objEnd = message.find('}', pos);
      if (objEnd == std::string::npos) break;
      
      std::string objStr = message.substr(pos, objEnd - pos + 1);
      
      WebPeer peer;
      peer.id = JsonGetStringField(objStr, "id");
      peer.name = JsonGetStringField(objStr, "name");
      peer.platform = JsonGetStringField(objStr, "clientType");
      if (peer.platform.empty()) peer.platform = "web";
      
      peer.isWeb = true;
      peer.lastSeen = std::chrono::steady_clock::now();
      
      if (!peer.id.empty() && peer.id != m_peerId) {
        bool duplicate = false;
        for (const auto& existing : peers) {
          if (existing.id == peer.id) duplicate = true;
        }
        if (!duplicate) {
          peers.push_back(peer);
        }
      }
      pos = objEnd + 1;
    }
    {
      std::lock_guard<std::mutex> lock(m_peersMutex);
      m_peers = std::move(peers);
    }
    if (m_onPeersUpdated) {
      std::vector<WebPeer> peersSnapshot;
      {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        peersSnapshot = m_peers;
      }
      std::lock_guard<std::mutex> lock(m_callbackMutex);
      m_onPeersUpdated(peersSnapshot);
    }
  } else if (type == "peer-joined") {
    WebPeer peer;
    peer.id = JsonGetStringField(message, "id");
    if (peer.id.empty()) {
      peer.id = JsonGetStringField(message, "peerId");
    }
    peer.name = JsonGetStringField(message, "name");
    if (peer.name.empty()) {
      peer.name = JsonGetStringField(message, "fromName");
    }
    peer.platform = JsonGetStringField(message, "platform");
    if (peer.platform.empty()) {
      peer.platform = "web";
    }
    peer.isWeb = true;
    peer.lastSeen = std::chrono::steady_clock::now();

    if (!peer.id.empty() && peer.id != m_peerId) {
      std::vector<WebPeer> peersSnapshot;
      {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        auto it = std::find_if(m_peers.begin(), m_peers.end(),
                               [&](const WebPeer &p) { return p.id == peer.id; });
        if (it == m_peers.end()) {
          m_peers.push_back(peer);
        } else {
          *it = peer;
        }
        peersSnapshot = m_peers;
      }
      OnPeersUpdatedCallback peersCb;
      {
        std::lock_guard<std::mutex> cbLock(m_callbackMutex);
        peersCb = m_onPeersUpdated;
      }
      if (peersCb) {
        peersCb(peersSnapshot);
      }
    }
  } else if (type == "peer-left") {
    std::string peerId = JsonGetStringField(message, "peerId");
    if (!peerId.empty()) {
      std::vector<WebPeer> peersSnapshot;
      bool removed = false;
      {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        auto newEnd = std::remove_if(m_peers.begin(), m_peers.end(),
                                     [&](const WebPeer &p) {
                                       return p.id == peerId;
                                     });
        removed = (newEnd != m_peers.end());
        m_peers.erase(newEnd, m_peers.end());
        peersSnapshot = m_peers;
      }

      if (removed) {
        OnPeersUpdatedCallback peersCb;
        {
          std::lock_guard<std::mutex> cbLock(m_callbackMutex);
          peersCb = m_onPeersUpdated;
        }
        if (peersCb) {
          peersCb(peersSnapshot);
        }
      }
    }
  } else if (type == "file-request") {
    // Parse file request
    std::string fromId = JsonGetStringField(message, "from");
    std::string fromName = JsonGetStringField(message, "fromName");
    if (fromName.empty()) {
      fromName = JsonGetStringField(message, "name");
    }

    std::vector<FileInfo> files = JsonParseFileArray(message);

    // Backward compatibility for minimal payloads containing filename/size.
    if (files.empty()) {
      FileInfo single;
      single.name = JsonGetStringField(message, "filename");
      single.size = JsonGetSizeField(message, "size", 0);
      if (!single.name.empty()) {
        files.push_back(std::move(single));
      }
    }

    if (m_onFileRequest) {
      std::lock_guard<std::mutex> lock(m_callbackMutex);
      m_onFileRequest(fromId, fromName, files);
    }
  } else if (type == "file-response") {
    std::string fromId = JsonGetStringField(message, "from");
    bool accepted = JsonGetBoolField(message, "accepted", false);

    if (!fromId.empty()) {
      {
        std::lock_guard<std::mutex> lock(m_fileResponseMutex);
        m_pendingFileResponses[fromId] = accepted;
      }
      m_fileResponseCv.notify_all();
    }
  } else if (type == "offer") {
    const std::string fromId = JsonGetStringField(message, "from");
    const std::string sdp = JsonGetStringField(message, "sdp");
    if (!fromId.empty() && !sdp.empty()) {
      auto it = m_webrtcClients.find(fromId);
      if (it == m_webrtcClients.end()) {
        auto client = std::make_shared<NativeWebRTCClient>();
        client->init(fromId);

        // Outgoing signaling: send our SDP/ICE back through the server.
        client->setOnLocalDescription([this, fromId](const std::string& t, const std::string& desc) {
           if (t == "answer") this->sendAnswer(fromId, desc);
           else if (t == "offer") this->sendOffer(fromId, desc);
        });
        client->setOnLocalCandidate([this, fromId](const std::string& cand, const std::string&) {
           this->sendIceCandidate(fromId, cand);
        });

        // ── RECEIVE PATH (Browser → Desktop) ──────────────────────────────
        // Binary chunks: route to handleWebRTCChunk which writes to the
        // active RelayTransfer (streaming or in-memory).
        client->setOnDataChannelMessage([this, fromId](const uint8_t* data, size_t size) {
          handleWebRTCChunk(fromId, data, size);
        });
        // JSON control messages: file-start / file-end / resume-ready
        client->setOnDataChannelStringMessage([this, fromId](const std::string& msg) {
          handleWebRTCControlMessage(fromId, msg);
        });

        m_webrtcClients[fromId] = client;
        it = m_webrtcClients.find(fromId);
      }
      it->second->processOffer(sdp);
    }

  } else if (type == "answer") {
    const std::string fromId = JsonGetStringField(message, "from");
    const std::string sdp = JsonGetStringField(message, "sdp");
    if (!fromId.empty() && !sdp.empty()) {
        auto it = m_webrtcClients.find(fromId);
        if (it != m_webrtcClients.end()) {
             it->second->processAnswer(sdp);
        }
    }
  } else if (type == "ice") {
    const std::string fromId = JsonGetStringField(message, "from");
    std::string candidate = JsonGetStringField(message, "candidate");
    if (candidate.empty()) {
      candidate = JsonGetStringField(message, "sdp");
    }
    if (!fromId.empty() && !candidate.empty()) {
        auto it = m_webrtcClients.find(fromId);
        if (it != m_webrtcClients.end()) {
             it->second->processIceCandidate(candidate, "0");
        }
    }
  } else if (type == "relay-start") {
    // Start new incoming transfer
    std::string transferId = JsonGetStringField(message, "transferId");
    if (transferId.empty()) {
      return;
    }

    RelayTransfer transfer;
    transfer.transferId = transferId;
    transfer.fromPeerId = JsonGetStringField(message, "from");
    transfer.filename = JsonGetStringField(message, "filename");
    transfer.totalSize = JsonGetSizeField(message, "size", 0);
    transfer.fileIndex =
        static_cast<int>(JsonGetSizeField(message, "fileIndex", 0));
    transfer.totalFiles =
        static_cast<int>(JsonGetSizeField(message, "totalFiles", 1));
    transfer.sha256Expected = JsonGetStringField(message, "sha256");
    transfer.state = TransferState::InProgress;
    transfer.lastActivity = std::chrono::steady_clock::now();

    // sha256 is optional — if present it must be a valid 64-hex-char digest
    if (!transfer.sha256Expected.empty() &&
        !isValidSha256Hex(transfer.sha256Expected)) {
      // SHA-256 was provided but is malformed → reject
      std::ostringstream cancelMsg;
      cancelMsg << "{\"type\":\"relay-cancel\",\"to\":\""
                << JsonEscape(transfer.fromPeerId) << "\",\"transferId\":\""
                << JsonEscape(transfer.transferId)
                << "\",\"reason\":\"invalid-sha256\"}";
      sendMessage(cancelMsg.str());
      if (m_onError)
        m_onError(SignalingError::InvalidMessage,
                  "Incoming relay transfer has malformed sha256");
      return;
    }

    if (!m_downloadPath.empty() &&
        transfer.totalSize > SignalingConfig::STREAM_THRESHOLD) {
      // Large file — stream to a temp file on disk to avoid RAM exhaustion
      transfer.streaming = true;
      transfer.tempFilePath =
          m_downloadPath + "/.__tmp_" + transfer.transferId;
      transfer.tempFileHandle = std::make_shared<std::ofstream>(
          transfer.tempFilePath, std::ios::binary | std::ios::trunc);
      if (!transfer.tempFileHandle->is_open()) {
        // Temp file creation failed — fall back to memory if size permits
        transfer.streaming = false;
        transfer.tempFileHandle.reset();
        if (transfer.totalSize > SignalingConfig::MAX_RECEIVE_BUFFER_SIZE) {
          if (m_onError)
            m_onError(SignalingError::MessageTooLarge,
                      "File too large and cannot stream to disk");
          return;
        }
        transfer.data.reserve(transfer.totalSize);
      }
    } else {
      // Small file — buffer in memory
      if (transfer.totalSize > SignalingConfig::MAX_RECEIVE_BUFFER_SIZE) {
        if (m_onError)
          m_onError(SignalingError::MessageTooLarge,
                    "Incoming relay transfer exceeds receive buffer limit");
        return;
      }
      if (transfer.totalSize > 0) {
        transfer.data.reserve(transfer.totalSize);
      }
    }

    // ── Resume check ──────────────────────────────────────────────────────
    // If we have a sidecar for this transferId AND the temp file still exists
    // with some data, reply with relay-resume-request instead of starting from
    // scratch.  The browser will then slice the File from the resume offset.
    if (m_resumeManager) {
      RelayResumeState saved;
      if (m_resumeManager->load(transferId, saved) &&
          saved.receivedBytes > 0 &&
          !saved.tempFilePath.empty()) {
        // Verify the temp file is still present
        std::ifstream probe(saved.tempFilePath, std::ios::binary | std::ios::ate);
        if (probe.is_open() && static_cast<size_t>(probe.tellg()) == saved.receivedBytes) {
          probe.close();
          // Restore in-memory state from disk
          transfer.streaming       = saved.streaming;
          transfer.tempFilePath    = saved.tempFilePath;
          transfer.receivedBytes   = saved.receivedBytes;
          transfer.sha256Expected  = transfer.sha256Expected.empty()
                                         ? saved.sha256Expected
                                         : transfer.sha256Expected;
          transfer.nextPersistAt   = saved.receivedBytes +
                                         RelayResumeManager::PERSIST_INTERVAL_BYTES;
          if (transfer.streaming) {
            transfer.tempFileHandle = std::make_shared<std::ofstream>(
                transfer.tempFilePath,
                std::ios::binary | std::ios::app); // append!
          }
          if (!transfer.tempFileHandle || !transfer.tempFileHandle->is_open()) {
            // Can't reopen — fall through to fresh start
            transfer.streaming     = false;
            transfer.tempFileHandle.reset();
            transfer.receivedBytes = 0;
            transfer.nextPersistAt = 0;
          } else {
            // State successfully restored — tell the browser to resume
            std::lock_guard<std::mutex> lock(m_transfersMutex);
            m_incomingTransfers[transferId] = transfer;
            sendRelayResumeRequest(transfer.fromPeerId, transferId,
                                   transfer.receivedBytes);
            return;
          }
        }
      }
    }
    // ── End resume check ──────────────────────────────────────────────────

    std::lock_guard<std::mutex> lock(m_transfersMutex);
    m_incomingTransfers[transferId] = transfer;
  } else if (type == "relay-chunk") {
    // Receive chunk
    std::string transferId = JsonGetStringField(message, "transferId");
    if (transferId.empty()) {
      return;
    }

    size_t offset = JsonGetSizeField(message, "offset", 0);
    size_t dataPos = message.find("\"data\":");
    if (dataPos != std::string::npos) {
      size_t start = message.find('"', dataPos + 7);
      // BUG FIX (Bug W5): The old code searched for the closing '"' with a
      // bare find(), which stopped at the first unescaped quote.  base64
      // doesn't contain backslashes, but a safe scanner is still better.
      // Scan forward skipping \" escape sequences.
      size_t end = start + 1;
      while (end < message.size()) {
        if (message[end] == '\\') { ++end; } // skip escape prefix
        else if (message[end] == '"') { break; }
        ++end;
      }
      if (start != std::string::npos && end < message.size()) {
        std::string b64 = message.substr(start + 1, end - start - 1);
        std::vector<uint8_t> chunk = base64Decode(b64);
        TransferProgress progress;
        bool emitProgress = false;
        SignalingError pendingError = SignalingError::None;
        std::string pendingErrorMessage;

        {
          std::lock_guard<std::mutex> lock(m_transfersMutex);
          auto it = m_incomingTransfers.find(transferId);
          if (it != m_incomingTransfers.end()) {
            if (offset != it->second.receivedBytes) {
              pendingError = SignalingError::InvalidMessage;
              pendingErrorMessage = "Out-of-order relay chunk offset";
              m_incomingTransfers.erase(it);
            } else if (it->second.streaming && it->second.tempFileHandle &&
                       it->second.tempFileHandle->is_open()) {
              // Streaming mode: write chunk to disk
              it->second.tempFileHandle->write(
                  reinterpret_cast<const char *>(chunk.data()), chunk.size());
              if (it->second.tempFileHandle->fail()) {
                // Disk write error
                it->second.tempFileHandle->close();
                std::remove(it->second.tempFilePath.c_str());
                if (m_resumeManager) m_resumeManager->remove(it->second.transferId);
                m_incomingTransfers.erase(it);
                pendingError = SignalingError::TransferFailed;
                pendingErrorMessage = "Disk write failed during streaming";
              } else {
                it->second.receivedBytes += chunk.size();
                it->second.lastActivity = std::chrono::steady_clock::now();

                // Durable checkpoint every PERSIST_INTERVAL_BYTES
                if (m_resumeManager &&
                    it->second.receivedBytes >= it->second.nextPersistAt) {
                  RelayResumeState snap;
                  snap.transferId     = it->second.transferId;
                  snap.fromPeerId     = it->second.fromPeerId;
                  snap.filename       = it->second.filename;
                  snap.totalSize      = it->second.totalSize;
                  snap.receivedBytes  = it->second.receivedBytes;
                  snap.sha256Expected = it->second.sha256Expected;
                  snap.tempFilePath   = it->second.tempFilePath;
                  snap.streaming      = it->second.streaming;
                  m_resumeManager->save(snap);
                  it->second.nextPersistAt = it->second.receivedBytes +
                                            RelayResumeManager::PERSIST_INTERVAL_BYTES;
                }

                progress.transferId      = it->second.transferId;
                progress.filename        = it->second.filename;
                progress.totalBytes      = it->second.totalSize;
                progress.transferredBytes = it->second.receivedBytes;
                progress.state           = TransferState::InProgress;
                emitProgress = true;
              }
            } else {
              // In-memory mode: guard against overflow.
              // BUG FIX (Bug W6): Also reject a single chunk that is larger
              // than the buffer limit, not just cumulative overflow.
              const bool overflow =
                  chunk.size() > SignalingConfig::MAX_RECEIVE_BUFFER_SIZE ||
                  it->second.data.size() + chunk.size() >
                      SignalingConfig::MAX_RECEIVE_BUFFER_SIZE;
              if (overflow) {
                pendingError = SignalingError::BufferOverflow;
                pendingErrorMessage = "Transfer buffer overflow";
                m_incomingTransfers.erase(it);
              } else {
                it->second.data.insert(it->second.data.end(), chunk.begin(),
                                       chunk.end());
                it->second.receivedBytes += chunk.size();
                it->second.lastActivity = std::chrono::steady_clock::now();
                progress.transferId = it->second.transferId;
                progress.filename = it->second.filename;
                progress.totalBytes = it->second.totalSize;
                progress.transferredBytes = it->second.receivedBytes;
                progress.state = TransferState::InProgress;
                emitProgress = true;
              }
            }
          }
        }

        if (pendingError != SignalingError::None && m_onError) {
          m_onError(pendingError, pendingErrorMessage);
        }

        if (emitProgress) {
          OnTransferProgressCallback progressCb;
          {
            std::lock_guard<std::mutex> cbLock(m_callbackMutex);
            progressCb = m_onTransferProgress;
          }
          if (progressCb) {
            progressCb(progress);
          }
        }
      }
    }
  } else if (type == "relay-end") {
    std::string transferId = JsonGetStringField(message, "transferId");
    std::string fromPeerId = JsonGetStringField(message, "from");
    std::string filename;
    std::vector<uint8_t> data;
    bool verified = true;
    std::string verificationReason;
    std::string actualHash;
    bool emitComplete = false;
    bool emitIntegrityError = false;

    {
      std::lock_guard<std::mutex> lock(m_transfersMutex);
      auto it = m_incomingTransfers.find(transferId);
      if (it != m_incomingTransfers.end()) {
        // ---- Streaming mode: finalize file on disk ----
        if (it->second.streaming) {
          if (it->second.tempFileHandle && it->second.tempFileHandle->is_open()) {
            it->second.tempFileHandle->flush();
            it->second.tempFileHandle->close();
          }
          it->second.tempFileHandle.reset();

          filename = it->second.filename;
          fromPeerId = it->second.fromPeerId.empty()
                           ? fromPeerId
                           : it->second.fromPeerId;

          // Size verification
          if (it->second.receivedBytes != it->second.totalSize) {
            verified = false;
            verificationReason = "size-mismatch";
          }

          // SHA-256 verification (read file back — one extra read but keeps code simple)
          if (verified && !it->second.sha256Expected.empty()) {
            actualHash = computeSHA256(it->second.tempFilePath);
            if (!actualHash.empty() &&
                isValidSha256Hex(it->second.sha256Expected) &&
                actualHash != it->second.sha256Expected) {
              verified = false;
              verificationReason = "sha256-mismatch";
            }
          }

          if (verified) {
            // Move temp file to final destination — then delete the sidecar.
            if (m_resumeManager) m_resumeManager->remove(transferId);
            std::string safeName = it->second.filename;
            // Sanitize: strip path components and dangerous chars
            {
              size_t sl = safeName.find_last_of("/\\");
              if (sl != std::string::npos)
                safeName = safeName.substr(sl + 1);
              for (auto &c : safeName) {
                if (c == ':' || c == '*' || c == '?' || c == '"' ||
                    c == '<' || c == '>' || c == '|')
                  c = '_';
              }
              if (safeName.empty())
                safeName = "received_file";
            }
            std::string finalPath = m_downloadPath + "/" + safeName;
            // Avoid overwriting existing files
            if (std::ifstream(finalPath).good()) {
              size_t dot = safeName.rfind('.');
              std::string base =
                  dot == std::string::npos ? safeName : safeName.substr(0, dot);
              std::string ext =
                  dot == std::string::npos ? "" : safeName.substr(dot);
              int n = 1;
              do {
                finalPath = m_downloadPath + "/" + base + "_" +
                            std::to_string(n++) + ext;
              } while (std::ifstream(finalPath).good() && n < 1000);
            }
            it->second.finalFilePath = finalPath;
            if (std::rename(it->second.tempFilePath.c_str(),
                            finalPath.c_str()) != 0) {
              // rename failed — try copy+delete
              std::ifstream src(it->second.tempFilePath, std::ios::binary);
              std::ofstream dst(finalPath, std::ios::binary);
              if (src && dst) {
                dst << src.rdbuf();
                src.close();
                dst.close();
                std::remove(it->second.tempFilePath.c_str());
              } else {
                verified = false;
                verificationReason = "file-save-failed";
              }
            }
          } else {
            // Verification failed — delete temp file
            std::remove(it->second.tempFilePath.c_str());
          }

          // Send verification ack
          if (!transferId.empty() && !fromPeerId.empty()) {
            std::ostringstream verifyMsg;
            verifyMsg << "{\"type\":\"relay-verified\",\"to\":\""
                      << JsonEscape(fromPeerId) << "\",\"transferId\":\""
                      << JsonEscape(transferId) << "\",\"ok\":"
                      << (verified ? "true" : "false") << ",\"reason\":\""
                      << JsonEscape(verificationReason) << "\",\"sha256\":\""
                      << JsonEscape(actualHash) << "\"}";
            sendMessage(verifyMsg.str());
          }

          // Callback with empty data — file is already on disk.
          // Pass the actual saved path as filename so the bridge can show it.
          if (m_onTransferComplete) {
            m_onTransferComplete(
                transferId,
                verified ? it->second.finalFilePath : it->second.filename,
                {} /* empty - file already on disk */, verified);
          }

          m_incomingTransfers.erase(it);
          return; // streaming path fully handled above
        }
        // ---- End streaming mode ----

        if (fromPeerId.empty()) {
          fromPeerId = it->second.fromPeerId;
        }
        filename = it->second.filename;
        data = it->second.data;
        if (it->second.receivedBytes != it->second.totalSize) {
          verified = false;
          verificationReason = "size-mismatch";
        }
        actualHash = computeSHA256(it->second.data);
        if (!it->second.sha256Expected.empty()) {
          if (!isValidSha256Hex(it->second.sha256Expected)) {
            verified = false;
            verificationReason = "invalid-sha256";
          } else if (actualHash != it->second.sha256Expected) {
            verified = false;
            verificationReason = "sha256-mismatch";
          }
          emitIntegrityError = !verified;
        }

        if (!verified && verificationReason.empty()) {
          verificationReason = "integrity-check-failed";
        }

        emitComplete = true;
        m_incomingTransfers.erase(it);
      }
    }

    if (!transferId.empty() && !fromPeerId.empty()) {
      std::ostringstream verifyMsg;
      verifyMsg << "{\"type\":\"relay-verified\",\"to\":\""
                << JsonEscape(fromPeerId) << "\",\"transferId\":\""
                << JsonEscape(transferId) << "\",\"ok\":"
                << (verified ? "true" : "false") << ",\"reason\":\""
                << JsonEscape(verificationReason) << "\",\"sha256\":\""
                << JsonEscape(actualHash) << "\"}";
      sendMessage(verifyMsg.str());
    }

    if (emitIntegrityError && m_onError) {
      std::string message = verificationReason.empty() ? "SHA-256 mismatch"
                                                       : verificationReason;
      m_onError(SignalingError::IntegrityCheckFailed, message);
    }

    if (emitComplete && m_onTransferComplete) {
      m_onTransferComplete(transferId, filename, data, verified);
    }
  } else if (type == "relay-verified") {
    std::string transferId = JsonGetStringField(message, "transferId");
    if (transferId.empty()) {
      return;
    }

    RelayVerificationResult result;
    result.received = true;
    result.ok = JsonGetBoolField(message, "ok", false);
    result.reason = JsonGetStringField(message, "reason");
    result.sha256 = JsonGetStringField(message, "sha256");

    {
      std::lock_guard<std::mutex> lock(m_verificationMutex);
      m_relayVerifications[transferId] = result;
    }
    m_verificationCv.notify_all();
  } else if (type == "relay-cancel") {
    std::string transferId = JsonGetStringField(message, "transferId");
    if (transferId.empty()) {
      return;
    }

    RelayVerificationResult result;
    result.received = true;
    result.ok = false;
    result.reason = JsonGetStringField(message, "reason");
    if (result.reason.empty()) {
      result.reason = "relay-cancelled";
    }

    // Clean up any persisted sidecar for this cancelled transfer
    if (m_resumeManager) {
      m_resumeManager->remove(transferId);
    }

    {
      std::lock_guard<std::mutex> lock(m_verificationMutex);
      m_relayVerifications[transferId] = result;
    }
    m_verificationCv.notify_all();
  } else if (type == "peer-lan-updated") {
    // A peer announced its LAN IP/port. Update local record so UI shows the LAN badge.
    std::string updatedPeerId = JsonGetStringField(message, "peerId");
    if (!updatedPeerId.empty() && updatedPeerId != m_peerId) {
      std::lock_guard<std::mutex> lock(m_peersMutex);
      for (auto &p : m_peers) {
        if (p.id == updatedPeerId) {
          p.isWeb = false; // has a real LAN presence
          break;
        }
      }
    }
  }
}

// ============ File Transfer Methods ============
bool WebSignalingClient::requestFileSend(const std::string &targetPeerId,
                                         const std::vector<FileInfo> &files) {
  if (!isConnected())
    return false;

  {
    std::lock_guard<std::mutex> lock(m_fileResponseMutex);
    m_pendingFileResponses.erase(targetPeerId);
  }

  std::ostringstream msg;
  msg << "{\"type\":\"file-request\",\"to\":\""
      << JsonEscape(targetPeerId) << "\",\"files\":[";
  for (size_t i = 0; i < files.size(); i++) {
    if (i > 0)
      msg << ",";
    const std::string mimeType = files[i].mimeType.empty()
                                     ? "application/octet-stream"
                                     : files[i].mimeType;
    msg << "{\"name\":\"" << JsonEscape(files[i].name)
        << "\",\"size\":" << files[i].size
        << ",\"type\":\"" << JsonEscape(mimeType)
        << "\",\"relativePath\":\"\"}";
  }
  msg << "]}";
  if (!sendMessage(msg.str())) {
    return false;
  }

  bool accepted = false;
  if (!waitForFileResponse(targetPeerId,
                           SignalingConfig::FILE_REQUEST_TIMEOUT_MS,
                           accepted)) {
    if (m_onError) {
      m_onError(SignalingError::TransferFailed,
                "Timed out waiting for file request response");
    }
    return false;
  }

  if (!accepted && m_onError) {
    m_onError(SignalingError::TransferFailed,
              "Peer rejected incoming file request");
  }

  return accepted;
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
  (void)mimeType;
  if (!isConnected())
    return false;
  if (data.size() > SignalingConfig::MAX_FILE_SIZE) {
    if (m_onError)
      m_onError(SignalingError::MessageTooLarge, "File too large");
    return false;
  }

  std::string transferId = generateUUID();
  std::string sha256 = computeSHA256(data);

  {
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    TransferProgress progress;
    progress.transferId = transferId;
    progress.targetPeerId = targetPeerId;
    progress.filename = filename;
    progress.totalBytes = data.size();
    progress.transferredBytes = 0;
    progress.state = TransferState::InProgress;
    progress.startTime = std::chrono::steady_clock::now();
    progress.lastChunkTime = progress.startTime;
    m_outgoingTransfers[transferId] = std::move(progress);
  }

  // Send relay-start
  std::ostringstream startMsg;
  startMsg << "{\"type\":\"relay-start\",\"to\":\""
           << JsonEscape(targetPeerId) << "\",\"transferId\":\""
           << JsonEscape(transferId) << "\",\"filename\":\""
           << JsonEscape(filename) << "\",\"size\":" << data.size()
           << ",\"mimeType\":\"application/octet-stream\""
           << ",\"fileIndex\":0,\"totalFiles\":1"
           << ",\"sha256\":\"" << JsonEscape(sha256) << "\"}";
  if (!sendMessageWithRetry(startMsg.str())) {
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    m_outgoingTransfers.erase(transferId);
    return false;
  }

  // Send chunks
  size_t offset = 0;
  while (offset < data.size()) {
    size_t chunkSize =
        std::min(SignalingConfig::CHUNK_SIZE, data.size() - offset);
    std::string b64 = base64Encode(data.data() + offset, chunkSize);

    std::ostringstream chunkMsg;
    chunkMsg << "{\"type\":\"relay-chunk\",\"to\":\""
             << JsonEscape(targetPeerId) << "\",\"transferId\":\""
             << JsonEscape(transferId)
             << "\",\"offset\":" << offset << ",\"data\":\"" << b64 << "\"}";
    if (!sendMessage(chunkMsg.str())) {
      std::ostringstream cancelMsg;
      cancelMsg << "{\"type\":\"relay-cancel\",\"to\":\""
                << JsonEscape(targetPeerId) << "\",\"transferId\":\""
                << JsonEscape(transferId) << "\",\"reason\":\"send-failed\"}";
      sendMessage(cancelMsg.str());
      std::lock_guard<std::mutex> lock(m_transfersMutex);
      m_outgoingTransfers.erase(transferId);
      return false;
    }
    offset += chunkSize;

    TransferProgress progressSnapshot;
    bool emitProgress = false;
    {
      std::lock_guard<std::mutex> lock(m_transfersMutex);
      auto it = m_outgoingTransfers.find(transferId);
      if (it != m_outgoingTransfers.end()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.startTime)
                .count();
        it->second.transferredBytes = offset;
        it->second.lastChunkTime = now;
        if (elapsedMs > 0) {
          // BUG FIX (Bug W7): speedBytesPerSecond was stored as float, which
          // overflows silently for files larger than ~16 MB/s sustained.
          // Compute as double and cast only for storage.
          it->second.speedBytesPerSecond =
              static_cast<float>(static_cast<double>(offset) * 1000.0 / elapsedMs);
        }
        progressSnapshot = it->second;
        emitProgress = true;
      }
    }
    if (emitProgress) {
      OnTransferProgressCallback progressCb;
      {
        std::lock_guard<std::mutex> cbLock(m_callbackMutex);
        progressCb = m_onTransferProgress;
      }
      if (progressCb) {
        progressCb(progressSnapshot);
      }
    }

    // Small delay to avoid overwhelming
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Send relay-end
  std::ostringstream endMsg;
  endMsg << "{\"type\":\"relay-end\",\"to\":\""
         << JsonEscape(targetPeerId) << "\",\"transferId\":\""
         << JsonEscape(transferId) << "\"}";
  bool ok = sendMessageWithRetry(endMsg.str());
  RelayVerificationResult verification;
  if (ok) {
    verification = waitForRelayVerification(transferId, 15000);
    ok = verification.received && verification.ok;
  }

  if (!ok && m_onError) {
    std::string reason = verification.received
                             ? verification.reason
                             : "relay verification timeout";
    if (reason.empty()) {
      reason = "relay verification failed";
    }
    m_onError(SignalingError::IntegrityCheckFailed, reason);
  }

  {
    TransferProgress finalProgress;
    bool emitFinalProgress = false;
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    auto it = m_outgoingTransfers.find(transferId);
    if (it != m_outgoingTransfers.end()) {
      it->second.state = ok ? TransferState::Completed : TransferState::Failed;
      if (!ok) {
        it->second.errorMessage = verification.received
                                      ? verification.reason
                                      : "relay verification timeout";
      }
      finalProgress = it->second;
      emitFinalProgress = true;
    }
    m_outgoingTransfers.erase(transferId);

    if (emitFinalProgress) {
      OnTransferProgressCallback progressCb;
      {
        std::lock_guard<std::mutex> cbLock(m_callbackMutex);
        progressCb = m_onTransferProgress;
      }
      if (progressCb) {
        progressCb(finalProgress);
      }
    }
  }
  return ok;
}

bool WebSignalingClient::streamFileViaRelay(const std::string &targetPeerId,
                                            const std::string &filePath) {
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file)
    return false;

  std::streampos endPos = file.tellg();
  if (endPos < 0) {
    return false;
  }
  size_t fileSize = static_cast<size_t>(endPos);
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

  {
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    TransferProgress progress;
    progress.transferId = transferId;
    progress.targetPeerId = targetPeerId;
    progress.filename = filename;
    progress.totalBytes = fileSize;
    progress.transferredBytes = 0;
    progress.state = TransferState::InProgress;
    progress.startTime = std::chrono::steady_clock::now();
    progress.lastChunkTime = progress.startTime;
    m_outgoingTransfers[transferId] = std::move(progress);
  }

  // Send relay-start
  std::ostringstream startMsg;
  startMsg << "{\"type\":\"relay-start\",\"to\":\""
           << JsonEscape(targetPeerId) << "\",\"transferId\":\""
           << JsonEscape(transferId) << "\",\"filename\":\""
           << JsonEscape(filename) << "\",\"size\":" << fileSize
           << ",\"mimeType\":\"application/octet-stream\""
           << ",\"fileIndex\":0,\"totalFiles\":1"
           << ",\"sha256\":\"" << JsonEscape(sha256) << "\"}";
  if (!sendMessageWithRetry(startMsg.str())) {
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    m_outgoingTransfers.erase(transferId);
    return false;
  }

  // Stream chunks from file
  char buffer[SignalingConfig::CHUNK_SIZE];
  size_t offset = 0;
  while (file && offset < fileSize) {
    file.read(buffer, SignalingConfig::CHUNK_SIZE);
    std::streamsize readCount = file.gcount();
    if (readCount <= 0)
      break;

    size_t read = static_cast<size_t>(readCount);

    std::string b64 = base64Encode(reinterpret_cast<uint8_t *>(buffer), read);

    std::ostringstream chunkMsg;
    chunkMsg << "{\"type\":\"relay-chunk\",\"to\":\""
             << JsonEscape(targetPeerId) << "\",\"transferId\":\""
             << JsonEscape(transferId)
             << "\",\"offset\":" << offset << ",\"data\":\"" << b64 << "\"}";
    if (!sendMessage(chunkMsg.str())) {
      std::ostringstream cancelMsg;
      cancelMsg << "{\"type\":\"relay-cancel\",\"to\":\""
                << JsonEscape(targetPeerId) << "\",\"transferId\":\""
                << JsonEscape(transferId) << "\",\"reason\":\"send-failed\"}";
      sendMessage(cancelMsg.str());
      std::lock_guard<std::mutex> lock(m_transfersMutex);
      m_outgoingTransfers.erase(transferId);
      return false;
    }
    offset += read;

    TransferProgress progressSnapshot;
    bool emitProgress = false;
    {
      std::lock_guard<std::mutex> lock(m_transfersMutex);
      auto it = m_outgoingTransfers.find(transferId);
      if (it != m_outgoingTransfers.end()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.startTime)
                .count();
        it->second.transferredBytes = offset;
        it->second.lastChunkTime = now;
        if (elapsedMs > 0) {
          it->second.speedBytesPerSecond = (offset * 1000.0f) / elapsedMs;
        }
        progressSnapshot = it->second;
        emitProgress = true;
      }
    }
    if (emitProgress) {
      OnTransferProgressCallback progressCb;
      {
        std::lock_guard<std::mutex> cbLock(m_callbackMutex);
        progressCb = m_onTransferProgress;
      }
      if (progressCb) {
        progressCb(progressSnapshot);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  if (offset != fileSize || file.bad()) {
    std::ostringstream cancelMsg;
    cancelMsg << "{\"type\":\"relay-cancel\",\"to\":\""
              << JsonEscape(targetPeerId) << "\",\"transferId\":\""
              << JsonEscape(transferId) << "\",\"reason\":\"file-read-failed\"}";
    sendMessage(cancelMsg.str());

    std::lock_guard<std::mutex> lock(m_transfersMutex);
    m_outgoingTransfers.erase(transferId);
    return false;
  }

  std::ostringstream endMsg;
  endMsg << "{\"type\":\"relay-end\",\"to\":\""
         << JsonEscape(targetPeerId) << "\",\"transferId\":\""
         << JsonEscape(transferId) << "\"}";
  bool ok = sendMessageWithRetry(endMsg.str());
  RelayVerificationResult verification;
  if (ok) {
    verification = waitForRelayVerification(transferId, 15000);
    ok = verification.received && verification.ok;
  }

  if (!ok && m_onError) {
    std::string reason = verification.received
                             ? verification.reason
                             : "relay verification timeout";
    if (reason.empty()) {
      reason = "relay verification failed";
    }
    m_onError(SignalingError::IntegrityCheckFailed, reason);
  }

  {
    TransferProgress finalProgress;
    bool emitFinalProgress = false;
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    auto it = m_outgoingTransfers.find(transferId);
    if (it != m_outgoingTransfers.end()) {
      it->second.state = ok ? TransferState::Completed : TransferState::Failed;
      if (!ok) {
        it->second.errorMessage = verification.received
                                      ? verification.reason
                                      : "relay verification timeout";
      }
      finalProgress = it->second;
      emitFinalProgress = true;
    }
    m_outgoingTransfers.erase(transferId);

    if (emitFinalProgress) {
      OnTransferProgressCallback progressCb;
      {
        std::lock_guard<std::mutex> cbLock(m_callbackMutex);
        progressCb = m_onTransferProgress;
      }
      if (progressCb) {
        progressCb(finalProgress);
      }
    }
  }
  return ok;
}

void WebSignalingClient::handleWebRTCControlMessage(const std::string &fromId, const std::string &message) {
  if (message.empty() || message[0] != '{') return;
  std::string type = JsonGetStringField(message, "type");
  if (type == "file-start") {
    std::string transferId = JsonGetStringField(message, "transferId");
    if (transferId.empty()) return;

    // Build a FileInfo vector from the file-start metadata so the UI can show
    // exactly what the browser is trying to send.
    FileInfo fi;
    fi.name     = JsonGetStringField(message, "filename");
    fi.size     = JsonGetSizeField(message, "size", 0);
    fi.mimeType = JsonGetStringField(message, "mimeType");
    fi.sha256   = JsonGetStringField(message, "sha256");
    if (fi.name.empty()) return; // Malformed — reject silently.

    // Validate SHA-256 if provided.
    if (!fi.sha256.empty() && !isValidSha256Hex(fi.sha256)) {
      // SHA-256 field present but invalid — reject and notify sender.
      auto it = m_webrtcClients.find(fromId);
      if (it != m_webrtcClients.end()) {
        it->second->sendString(
            "{\"type\":\"file-cancel\",\"transferId\":\"" +
            JsonEscape(transferId) + "\",\"reason\":\"invalid-sha256\"}");
      }
      if (m_onError)
        m_onError(SignalingError::InvalidMessage,
                  "WebRTC file-start: invalid sha256 digest");
      return;
    }

    // ── User Consent Gate ────────────────────────────────────────────────────
    // Mirror the relay path: call m_onFileRequest so the UI can show an accept/
    // reject dialog. Then wait (on a worker thread) for the decision.
    // The DataChannel callback MUST return immediately, so all blocking happens
    // on the detached thread below.
    OnFileRequestCallback fileReqCb;
    {
      std::lock_guard<std::mutex> cbLock(m_callbackMutex);
      fileReqCb = m_onFileRequest;
    }

    // Capture everything the worker thread needs by value.
    std::string capturedFromId   = fromId;
    std::string capturedTfId     = transferId;
    std::string capturedFilename = fi.name;
    size_t      capturedSize     = fi.size;
    std::string capturedMime     = fi.mimeType;
    std::string capturedSha256   = fi.sha256;
    int capturedFileIndex  = static_cast<int>(JsonGetSizeField(message, "fileIndex", 0));
    int capturedTotalFiles = static_cast<int>(JsonGetSizeField(message, "totalFiles", 1));

    // Notify the UI. m_pendingFileResponses[transferId] will be set to
    // true (accept) or false (reject) when AcceptPendingRequest / Reject is called.
    if (fileReqCb) {
      std::string fromName;
      {
        std::lock_guard<std::mutex> pLock(m_peersMutex);
        for (const auto& p : m_peers) {
          if (p.id == fromId) { fromName = p.name; break; }
        }
        if (fromName.empty()) fromName = fromId;
      }
      std::lock_guard<std::mutex> cbLock(m_callbackMutex);
      fileReqCb(fromId, fromName, {fi});
    } else {
      // No UI callback registered — auto-accept (desktop in headless/kiosk mode).
      std::lock_guard<std::mutex> lock(m_fileResponseMutex);
      m_pendingFileResponses[capturedTfId] = true;
      m_fileResponseCv.notify_all();
    }

    // Worker thread: waits for user decision, then either creates the
    // RelayTransfer + sends resume-ready, or sends file-cancel.
    std::thread([this,
                 capturedFromId, capturedTfId,
                 capturedFilename, capturedSize, capturedMime, capturedSha256,
                 capturedFileIndex, capturedTotalFiles]() mutable {

      bool accepted = false;
      {
        std::unique_lock<std::mutex> lock(m_fileResponseMutex);
        // 30-second window for the user to respond.
        auto didRespond = m_fileResponseCv.wait_for(lock, std::chrono::seconds(30),
            [&]() {
              return m_pendingFileResponses.find(capturedTfId) !=
                     m_pendingFileResponses.end();
            });
        if (didRespond) {
          accepted = m_pendingFileResponses[capturedTfId];
          m_pendingFileResponses.erase(capturedTfId);
        }
      }

      auto clientIt = m_webrtcClients.find(capturedFromId);
      if (clientIt == m_webrtcClients.end()) return; // Peer already gone.

      if (!accepted) {
        // User rejected (or timed out) — tell the browser to stop.
        clientIt->second->sendString(
            "{\"type\":\"file-cancel\",\"transferId\":\"" +
            JsonEscape(capturedTfId) + "\",\"reason\":\"rejected\"}");
        return;
      }

      // ── Accepted — create the RelayTransfer and reply with resume-ready ──
      {
        std::lock_guard<std::mutex> lock(m_transfersMutex);
        m_activeWebRTCTransfers[capturedFromId] = capturedTfId;

        RelayTransfer transfer;
        transfer.transferId    = capturedTfId;
        transfer.fromPeerId    = capturedFromId;
        transfer.filename      = capturedFilename;
        transfer.totalSize     = capturedSize;
        transfer.fileIndex     = capturedFileIndex;
        transfer.totalFiles    = capturedTotalFiles;
        transfer.sha256Expected = capturedSha256;
        transfer.state         = TransferState::InProgress;
        transfer.lastActivity  = std::chrono::steady_clock::now();

        if (!m_downloadPath.empty() &&
            transfer.totalSize > SignalingConfig::STREAM_THRESHOLD) {
          transfer.streaming    = true;
          transfer.tempFilePath = m_downloadPath + "/.__tmp_" + capturedTfId;
          transfer.tempFileHandle = std::make_shared<std::ofstream>(
              transfer.tempFilePath, std::ios::binary | std::ios::trunc);
          if (!transfer.tempFileHandle->is_open()) {
            transfer.streaming = false;
            transfer.tempFileHandle.reset();
            if (transfer.totalSize > 0)
              transfer.data.reserve(transfer.totalSize);
          }
        } else {
          if (transfer.totalSize > 0) transfer.data.reserve(transfer.totalSize);
        }
        m_incomingTransfers[capturedTfId] = transfer;
      }

      std::ostringstream responseMsg;
      responseMsg << "{\"type\":\"resume-ready\",\"transferId\":\""
                  << JsonEscape(capturedTfId)
                  << "\",\"resumeOffset\":0,\"resumeCapable\":false}";
      clientIt->second->sendString(responseMsg.str());
    }).detach();

  } else if (type == "file-end") {
    std::string transferId = JsonGetStringField(message, "transferId");
    if (transferId.empty()) return;
    RelayTransfer completedTransfer;
    bool found = false;
    {
      std::lock_guard<std::mutex> lock(m_transfersMutex);
      auto it = m_incomingTransfers.find(transferId);
      if (it != m_incomingTransfers.end()) {
        completedTransfer = it->second;
        found = true;
        m_incomingTransfers.erase(it);
        m_activeWebRTCTransfers.erase(fromId);
      }
    }
    if (!found) return;

    if (completedTransfer.streaming && completedTransfer.tempFileHandle) {
      // ── Streaming path ──────────────────────────────────────────────────
      // Close the temp file first (must happen before any reads of it).
      completedTransfer.tempFileHandle->close();
      completedTransfer.tempFileHandle.reset();

      // Bug 5: Partial-transfer guard.
      // If the received byte count doesn't match what the sender declared,
      // the temp file is corrupt — reject without running SHA-256 on garbage.
      if (completedTransfer.totalSize > 0 &&
          completedTransfer.receivedBytes != completedTransfer.totalSize) {
        std::remove(completedTransfer.tempFilePath.c_str());
        auto errCb = m_onError;
        if (errCb) {
          errCb(SignalingError::TransferFailed,
                "WebRTC incomplete transfer: expected " +
                std::to_string(completedTransfer.totalSize) + " bytes, got " +
                std::to_string(completedTransfer.receivedBytes));
        }
        return;
      }

      // Bug 4: Move SHA-256 + rename off the signaling thread into a worker.
      // The message loop must never block on multi-second file I/O.
      std::thread([this, completedTransfer]() mutable {
        // ── SHA-256 verification ─────────────────────────────────────────
        bool ok = true;
        if (!completedTransfer.sha256Expected.empty()) {
          std::string actualHash = computeSHA256(completedTransfer.tempFilePath);
          ok = (actualHash == completedTransfer.sha256Expected);
        }

        if (!ok) {
          std::remove(completedTransfer.tempFilePath.c_str());
          auto errCb = m_onError;
          if (errCb) errCb(SignalingError::IntegrityCheckFailed,
                           "WebRTC SHA-256 mismatch (streaming)");
          return;
        }

        // ── Bug 3: Rename temp file to final path ────────────────────────
        // Sanitize filename to prevent path traversal.
        std::string safeName = completedTransfer.filename;
        for (char& c : safeName) {
          if (c == '/' || c == '\\') c = '_';
        }
        if (safeName.empty() || safeName == "." || safeName == "..") {
          safeName = "received_file";
        }

        // Collision-safe final path: append _2, _3, ... if target exists.
        std::string basePath = m_downloadPath + "/" + safeName;
        std::string finalPath = basePath;
        {
          int suffix = 1;
          while (std::ifstream(finalPath).good()) {
            auto dot = safeName.rfind('.');
            std::string stem = (dot != std::string::npos)
                                   ? safeName.substr(0, dot)
                                   : safeName;
            std::string ext =
                (dot != std::string::npos) ? safeName.substr(dot) : "";
            finalPath = m_downloadPath + "/" + stem + "_" +
                        std::to_string(++suffix) + ext;
          }
        }

        // std::rename is atomic on same filesystem; falls back to copy+delete
        // on cross-device moves (e.g., tmp on tmpfs, download on ext4).
        bool renamed = (std::rename(completedTransfer.tempFilePath.c_str(),
                                    finalPath.c_str()) == 0);
        if (!renamed) {
          // Cross-filesystem fallback: binary copy then delete source.
          std::ifstream src(completedTransfer.tempFilePath,
                            std::ios::binary);
          std::ofstream dst(finalPath, std::ios::binary | std::ios::trunc);
          if (src && dst) {
            dst << src.rdbuf();
            src.close();
            dst.close();
            if (!dst.fail()) {
              std::remove(completedTransfer.tempFilePath.c_str());
              renamed = true;
            }
          }
        }

        if (!renamed) {
          std::remove(completedTransfer.tempFilePath.c_str());
          auto errCb = m_onError;
          if (errCb) errCb(SignalingError::TransferFailed,
                           "WebRTC: failed to move temp file to: " + finalPath);
          return;
        }

        // Deliver the final path as the "filename" so TeleportBridge can
        // display it and update the transfer state correctly.
        auto completeCb = m_onTransferComplete;
        if (completeCb) {
          completeCb(completedTransfer.transferId, finalPath, {}, true);
        }
      }).detach();

    } else {
      // ── In-memory path (small files < STREAM_THRESHOLD) ─────────────────
      // Bug 5: Sanity check for in-memory path too.
      if (completedTransfer.totalSize > 0 &&
          completedTransfer.data.size() != completedTransfer.totalSize) {
        auto errCb = m_onError;
        if (errCb) {
          errCb(SignalingError::TransferFailed,
                "WebRTC incomplete in-memory transfer: expected " +
                std::to_string(completedTransfer.totalSize) + " bytes, got " +
                std::to_string(completedTransfer.data.size()));
        }
        return;
      }

      std::string actualHash = computeSHA256(completedTransfer.data);
      bool ok = completedTransfer.sha256Expected.empty() ||
                actualHash == completedTransfer.sha256Expected;
      if (!ok) {
        auto errCb = m_onError;
        if (errCb) errCb(SignalingError::IntegrityCheckFailed,
                         "WebRTC SHA-256 mismatch (in-memory)");
        return;
      }
      auto completeCb = m_onTransferComplete;
      if (completeCb) {
        completeCb(completedTransfer.transferId, completedTransfer.filename,
                   completedTransfer.data, true);
      }
    }
  } else if (type == "resume-ready") {
    std::string transferId = JsonGetStringField(message, "transferId");
    {
      std::lock_guard<std::mutex> lock(m_fileResponseMutex);
      m_pendingFileResponses[transferId] = true;
    }
    m_fileResponseCv.notify_all();
  }
}

void WebSignalingClient::handleWebRTCChunk(const std::string &fromId, const uint8_t *data, size_t size) {
  std::string transferId;
  {
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    auto it = m_activeWebRTCTransfers.find(fromId);
    if (it != m_activeWebRTCTransfers.end()) transferId = it->second;
  }
  if (transferId.empty()) return;
  bool emitProgress = false;
  TransferProgress progressSnapshot;
  {
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    auto it = m_incomingTransfers.find(transferId);
    if (it != m_incomingTransfers.end()) {
      if (it->second.streaming && it->second.tempFileHandle && it->second.tempFileHandle->is_open()) {
        it->second.tempFileHandle->write(reinterpret_cast<const char*>(data), size);
      } else {
        it->second.data.insert(it->second.data.end(), data, data + size);
      }
      it->second.receivedBytes += size;
      auto now = std::chrono::steady_clock::now();
      auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.lastActivity).count();
      if (elapsedMs > 100 || it->second.receivedBytes >= it->second.totalSize) {
        it->second.lastActivity = now;
        
        progressSnapshot.transferId = it->second.transferId;
        progressSnapshot.filename = it->second.filename;
        progressSnapshot.totalBytes = it->second.totalSize;
        progressSnapshot.transferredBytes = it->second.receivedBytes;
        progressSnapshot.state = it->second.state;
        
        emitProgress = true;
      }
    }
  }
  if (emitProgress && m_onTransferProgress) {
    m_onTransferProgress(progressSnapshot);
  }
}

bool WebSignalingClient::streamFileViaWebRTC(const std::string &targetPeerId, const std::string &filePath) {
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file) return false;
  std::streampos endPos = file.tellg();
  if (endPos < 0) return false;
  size_t fileSize = static_cast<size_t>(endPos);
  file.seekg(0);

  std::string filename = filePath;
  size_t lastSlash = filePath.find_last_of("/\\");
  if (lastSlash != std::string::npos) filename = filePath.substr(lastSlash + 1);

  std::string transferId = generateUUID();
  std::string sha256 = computeSHA256(filePath);

  {
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    TransferProgress progress;
    progress.transferId = transferId;
    progress.targetPeerId = targetPeerId;
    progress.filename = filename;
    progress.totalBytes = fileSize;
    progress.transferredBytes = 0;
    progress.state = TransferState::InProgress;
    progress.startTime = std::chrono::steady_clock::now();
    progress.lastChunkTime = progress.startTime;
    m_outgoingTransfers[transferId] = std::move(progress);
  }

  std::ostringstream startMsg;
  startMsg << "{\"type\":\"file-start\",\"transferId\":\"" << JsonEscape(transferId) << "\",\"filename\":\"" << JsonEscape(filename) << "\",\"size\":" << fileSize << ",\"mimeType\":\"application/octet-stream\",\"fileIndex\":0,\"totalFiles\":1,\"sha256\":\"" << JsonEscape(sha256) << "\"}";

  auto it = m_webrtcClients.find(targetPeerId);
  if (it == m_webrtcClients.end()) return false;
  it->second->sendString(startMsg.str());

  // Wait for resume-ready
  bool accepted = false;
  {
    std::unique_lock<std::mutex> lock(m_fileResponseMutex);
    auto status = m_fileResponseCv.wait_for(lock, std::chrono::seconds(15), [&]() {
      return m_pendingFileResponses.find(transferId) != m_pendingFileResponses.end();
    });
    if (status) {
      accepted = m_pendingFileResponses[transferId];
      m_pendingFileResponses.erase(transferId);
    }
  }
  if (!accepted) {
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    m_outgoingTransfers.erase(transferId);
    return false;
  }

  // Stream via DataChannel
  char buffer[SignalingConfig::CHUNK_SIZE];
  size_t offset = 0;
  while (file && offset < fileSize) {
    file.read(buffer, SignalingConfig::CHUNK_SIZE);
    std::streamsize readCount = file.gcount();
    if (readCount <= 0) break;
    size_t read = static_cast<size_t>(readCount);

    if (!it->second->sendData(reinterpret_cast<const uint8_t*>(buffer), read)) {
      std::lock_guard<std::mutex> lock(m_transfersMutex);
      m_outgoingTransfers.erase(transferId);
      return false;
    }
    offset += read;

    bool emitProgress = false;
    TransferProgress progressSnapshot;
    {
      std::lock_guard<std::mutex> lock(m_transfersMutex);
      auto tIt = m_outgoingTransfers.find(transferId);
      if (tIt != m_outgoingTransfers.end()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - tIt->second.startTime).count();
        tIt->second.transferredBytes = offset;
        tIt->second.lastChunkTime = now;
        if (elapsedMs > 0) tIt->second.speedBytesPerSecond = (offset * 1000.0f) / elapsedMs;
        progressSnapshot = tIt->second;
        emitProgress = true;
      }
    }
    if (emitProgress && m_onTransferProgress) {
        m_onTransferProgress(progressSnapshot);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2)); // Flow control
  }

  std::ostringstream endMsg;
  endMsg << "{\"type\":\"file-end\",\"transferId\":\"" << JsonEscape(transferId) << "\"}";
  it->second->sendString(endMsg.str());

  {
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    auto tIt = m_outgoingTransfers.find(transferId);
    if (tIt != m_outgoingTransfers.end()) {
      tIt->second.state = TransferState::Completed;
    }
    m_outgoingTransfers.erase(transferId);
  }
  return true;
}

bool WebSignalingClient::streamFileOnline(const std::string &targetPeerId,
                                          const std::string &filePath) {
  // If we have an active WebRTC client, use it automatically!
  auto it = m_webrtcClients.find(targetPeerId);
  if (it != m_webrtcClients.end()) {
    return streamFileViaWebRTC(targetPeerId, filePath);
  }
  // Otherwise fall through to the server relay path
  return streamFileViaRelay(targetPeerId, filePath);
}


bool WebSignalingClient::sendOffer(const std::string &targetPeerId,
                                   const std::string &sdp) {
  if (!isConnected() || targetPeerId.empty() || sdp.empty()) {
    return false;
  }

  std::ostringstream msg;
  msg << "{\"type\":\"offer\",\"to\":\"" << JsonEscape(targetPeerId)
      << "\",\"from\":\"" << JsonEscape(m_peerId)
      << "\",\"sdp\":\"" << JsonEscape(sdp) << "\"}";
  return sendMessage(msg.str());
}

bool WebSignalingClient::sendAnswer(const std::string &targetPeerId,
                                    const std::string &sdp) {
  if (!isConnected() || targetPeerId.empty() || sdp.empty()) {
    return false;
  }

  std::ostringstream msg;
  msg << "{\"type\":\"answer\",\"to\":\"" << JsonEscape(targetPeerId)
      << "\",\"from\":\"" << JsonEscape(m_peerId)
      << "\",\"sdp\":\"" << JsonEscape(sdp) << "\"}";
  return sendMessage(msg.str());
}

bool WebSignalingClient::sendIceCandidate(const std::string &targetPeerId,
                                          const std::string &candidate) {
  if (!isConnected() || targetPeerId.empty() || candidate.empty()) {
    return false;
  }

  std::ostringstream msg;
  msg << "{\"type\":\"ice\",\"to\":\"" << JsonEscape(targetPeerId)
      << "\",\"from\":\"" << JsonEscape(m_peerId)
      << "\",\"candidate\":\"" << JsonEscape(candidate) << "\"}";
  return sendMessage(msg.str());
}

void WebSignalingClient::cancelTransfer(const std::string &transferId) {
  std::string targetPeerId;
  {
    std::lock_guard<std::mutex> lock(m_transfersMutex);
    auto it = m_outgoingTransfers.find(transferId);
    if (it != m_outgoingTransfers.end()) {
      targetPeerId = it->second.targetPeerId;
    }
    m_incomingTransfers.erase(transferId);
    m_outgoingTransfers.erase(transferId);
  }

  {
    std::lock_guard<std::mutex> verificationLock(m_verificationMutex);
    m_relayVerifications.erase(transferId);
  }

  if (!targetPeerId.empty() && isConnected()) {
    std::ostringstream cancelMsg;
    cancelMsg << "{\"type\":\"relay-cancel\",\"to\":\""
              << JsonEscape(targetPeerId) << "\",\"transferId\":\""
              << JsonEscape(transferId) << "\",\"reason\":\"manual-cancel\"}";
    sendMessage(cancelMsg.str());
  }
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

void WebSignalingClient::setDownloadPath(const std::string &path) {
  m_downloadPath = path;
  // Initialise the resume manager whenever the download path is (re)configured.
  // State dir is hidden inside the download folder so it follows the same
  // filesystem permissions as the received files themselves.
  if (!path.empty()) {
    m_resumeManager = std::make_unique<RelayResumeManager>(path + "/.relay_state");
  } else {
    m_resumeManager.reset();
  }
}

std::string WebSignalingClient::getDownloadPath() const {
  return m_downloadPath;
}

// ─────────────────────────────────────────────────────────────────────────────
// Relay Resume Helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Send a relay-resume-request to the remote browser peer.
 * The browser will seek its File reader to @p resumeOffset and re-enter
 * the sendFileViaRelay pipeline from that byte position.
 */
void WebSignalingClient::sendRelayResumeRequest(const std::string &toPeerId,
                                                const std::string &transferId,
                                                size_t resumeOffset) {
  if (toPeerId.empty() || transferId.empty()) return;

  std::ostringstream msg;
  msg << "{\"type\":\"relay-resume-request\",\"to\":\""
      << JsonEscape(toPeerId) << "\",\"transferId\":\""
      << JsonEscape(transferId) << "\",\"resumeOffset\":"
      << resumeOffset << "}";
  sendMessage(msg.str());
}

/**
 * Called once after every successful WebSocket (re)connect.
 * Scans the resume-state directory for interrupted transfers whose temp
 * files still exist, then sends a relay-reconnect-hint to the originating
 * peer so it knows the desktop is ready to accept a resume relay-start.
 *
 * The browser MUST still re-initiate with relay-start (containing the same
 * transferId).  This hint is advisory \u2014 if the browser has already moved on
 * or the peer is no longer in the room the hint is silently dropped by the
 * signaling server.
 */
void WebSignalingClient::announceReconnectHints() {
  if (!m_resumeManager) return;

  const auto pending = m_resumeManager->listPending();
  for (const auto &state : pending) {
    if (state.fromPeerId.empty() || state.transferId.empty()) continue;

    std::ostringstream msg;
    msg << "{\"type\":\"relay-reconnect-hint\",\"to\":\""
        << JsonEscape(state.fromPeerId) << "\",\"transferId\":\""
        << JsonEscape(state.transferId) << "\",\"resumeOffset\":"
        << state.receivedBytes << "}";
    sendMessage(msg.str());
  }
}

} // namespace teleport
