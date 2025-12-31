/**
 * @file TransfersView.h
 * @brief Active transfers view with progress indicators
 */

#pragma once

#include "TeleportBridge.h"
#include "Theme.h"

namespace teleport::ui {

class TransfersView {
public:
    TransfersView(TeleportBridge* bridge, Theme* theme);
    ~TransfersView() = default;

    void Update();
    void Render();

private:
    void RenderHeader();
    void RenderTransferList();
    void RenderTransferCard(const TransferInfo& transfer, int index);
    void RenderEmptyState();

    TeleportBridge* bridge_;
    Theme* theme_;
    
    float emptyAnim_ = 0.0f;
};

} // namespace teleport::ui
