/**
 * @file SendView.h
 * @brief File sending view with lovable micro-interactions
 */

#pragma once

#include <windows.h>
#include <shellapi.h>
#include "TeleportBridge.h"
#include "Theme.h"
#include <vector>
#include <string>
#include <random>

namespace teleport::ui {

// Celebration particle for confetti effect
struct SendParticle {
    float x, y;
    float vx, vy;
    float life;
    float size;
    float rotation;
    unsigned int color;
};

class SendView {
public:
    SendView(TeleportBridge* bridge, Theme* theme);
    ~SendView() = default;

    void Update();
    void Render();
    void HandleFileDrop(HDROP hDrop);
    
    void SetTargetDevice(const std::string& deviceId) { selectedDeviceId_ = deviceId; }
    
    // Trigger celebration effect (called on transfer complete)
    void TriggerCelebration();

private:
    void RenderHeader();
    void RenderDeviceSelector();
    void RenderFileDropZone();
    void RenderFileList();
    void RenderSendButton();
    void RenderCelebration();
    void RenderProgressBar();
    void UpdateParticles(float dt);
    void PlaySuccessSound();

    TeleportBridge* bridge_;
    Theme* theme_;
    
    std::vector<std::string> selectedFiles_;
    std::string selectedDeviceId_;
    
    // Drop zone animation
    float dropZoneAnim_ = 0.0f;
    bool isDragging_ = false;
    
    // Send button micro-interactions
    float sendButtonAnim_ = 0.0f;
    float sendButtonPulse_ = 0.0f;
    float sendButtonScale_ = 1.0f;
    bool buttonPressed_ = false;
    
    // Device card animations
    float deviceHoverAnim_[16] = {0};  // Up to 16 devices
    float deviceScaleAnim_[16] = {0};
    float deviceGlowAnim_[16] = {0};
    
    // Celebration state
    bool celebrating_ = false;
    float celebrationTimer_ = 0.0f;
    float successGlow_ = 0.0f;
    std::vector<SendParticle> particles_;
    std::mt19937 rng_{std::random_device{}()};
    
    // Transfer tracking
    bool wasSending_ = false;
    std::string lastTransferId_;
    
    // File drop animation
    float fileDropFlash_ = 0.0f;
    int prevFileCount_ = 0;
};

} // namespace teleport::ui
