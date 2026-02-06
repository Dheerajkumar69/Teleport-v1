/**
 * @file WebSignalingClient.cpp
 * @brief WebSocket signaling client implementation
 *
 * Simple WebSocket implementation using POSIX sockets.
 * Connects to signaling server for web ↔ desktop file transfers.
 */

#include "WebSignalingClient.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define SOCKET_ERROR_VAL SOCKET_ERROR
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define SOCKET_ERROR_VAL -1
#define INVALID_SOCKET -1
#define CLOSE_SOCKET close
#endif

// Simple JSON helpers (avoid external dependency)
namespace json {
std::string escape(const std::string &s) {
  std::string out;
  for (char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
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
      out += c;
    }
  }
  return out;
}

std::string get(const std::string &json, const std::string &key) {
  std::string search = "\"" + key + "\":";
  size_t pos = json.find(search);
  if (pos == std::string::npos)
    return "";
  pos += search.length();
  while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
    pos++;
  if (pos >= json.length())
    return "";

  if (json[pos] == '"') {
    // String value
    pos++;
    std::string val;
    while (pos < json.length() && json[pos] != '"') {
      if (json[pos] == '\\' && pos + 1 < json.length()) {
        pos++;
        switch (json[pos]) {
        case 'n':
          val += '\n';
          break;
        case 'r':
          val += '\r';
          break;
        case 't':
          val += '\t';
          break;
        default:
          val += json[pos];
        }
      } else {
        val += json[pos];
      }
      pos++;
    }
    return val;
  } else if (json[pos] == '[') {
    // Array - return raw
    int depth = 1;
    size_t start = pos;
    pos++;
    while (pos < json.length() && depth > 0) {
      if (json[pos] == '[')
        depth++;
      else if (json[pos] == ']')
        depth--;
      pos++;
    }
    return json.substr(start, pos - start);
  } else if (json[pos] == '{') {
    // Object - return raw
    int depth = 1;
    size_t start = pos;
    pos++;
    while (pos < json.length() && depth > 0) {
      if (json[pos] == '{')
        depth++;
      else if (json[pos] == '}')
        depth--;
      pos++;
    }
    return json.substr(start, pos - start);
  } else {
    // Number/bool
    std::string val;
    while (pos < json.length() && json[pos] != ',' && json[pos] != '}' &&
           json[pos] != ']') {
      val += json[pos++];
    }
    // Trim whitespace
    while (!val.empty() &&
           (val.back() == ' ' || val.back() == '\t' || val.back() == '\n')) {
      val.pop_back();
    }
    return val;
  }
}

bool getBool(const std::string &json, const std::string &key) {
  return get(json, key) == "true";
}
} // namespace json

// Base64 encoding/decoding for relay chunks
namespace base64 {
static const char *chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = (uint32_t)data[i] << 16;
    if (i + 1 < len)
      n |= (uint32_t)data[i + 1] << 8;
    if (i + 2 < len)
      n |= (uint32_t)data[i + 2];
    out.push_back(chars[(n >> 18) & 0x3F]);
    out.push_back(chars[(n >> 12) & 0x3F]);
    out.push_back(i + 1 < len ? chars[(n >> 6) & 0x3F] : '=');
    out.push_back(i + 2 < len ? chars[n & 0x3F] : '=');
  }
  return out;
}

std::vector<uint8_t> decode(const std::string &s) {
  std::vector<uint8_t> out;
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++)
    T[(uint8_t)chars[i]] = i;

  int val = 0, bits = -8;
  for (uint8_t c : s) {
    if (T[c] == -1)
      continue;
    val = (val << 6) + T[c];
    bits += 6;
    if (bits >= 0) {
      out.push_back((val >> bits) & 0xFF);
      bits -= 8;
    }
  }
  return out;
}
} // namespace base64

namespace teleport {

// Generate random WebSocket key
static std::string generateWebSocketKey() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 255);
  uint8_t bytes[16];
  for (int i = 0; i < 16; i++)
    bytes[i] = (uint8_t)dis(gen);
  return base64::encode(bytes, 16);
}

