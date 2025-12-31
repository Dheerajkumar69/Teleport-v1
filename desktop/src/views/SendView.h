/**
 * @file SendView.h
 * @brief File sending view with drag & drop
 */

#pragma once

#include <windows.h>
#include <shellapi.h>
#include "TeleportBridge.h"
#include "Theme.h"
#include <vector>
#include <string>

namespace teleport::ui {

class SendView {
public:
    SendView(TeleportBridge* bridge, Theme* theme);
    ~SendView() = default;

    void Update();
    void Render();
    void HandleFileDrop(HDROP hDrop);

private:
    void RenderHeader();
    void RenderDeviceSelector();
    void RenderFileDropZone();
    void RenderFileList();
    void RenderSendButton();

    TeleportBridge* bridge_;
    Theme* theme_;
    
    std::vector<std::string> selectedFiles_;
    std::string selectedDeviceId_;
    
    // Animation
    float dropZoneAnim_ = 0.0f;
    bool isDragging_ = false;
    float sendButtonAnim_ = 0.0f;
};

} // namespace teleport::ui
