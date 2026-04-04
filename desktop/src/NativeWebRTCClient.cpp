#include "NativeWebRTCClient.h"
#include <rtc/rtc.hpp>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace teleport {

NativeWebRTCClient::NativeWebRTCClient() : m_pc(nullptr), m_dc(nullptr) {
    // Optionally initialize libdatachannel global config here
    rtc::InitLogger(rtc::LogLevel::Warning);
}

NativeWebRTCClient::~NativeWebRTCClient() {
    if (m_dc) m_dc->close();
    if (m_pc) m_pc->close();
}

void NativeWebRTCClient::init(const std::string& peerId) {
    m_peerId = peerId;
    setupPeerConnection();
}

void NativeWebRTCClient::setupPeerConnection() {
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    
    // openrelay STUN/TURN fallback
    rtc::IceServer turn("turn:openrelay.metered.ca:443?transport=tcp");
    turn.username = "openrelayproject";
    turn.password = "openrelayproject";
    config.iceServers.push_back(turn);

    m_pc = std::make_shared<rtc::PeerConnection>(config);

    m_pc->onLocalDescription([this](rtc::Description description) {
        if (m_onLocalDescription) {
            m_onLocalDescription(description.typeString(), std::string(description));
        }
    });

    m_pc->onLocalCandidate([this](rtc::Candidate candidate) {
        if (m_onLocalCandidate) {
            m_onLocalCandidate(std::string(candidate), candidate.mid());
        }
    });

    m_pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[NativeWebRTC] Connection state changed: " << state << std::endl;
    });

    m_pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
        std::cout << "[NativeWebRTC] ICE gathering state changed: " << state << std::endl;
    });

    m_pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
        std::cout << "[NativeWebRTC] Data channel received from remote: " << dc->label() << std::endl;
        m_dc = dc;
        this->setupDataChannelCallbacks();
    });
}

void NativeWebRTCClient::setupDataChannelCallbacks() {
    if (!m_dc) return;

    m_dc->onOpen([this]() {
        std::cout << "[NativeWebRTC] Data channel opened" << std::endl;
        if (m_onDataChannelOpen) m_onDataChannelOpen();
    });

    m_dc->onMessage([this](std::variant<rtc::binary, rtc::string> message) {
        if (std::holds_alternative<rtc::string>(message)) {
            // It's a JSON control message from the web (file-start, resume-ready, file-end)
            std::string msg = std::get<rtc::string>(message);
            std::cout << "[NativeWebRTC] Received JSON message: " << msg.substr(0, 50) << "..." << std::endl;
            
            if (m_onDataChannelStringMessage) {
                m_onDataChannelStringMessage(msg);
            }
        } else {
            // It's a binary file chunk
            auto data = std::get<rtc::binary>(message);
            if (m_onDataChannelMessage) {
                m_onDataChannelMessage(reinterpret_cast<const uint8_t*>(data.data()), data.size());
            }
        }
    });

    m_dc->onError([](std::string error) {
        std::cerr << "[NativeWebRTC] Data channel error: " << error << std::endl;
    });

    m_dc->onClosed([]() {
        std::cout << "[NativeWebRTC] Data channel closed" << std::endl;
    });
}

bool NativeWebRTCClient::processOffer(const std::string& sdp) {
    if (!m_pc) return false;
    
    try {
        auto desc = rtc::Description(sdp, "offer");
        m_pc->setRemoteDescription(desc);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[NativeWebRTC] Failed to process offer: " << e.what() << std::endl;
        return false;
    }
}

bool NativeWebRTCClient::processAnswer(const std::string& sdp) {
    if (!m_pc) return false;
    
    try {
        auto desc = rtc::Description(sdp, "answer");
        m_pc->setRemoteDescription(desc);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[NativeWebRTC] Failed to process answer: " << e.what() << std::endl;
        return false;
    }
}

bool NativeWebRTCClient::processIceCandidate(const std::string& candidate, const std::string& mid) {
    if (!m_pc) return false;
    
    try {
        m_pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[NativeWebRTC] Failed to process ICE candidate: " << e.what() << std::endl;
        return false;
    }
}

bool NativeWebRTCClient::createOffer() {
    if (!m_pc) return false;
    
    try {
        // We are initiating, so we create the DataChannel FIRST before the offer
        rtc::DataChannelInit dcConfig;
        
        m_dc = m_pc->createDataChannel("teleport-files", dcConfig);
        this->setupDataChannelCallbacks();
        
        return true; // We don't manually trigger createOffer(), libdatachannel does it automatically when you create a DC or add tracks
    } catch (const std::exception& e) {
        std::cerr << "[NativeWebRTC] Failed to create offer: " << e.what() << std::endl;
        return false;
    }
}

bool NativeWebRTCClient::sendData(const uint8_t* data, size_t size) {
    if (!m_dc || m_dc->isOpen() == false) return false;
    
    try {
        return m_dc->send(reinterpret_cast<const std::byte*>(data), size);
    } catch (const std::exception& e) {
        std::cerr << "[NativeWebRTC] Send binary failed: " << e.what() << std::endl;
        return false;
    }
}

bool NativeWebRTCClient::sendString(const std::string& message) {
    if (!m_dc || m_dc->isOpen() == false) return false;
    
    try {
        return m_dc->send(message);
    } catch (const std::exception& e) {
        std::cerr << "[NativeWebRTC] Send string failed: " << e.what() << std::endl;
        return false;
    }
}

} // namespace teleport