WebSignalingClient::WebSignalingClient() {
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

WebSignalingClient::~WebSignalingClient() {
  disconnect();
#ifdef _WIN32
  WSACleanup();
#endif
}

bool WebSignalingClient::connect(const std::string &serverUrl,
                                 const std::string &deviceName) {
  if (m_connected) {
    disconnect();
  }

  m_deviceName = deviceName;
  m_serverUrl = serverUrl;

  // Parse URL (ws://host:port or wss://host:port)
  std::string host;
  int port = 80;
  bool useSSL = false;

  std::string url = serverUrl;
  if (url.substr(0, 6) == "wss://") {
    useSSL = true;
    port = 443;
    url = url.substr(6);
  } else if (url.substr(0, 5) == "ws://") {
    url = url.substr(5);
  }

  size_t colonPos = url.find(':');
  size_t slashPos = url.find('/');
  if (colonPos != std::string::npos &&
      (slashPos == std::string::npos || colonPos < slashPos)) {
    host = url.substr(0, colonPos);
    size_t portEnd = slashPos != std::string::npos ? slashPos : url.length();
    port = std::stoi(url.substr(colonPos + 1, portEnd - colonPos - 1));
  } else if (slashPos != std::string::npos) {
    host = url.substr(0, slashPos);
  } else {
    host = url;
  }

  std::cout << "[WebSignaling] Connecting to " << host << ":" << port
            << std::endl;

  // Resolve host
  struct addrinfo hints{}, *result;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                  &result) != 0) {
    std::cerr << "[WebSignaling] Failed to resolve host: " << host << std::endl;
    return false;
  }

  // Create socket
  socket_t sock =
      socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (sock == INVALID_SOCKET) {
    std::cerr << "[WebSignaling] Failed to create socket" << std::endl;
    freeaddrinfo(result);
    return false;
  }

  // Connect
  if (::connect(sock, result->ai_addr, (int)result->ai_addrlen) ==
      SOCKET_ERROR_VAL) {
    std::cerr << "[WebSignaling] Failed to connect" << std::endl;
    CLOSE_SOCKET(sock);
    freeaddrinfo(result);
    return false;
  }
  freeaddrinfo(result);

  // NOTE: For WSS (TLS), you would need OpenSSL here
  // For now, we only support WS (non-TLS) connections
  if (useSSL) {
    std::cerr << "[WebSignaling] WSS (TLS) not supported yet, use WS"
              << std::endl;
    CLOSE_SOCKET(sock);
    return false;
  }

  // WebSocket handshake
  std::string wsKey = generateWebSocketKey();
  std::ostringstream request;
  request << "GET / HTTP/1.1\r\n"
          << "Host: " << host << ":" << port << "\r\n"
          << "Upgrade: websocket\r\n"
          << "Connection: Upgrade\r\n"
          << "Sec-WebSocket-Key: " << wsKey << "\r\n"
          << "Sec-WebSocket-Version: 13\r\n"
          << "\r\n";

  std::string reqStr = request.str();
  if (send(sock, reqStr.c_str(), (int)reqStr.length(), 0) == SOCKET_ERROR_VAL) {
    std::cerr << "[WebSignaling] Failed to send handshake" << std::endl;
    CLOSE_SOCKET(sock);
    return false;
  }

  // Read response
  char buffer[1024];
  int received = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (received <= 0) {
    std::cerr << "[WebSignaling] Failed to receive handshake response"
              << std::endl;
    CLOSE_SOCKET(sock);
    return false;
  }
  buffer[received] = '\0';

  // Check for 101 Switching Protocols
  if (std::string(buffer).find("101") == std::string::npos) {
    std::cerr << "[WebSignaling] Invalid handshake response" << std::endl;
    CLOSE_SOCKET(sock);
    return false;
  }

  std::cout << "[WebSignaling] WebSocket connected!" << std::endl;

  m_wsHandle = reinterpret_cast<void *>(static_cast<intptr_t>(sock));
  m_connected = true;
  m_running = true;

  // Start message processing thread
  m_thread =
      std::make_unique<std::thread>(&WebSignalingClient::processMessages, this);

  if (m_onConnected) {
    m_onConnected();
  }

  return true;
}

void WebSignalingClient::disconnect() {
  m_running = false;
  m_connected = false;

  if (m_wsHandle) {
    socket_t sock =
        static_cast<socket_t>(reinterpret_cast<intptr_t>(m_wsHandle));
    CLOSE_SOCKET(sock);
    m_wsHandle = nullptr;
  }

  if (m_thread && m_thread->joinable()) {
    m_thread->join();
  }
  m_thread.reset();

  std::lock_guard<std::mutex> lock(m_peersMutex);
  m_peers.clear();
}

std::vector<WebPeer> WebSignalingClient::getPeers() const {
  std::lock_guard<std::mutex> lock(m_peersMutex);
  return m_peers;
}

