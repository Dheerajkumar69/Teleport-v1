/**
 * @file ReceiveView.h
 * @brief File receiving view
 */

#pragma once

#include "TeleportBridge.h"
#include "Theme.h"
#include <string>

namespace teleport::ui {

class ReceiveView {
public:
    ReceiveView(TeleportBridge* bridge, Theme* theme);
    ~ReceiveView() = default;

    void Update();
    void Render();

private:
    void RenderHeader();
    void RenderStatus();
    void RenderFolderSelector();
    void RenderToggle();
    void RenderIncomingDialog();

    TeleportBridge* bridge_;
    Theme* theme_;
    
    std::string downloadPath_;
    float toggleAnim_ = 0.0f;
    float pulseAnim_ = 0.0f;
};

} // namespace teleport::ui
