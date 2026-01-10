/**
 * @file ReceiveView.h
 * @brief File receiving view with celebrations
 */

#pragma once

#include "TeleportBridge.h"
#include "Theme.h"
#include <string>
#include <vector>
#include <random>

namespace teleport::ui {

// Celebration particle
struct ReceiveParticle {
    float x, y;
    float vx, vy;
    float life;
    float size;
    float rotation;
    unsigned int color;
};

class ReceiveView {
public:
    ReceiveView(TeleportBridge* bridge, Theme* theme);
    ~ReceiveView() = default;

    void Update();
    void Render();
    
    // Trigger celebration on receive complete
    void TriggerCelebration();

private:
    void RenderHeader();
    void RenderStatus();
    void RenderFolderSelector();
    void RenderToggle();
    void RenderIncomingDialog();
    void RenderProgressBar();
    void RenderCelebration();
    void UpdateParticles(float dt);
    void PlaySuccessSound();

    TeleportBridge* bridge_;
    Theme* theme_;
    
    std::string downloadPath_;
    float toggleAnim_ = 0.0f;
    float pulseAnim_ = 0.0f;
    
    // Celebration state
    bool celebrating_ = false;
    float celebrationTimer_ = 0.0f;
    float successGlow_ = 0.0f;
    std::vector<ReceiveParticle> particles_;
    std::mt19937 rng_{std::random_device{}()};
    
    // Transfer tracking
    std::string lastTransferId_;
};

} // namespace teleport::ui