void WebSignalingClient::sendMessage(const std::string &message) {
  if (!m_connected || !m_wsHandle)
    return;

  socket_t sock = static_cast<socket_t>(reinterpret_cast<intptr_t>(m_wsHandle));

  // Build WebSocket frame
  std::vector<uint8_t> frame;
  frame.push_back(0x81); // Text frame, FIN bit set

  size_t len = message.length();
  if (len < 126) {
    frame.push_back(0x80 | (uint8_t)len); // Mask bit + length
  } else if (len < 65536) {
    frame.push_back(0x80 | 126);
    frame.push_back((len >> 8) & 0xFF);
    frame.push_back(len & 0xFF);
  } else {
    frame.push_back(0x80 | 127);
    for (int i = 7; i >= 0; i--) {
      frame.push_back((len >> (i * 8)) & 0xFF);
    }
  }

  // Masking key
  uint8_t mask[4];
  std::random_device rd;
  for (int i = 0; i < 4; i++)
    mask[i] = rd() & 0xFF;
  frame.insert(frame.end(), mask, mask + 4);

  // Masked payload
  for (size_t i = 0; i < len; i++) {
    frame.push_back(message[i] ^ mask[i % 4]);
  }

  send(sock, reinterpret_cast<const char *>(frame.data()), (int)frame.size(),
       0);
}

void WebSignalingClient::processMessages() {
  socket_t sock = static_cast<socket_t>(reinterpret_cast<intptr_t>(m_wsHandle));

  // Set socket timeout for polling
#ifdef _WIN32
  DWORD timeout = 100;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
#else
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

  std::vector<uint8_t> buffer(65536);
  std::string accumulated;

  while (m_running) {
    // Send queued messages
    {
      std::lock_guard<std::mutex> lock(m_sendMutex);
      while (!m_sendQueue.empty()) {
        sendMessage(m_sendQueue.front());
        m_sendQueue.pop();
      }
    }

    // Receive
    int received = recv(sock, reinterpret_cast<char *>(buffer.data()),
                        (int)buffer.size(), 0);
    if (received <= 0) {
#ifdef _WIN32
      if (WSAGetLastError() == WSAETIMEDOUT)
        continue;
#else
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        continue;
#endif
      // Connection closed
      m_connected = false;
      if (m_onDisconnected) {
        m_onDisconnected("Connection lost");
      }
      break;
    }

    // Parse WebSocket frame(s)
    size_t pos = 0;
    while (pos < (size_t)received) {
      uint8_t opcode = buffer[pos] & 0x0F;
      bool fin = (buffer[pos] & 0x80) != 0;
      pos++;

      if (pos >= (size_t)received)
        break;

      bool masked = (buffer[pos] & 0x80) != 0;
      size_t payloadLen = buffer[pos] & 0x7F;
      pos++;

      if (payloadLen == 126) {
        if (pos + 2 > (size_t)received)
          break;
        payloadLen = ((size_t)buffer[pos] << 8) | buffer[pos + 1];
        pos += 2;
      } else if (payloadLen == 127) {
        if (pos + 8 > (size_t)received)
          break;
        payloadLen = 0;
        for (int i = 0; i < 8; i++) {
          payloadLen = (payloadLen << 8) | buffer[pos + i];
        }
        pos += 8;
      }

      uint8_t mask[4] = {0};
      if (masked) {
        if (pos + 4 > (size_t)received)
          break;
        memcpy(mask, &buffer[pos], 4);
        pos += 4;
      }

      if (pos + payloadLen > (size_t)received)
        break;

      std::string payload;
      for (size_t i = 0; i < payloadLen; i++) {
        payload += (char)(buffer[pos + i] ^ mask[i % 4]);
      }
      pos += payloadLen;

      if (opcode == 0x08) {
        // Close frame
        m_connected = false;
        if (m_onDisconnected) {
          m_onDisconnected("Server closed connection");
        }
        return;
      } else if (opcode == 0x09) {
        // Ping - send pong
        // (simplified)
      } else if (opcode == 0x01 || opcode == 0x00) {
        // Text or continuation
        accumulated += payload;
        if (fin) {
          handleMessage(accumulated);
          accumulated.clear();
        }
      }
    }
  }
}

