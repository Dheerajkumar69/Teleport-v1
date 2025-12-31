/**
 * @file TransferProgress.cpp
 * @brief Progress indicator implementations
 */

#include "TransferProgress.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace teleport::ui {

void DrawCircularProgress(
    ImDrawList* drawList,
    ImVec2 center,
    float radius,
    float progress,
    ImU32 bgColor,
    ImU32 fgColor,
    float thickness
) {
    const int numSegments = 64;
    
    // Background circle
    drawList->AddCircle(center, radius, bgColor, numSegments, thickness);
    
    // Progress arc
    if (progress > 0.0f) {
        float startAngle = -M_PI * 0.5f;  // Start from top
        float endAngle = startAngle + progress * 2.0f * M_PI;
        
        drawList->PathArcTo(center, radius, startAngle, endAngle, numSegments);
        drawList->PathStroke(fgColor, ImDrawFlags_None, thickness);
    }
}

void DrawArcProgress(
    ImDrawList* drawList,
    ImVec2 center,
    float radius,
    float progress,
    Theme* theme,
    bool showGlow
) {
    const int numSegments = 64;
    
    // Background ring
    drawList->AddCircle(center, radius, 
        theme->GetColor(ThemeColor::SurfaceLight), numSegments, 6.0f);
    
    if (progress > 0.0f) {
        float startAngle = -M_PI * 0.5f;
        float endAngle = startAngle + progress * 2.0f * M_PI;
        
        // Gradient effect using multiple arcs
        for (int i = 0; i < 3; i++) {
            float t = (float)i / 2.0f;
            float a = 1.0f - t * 0.3f;
            
            ImVec4 color = theme->GetColorVec(ThemeColor::Primary);
            color.w = a;
            
            drawList->PathArcTo(center, radius - i, startAngle, endAngle, numSegments);
            drawList->PathStroke(ImGui::ColorConvertFloat4ToU32(color), ImDrawFlags_None, 4.0f - i);
        }
        
        // Glow at the end point
        if (showGlow && progress < 1.0f) {
            float glowX = center.x + radius * std::cos(endAngle);
            float glowY = center.y + radius * std::sin(endAngle);
            
            ImVec4 glowColor = theme->GetColorVec(ThemeColor::Primary);
            
            for (int g = 4; g > 0; g--) {
                glowColor.w = 0.1f + (4 - g) * 0.1f;
                drawList->AddCircleFilled(
                    ImVec2(glowX, glowY),
                    4.0f + g * 2.0f,
                    ImGui::ColorConvertFloat4ToU32(glowColor)
                );
            }
            
            // Bright center
            drawList->AddCircleFilled(ImVec2(glowX, glowY), 4.0f,
                theme->GetColor(ThemeColor::PrimaryLight));
        }
    }
}

void DrawLinearProgress(
    ImDrawList* drawList,
    ImVec2 pos,
    ImVec2 size,
    float progress,
    ImU32 bgColor,
    ImU32 fgColor,
    float rounding
) {
    // Background
    drawList->AddRectFilled(pos, 
        ImVec2(pos.x + size.x, pos.y + size.y),
        bgColor, rounding);
    
    // Progress fill
    if (progress > 0.0f) {
        float fillWidth = size.x * std::min(1.0f, progress);
        drawList->AddRectFilled(pos,
            ImVec2(pos.x + fillWidth, pos.y + size.y),
            fgColor, rounding);
    }
}

} // namespace teleport::ui
