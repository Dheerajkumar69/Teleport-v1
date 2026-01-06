/**
 * @file DiscoverView.h
 * @brief Device discovery view with animated device cards
 */

#pragma once

#include "TeleportBridge.h"
#include "Theme.h"
#include <string>

namespace teleport::ui {

class DiscoverView {
public:
    DiscoverView(TeleportBridge* bridge, Theme* theme);
    ~DiscoverView() = default;

    void Update();
    void Render();
    
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
    void RenderDeviceGrid();
    void RenderDeviceCard(const DeviceInfo& device, int index);
    void RenderEmptyState();
    void RenderStatusBar();

    TeleportBridge* bridge_;
    Theme* theme_;
    
    // Animation state
    float pulseAnimation_ = 0.0f;
    float emptyStateAnim_ = 0.0f;
    float cardHoverAnim_[32] = {0};  // Max 32 devices
    std::string selectedDeviceId_;
    std::string sendRequestDeviceId_;  // Set when Send button clicked
};

} // namespace teleport::ui