void WebSignalingClient::handleMessage(const std::string &message) {
  std::string type = json::get(message, "type");

  if (type == "welcome") {
    m_peerId = json::get(message, "peerId");
    std::cout << "[WebSignaling] Our peer ID: " << m_peerId << std::endl;

    // Join room
    std::ostringstream joinMsg;
    joinMsg << "{\"type\":\"join\",\"room\":\"" << json::escape(m_room)
            << "\",\"name\":\"" << json::escape(m_deviceName) << "\"}";
    sendMessage(joinMsg.str());

  } else if (type == "peers") {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    m_peers.clear();

    std::string peersArr = json::get(message, "peers");
    // Simple array parsing
    size_t pos = 0;
    while ((pos = peersArr.find('{', pos)) != std::string::npos) {
      size_t end = peersArr.find('}', pos);
      if (end == std::string::npos)
        break;
      std::string obj = peersArr.substr(pos, end - pos + 1);

      WebPeer peer;
      peer.id = json::get(obj, "id");
      peer.name = json::get(obj, "name");
      if (!peer.id.empty()) {
        m_peers.push_back(peer);
      }
      pos = end + 1;
    }

    std::cout << "[WebSignaling] Got " << m_peers.size() << " peers"
              << std::endl;

    if (m_onPeersUpdated) {
      m_onPeersUpdated(m_peers);
    }

  } else if (type == "peer-joined") {
    std::string peerStr = json::get(message, "peer");
    WebPeer peer;
    peer.id = json::get(peerStr, "id");
    peer.name = json::get(peerStr, "name");

    if (!peer.id.empty()) {
      std::lock_guard<std::mutex> lock(m_peersMutex);
      m_peers.push_back(peer);
      std::cout << "[WebSignaling] Peer joined: " << peer.name << std::endl;

      if (m_onPeersUpdated) {
        m_onPeersUpdated(m_peers);
      }
    }

  } else if (type == "peer-left") {
    std::string leftId = json::get(message, "peerId");
    std::lock_guard<std::mutex> lock(m_peersMutex);
    m_peers.erase(
        std::remove_if(m_peers.begin(), m_peers.end(),
                       [&leftId](const WebPeer &p) { return p.id == leftId; }),
        m_peers.end());

    if (m_onPeersUpdated) {
      m_onPeersUpdated(m_peers);
    }

  } else if (type == "file-request") {
    std::string fromId = json::get(message, "from");
    std::string fromName = json::get(message, "fromName");
    std::string filesArr = json::get(message, "files");

    std::vector<FileInfo> files;
    size_t pos = 0;
    while ((pos = filesArr.find('{', pos)) != std::string::npos) {
      size_t end = filesArr.find('}', pos);
      if (end == std::string::npos)
        break;
      std::string obj = filesArr.substr(pos, end - pos + 1);

      FileInfo file;
      file.name = json::get(obj, "name");
      file.size = std::stoull(json::get(obj, "size"));
      file.mimeType = json::get(obj, "type");
      files.push_back(file);
      pos = end + 1;
    }

    std::cout << "[WebSignaling] File request from " << fromName << ": "
              << files.size() << " files" << std::endl;

    if (m_onFileRequest) {
      m_onFileRequest(fromId, fromName, files);
    }

  } else if (type == "file-response") {
    std::string from = json::get(message, "from");
    bool accepted = json::getBool(message, "accepted");
    std::cout << "[WebSignaling] File response from " << from << ": "
              << (accepted ? "accepted" : "rejected") << std::endl;
    // Handle in UI layer

  } else if (type == "relay-start") {
    std::string transferId = json::get(message, "transferId");
    std::string fromId = json::get(message, "from");
    std::string filename = json::get(message, "filename");
    size_t size = std::stoull(json::get(message, "size"));

    std::cout << "[WebSignaling] Relay transfer starting: " << filename << " ("
              << size << " bytes)" << std::endl;

    std::lock_guard<std::mutex> lock(m_transfersMutex);
    RelayTransfer transfer;
    transfer.transferId = transferId;
    transfer.fromPeerId = fromId;
    transfer.filename = filename;
    transfer.totalSize = size;
    transfer.receivedBytes = 0;
    transfer.data.reserve(size);
    m_incomingTransfers[transferId] = std::move(transfer);

  } else if (type == "relay-chunk") {
    std::string transferId = json::get(message, "transferId");
    std::string dataB64 = json::get(message, "data");

    std::vector<uint8_t> chunk = base64::decode(dataB64);

    std::lock_guard<std::mutex> lock(m_transfersMutex);
    auto it = m_incomingTransfers.find(transferId);
    if (it != m_incomingTransfers.end()) {
      it->second.data.insert(it->second.data.end(), chunk.begin(), chunk.end());
      it->second.receivedBytes += chunk.size();

      if (m_onRelayData) {
        m_onRelayData(transferId, chunk, it->second.receivedBytes,
                      it->second.totalSize);
      }
    }

  } else if (type == "relay-end") {
    std::string transferId = json::get(message, "transferId");

    std::lock_guard<std::mutex> lock(m_transfersMutex);
    auto it = m_incomingTransfers.find(transferId);
    if (it != m_incomingTransfers.end()) {
      std::cout << "[WebSignaling] Transfer complete: " << it->second.filename
                << " (" << it->second.data.size() << " bytes)" << std::endl;

      if (m_onTransferComplete) {
        m_onTransferComplete(transferId, it->second.filename, it->second.data);
      }

      m_incomingTransfers.erase(it);
    }
  }
}

