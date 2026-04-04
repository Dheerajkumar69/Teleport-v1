#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <cstddef>

// Forward declarations to avoid exposing libdatachannel headers globally
namespace rtc {
    class PeerConnection;
    class DataChannel;
}

namespace teleport {

class NativeWebRTCClient {
public:
    NativeWebRTCClient();
    ~NativeWebRTCClient();

    // Configuration
    void init(const std::string& peerId);

    // Callbacks
    using OnLocalDescriptionCallback = std::function<void(const std::string& type, const std::string& sdp)>;
    using OnLocalCandidateCallback = std::function<void(const std::string& candidate, const std::string& mid)>;
    using OnDataChannelOpenCallback = std::function<void()>;
    using OnDataChannelMessageCallback = std::function<void(const uint8_t* data, size_t size)>;
    using OnDataChannelStringMessageCallback = std::function<void(const std::string& message)>;

    void setOnLocalDescription(OnLocalDescriptionCallback cb) { m_onLocalDescription = cb; }
    void setOnLocalCandidate(OnLocalCandidateCallback cb) { m_onLocalCandidate = cb; }
    void setOnDataChannelOpen(OnDataChannelOpenCallback cb) { m_onDataChannelOpen = cb; }
    void setOnDataChannelMessage(OnDataChannelMessageCallback cb) { m_onDataChannelMessage = cb; }
    void setOnDataChannelStringMessage(OnDataChannelStringMessageCallback cb) { m_onDataChannelStringMessage = cb; }

    // Remote signaling -> Local WebRTC
    bool processOffer(const std::string& sdp);
    bool processAnswer(const std::string& sdp);
    bool processIceCandidate(const std::string& candidate, const std::string& mid);

    // API
    bool createOffer();
    bool sendData(const uint8_t* data, size_t size);
    bool sendString(const std::string& message);

private:
    void setupPeerConnection();
    void setupDataChannelCallbacks();

    std::string m_peerId;
    std::shared_ptr<rtc::PeerConnection> m_pc;
    std::shared_ptr<rtc::DataChannel> m_dc;

    OnLocalDescriptionCallback m_onLocalDescription;
    OnLocalCandidateCallback m_onLocalCandidate;
    OnDataChannelOpenCallback m_onDataChannelOpen;
    OnDataChannelMessageCallback m_onDataChannelMessage;
    OnDataChannelStringMessageCallback m_onDataChannelStringMessage;
};

} // namespace teleport
