/**
 * @file TransferProgress.h
 * @brief Circular and linear progress indicators
 */

#pragma once

#include "Theme.h"
#include "imgui.h"

namespace teleport::ui {

/**
 * @brief Draw a circular progress indicator
 */
void DrawCircularProgress(
    ImDrawList* drawList,
    ImVec2 center,
    float radius,
    float progress,    // 0.0 to 1.0
    ImU32 bgColor,
    ImU32 fgColor,
    float thickness = 4.0f
);

/**
 * @brief Draw an arc progress indicator with glow
 */
void DrawArcProgress(
    ImDrawList* drawList,
    ImVec2 center,
    float radius,
    float progress,
    Theme* theme,
    bool showGlow = true
);

/**
 * @brief Draw a linear progress bar
 */
void DrawLinearProgress(
    ImDrawList* drawList,
    ImVec2 pos,
    ImVec2 size,
    float progress,
    ImU32 bgColor,
    ImU32 fgColor,
    float rounding = 4.0f
);

} // namespace teleport::ui