bool WebSignalingClient::requestFileSend(const std::string &targetPeerId,
                                         const std::vector<FileInfo> &files) {
  if (!m_connected)
    return false;

  std::ostringstream msg;
  msg << "{\"type\":\"file-request\",\"to\":\"" << json::escape(targetPeerId)
      << "\",\"files\":[";
  for (size_t i = 0; i < files.size(); i++) {
    if (i > 0)
      msg << ",";
    msg << "{\"name\":\"" << json::escape(files[i].name)
        << "\",\"size\":" << files[i].size << ",\"type\":\""
        << json::escape(files[i].mimeType) << "\"}";
  }
  msg << "]}";

  std::lock_guard<std::mutex> lock(m_sendMutex);
  m_sendQueue.push(msg.str());
  return true;
}

void WebSignalingClient::acceptFileRequest(const std::string &fromPeerId) {
  std::ostringstream msg;
  msg << "{\"type\":\"file-response\",\"to\":\"" << json::escape(fromPeerId)
      << "\",\"accepted\":true}";

  std::lock_guard<std::mutex> lock(m_sendMutex);
  m_sendQueue.push(msg.str());
}

void WebSignalingClient::rejectFileRequest(const std::string &fromPeerId) {
  std::ostringstream msg;
  msg << "{\"type\":\"file-response\",\"to\":\"" << json::escape(fromPeerId)
      << "\",\"accepted\":false}";

  std::lock_guard<std::mutex> lock(m_sendMutex);
  m_sendQueue.push(msg.str());
}

bool WebSignalingClient::sendFileViaRelay(const std::string &targetPeerId,
                                          const std::string &filename,
                                          const std::vector<uint8_t> &data,
                                          const std::string &mimeType) {
  if (!m_connected)
    return false;

  // Generate transfer ID
  std::random_device rd;
  std::mt19937 gen(rd());
  std::ostringstream transferIdSs;
  for (int i = 0; i < 8; i++) {
    transferIdSs << std::hex << (gen() & 0xFFFF);
    if (i == 1 || i == 2 || i == 3 || i == 4)
      transferIdSs << "-";
  }
  std::string transferId = transferIdSs.str();

  std::cout << "[WebSignaling] Sending file via relay: " << filename << " ("
            << data.size() << " bytes)" << std::endl;

  // Send start
  {
    std::ostringstream msg;
    msg << "{\"type\":\"relay-start\",\"to\":\"" << json::escape(targetPeerId)
        << "\",\"transferId\":\"" << transferId << "\",\"filename\":\""
        << json::escape(filename) << "\",\"size\":" << data.size()
        << ",\"mimeType\":\"" << json::escape(mimeType)
        << "\",\"fileIndex\":0,\"totalFiles\":1}";

    std::lock_guard<std::mutex> lock(m_sendMutex);
    m_sendQueue.push(msg.str());
  }

  // Send chunks
  const size_t CHUNK_SIZE = 32 * 1024; // 32KB
  for (size_t offset = 0; offset < data.size(); offset += CHUNK_SIZE) {
    size_t chunkLen = std::min(CHUNK_SIZE, data.size() - offset);
    std::string b64 = base64::encode(&data[offset], chunkLen);

    std::ostringstream msg;
    msg << "{\"type\":\"relay-chunk\",\"to\":\"" << json::escape(targetPeerId)
        << "\",\"transferId\":\"" << transferId << "\",\"data\":\"" << b64
        << "\",\"offset\":" << offset << "}";

    std::lock_guard<std::mutex> lock(m_sendMutex);
    m_sendQueue.push(msg.str());

    // Small delay between chunks to not overwhelm
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Send end
  {
    std::ostringstream msg;
    msg << "{\"type\":\"relay-end\",\"to\":\"" << json::escape(targetPeerId)
        << "\",\"transferId\":\"" << transferId << "\"}";

    std::lock_guard<std::mutex> lock(m_sendMutex);
    m_sendQueue.push(msg.str());
  }

  return true;
}

} // namespace teleport
