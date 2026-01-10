/**
 * @file DiscoverView.h
 * @brief Device discovery view with delightful animations
 */

#pragma once

#include "imgui.h"
#include "TeleportBridge.h"
#include "Theme.h"
#include <string>
#include <vector>
#include <random>

namespace teleport::ui {

// Celebration particle for success effects
struct Particle {
    float x, y;
    float vx, vy;
    float life;
    float size;
    ImU32 color;
};

class DiscoverView {
public:
    DiscoverView(TeleportBridge* bridge, Theme* theme);
    ~DiscoverView() = default;

    void Update();
    void Render();
    
    // Trigger celebration on transfer complete
    void TriggerCelebration();
    
    /**
     * @brief Check if user requested to send to a device
     * @return Device ID to send to, or empty if no request
     */
    std::string PopSendRequest() {
        std::string result = sendRequestDeviceId_;
        sendRequestDeviceId_.clear();
        return result;
    }

private:
    void RenderHeader();
    void RenderConnectionMethods();
    void RenderDeviceGrid();
    void RenderDeviceCard(const DeviceInfo& device, int index);
    void RenderEmptyState();
    void RenderStatusBar();
    void RenderQrModal();
    void RenderHotspotModal();
    void RenderManualConnectModal();
    void RenderCelebration();
    void UpdateParticles(float dt);

    TeleportBridge* bridge_;
    Theme* theme_;
    
    // Animation state
    float pulseAnimation_ = 0.0f;
    float emptyStateAnim_ = 0.0f;
    float cardHoverAnim_[32] = {0};  // Max 32 devices
    float cardScaleAnim_[32] = {0};  // Scale bounce on hover
    float modalFadeIn_ = 0.0f;       // Modal fade animation
    float successGlow_ = 0.0f;       // Success glow effect
    std::string selectedDeviceId_;
    std::string sendRequestDeviceId_;
    
    // Celebration particles
    std::vector<Particle> particles_;
    bool celebrating_ = false;
    float celebrationTimer_ = 0.0f;
    std::mt19937 rng_{std::random_device{}()};
    
    // Modal states
    bool showQrModal_ = false;
    bool showHotspotModal_ = false;
    bool showManualConnectModal_ = false;
    std::vector<uint8_t> qrImageData_;
    std::string qrSessionToken_;
    int qrExpirySeconds_ = 300;
    
    // Hotspot state
    bool hotspotActive_ = false;
    std::string hotspotSsid_;
    std::string hotspotPassword_;
    std::string hotspotGatewayIp_;
    
    // Manual connect state
    char manualIp_[64] = "";
    char manualPort_[8] = "42000";
    char manualName_[64] = "Remote Device";
};

} // namespace teleport::ui


