/**
 * @file DiscoverView.cpp
 * @brief Device discovery view with animated cards
 */

#include "DiscoverView.h"
#include "components/DeviceCard.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <string>

namespace teleport::ui {

// OS Icons (Segoe MDL2 Assets)
static const char* ICON_WINDOWS = "\xEE\x83\x8E";  // Windows logo
static const char* ICON_ANDROID = "\xEE\x83\x82";  // Phone
static const char* ICON_MACOS = "\xEE\x83\x94";    // Monitor
static const char* ICON_SCAN = "\xEE\x81\x94";     // Radar

DiscoverView::DiscoverView(TeleportBridge* bridge, Theme* theme)
    : bridge_(bridge), theme_(theme) {}

void DiscoverView::Update() {
    // Update pulse animation
    pulseAnimation_ += 0.05f;
    if (pulseAnimation_ > 6.28f) pulseAnimation_ = 0.0f;
    
    // Update empty state animation
    if (bridge_->GetDevices().empty()) {
        emptyStateAnim_ += 0.03f;
    }
}

void DiscoverView::Render() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 20));
    
    RenderHeader();
    ImGui::Spacing();
    ImGui::Spacing();
    RenderStatusBar();
    ImGui::Spacing();
    ImGui::Spacing();
    
    auto devices = bridge_->GetDevices();
    if (devices.empty()) {
        RenderEmptyState();
    } else {
        RenderDeviceGrid();
    }
    
    ImGui::PopStyleVar();
}

void DiscoverView::RenderHeader() {
    ImGui::PushFont(theme_->GetHeadingFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Discover Devices");
    ImGui::PopFont();
    
    ImGui::SameLine(0, 20);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), 
        "Find devices on your local network");
}

void DiscoverView::RenderStatusBar() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    
    // Status indicator
    bool isDiscovering = bridge_->IsDiscovering();
    
    // Background pill
    float pillWidth = isDiscovering ? 140.0f : 120.0f;
    drawList->AddRectFilled(
        pos,
        ImVec2(pos.x + pillWidth, pos.y + 32),
        theme_->GetColor(ThemeColor::SurfaceLight),
        16.0f
    );
    
    // Animated dot
    float dotRadius = 4.0f;
    ImVec2 dotCenter(pos.x + 16, pos.y + 16);
    
    if (isDiscovering) {
        // Pulsing glow
        float pulse = (std::sin(pulseAnimation_) + 1.0f) * 0.5f;
        ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.063f, 0.725f, 0.506f, 0.3f + pulse * 0.3f)
        );
        drawList->AddCircleFilled(dotCenter, dotRadius + 4 + pulse * 4, glowColor);
        drawList->AddCircleFilled(dotCenter, dotRadius, theme_->GetColor(ThemeColor::Success));
    } else {
        drawList->AddCircleFilled(dotCenter, dotRadius, theme_->GetColor(ThemeColor::TextDisabled));
    }
    
    // Status text
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 28, pos.y + 7));
    ImGui::TextColored(
        isDiscovering ? theme_->GetColorVec(ThemeColor::Success) : theme_->GetColorVec(ThemeColor::TextSecondary),
        isDiscovering ? "Scanning..." : "Paused"
    );
    
    // Toggle button
    ImGui::SameLine(0, 30);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 7);
    
    ImGui::PushStyleColor(ImGuiCol_Button, 
        isDiscovering ? theme_->GetColorVec(ThemeColor::SurfaceLight) : theme_->GetColorVec(ThemeColor::Primary));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 
        isDiscovering ? ImVec4(0.2f, 0.2f, 0.22f, 0.9f) : theme_->GetColorVec(ThemeColor::PrimaryLight));
    ImGui::PushStyleColor(ImGuiCol_Text, theme_->GetColorVec(ThemeColor::TextPrimary));
    
    if (ImGui::Button(isDiscovering ? "  Stop  " : "  Start Discovery  ", ImVec2(0, 32))) {
        if (isDiscovering) {
            bridge_->StopDiscovery();
        } else {
            bridge_->StartDiscovery();
        }
    }
    
    ImGui::PopStyleColor(3);
    
    // Device count
    auto devices = bridge_->GetDevices();
    if (!devices.empty()) {
        ImGui::SameLine(0, 20);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7);
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), 
            "%zu device%s found", devices.size(), devices.size() == 1 ? "" : "s");
    }
    
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);
}

void DiscoverView::RenderDeviceGrid() {
    auto devices = bridge_->GetDevices();
    
    // Calculate grid layout
    float availableWidth = ImGui::GetContentRegionAvail().x - 30;
    float cardWidth = 280.0f;
    float cardSpacing = 20.0f;
    int columns = std::max(1, (int)((availableWidth + cardSpacing) / (cardWidth + cardSpacing)));
    
    ImGui::BeginChild("##DeviceGrid", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);
    
    int col = 0;
    for (size_t i = 0; i < devices.size(); i++) {
        if (col > 0) {
            ImGui::SameLine(0, cardSpacing);
        }
        
        RenderDeviceCard(devices[i], (int)i);
        
        col++;
        if (col >= columns) {
            col = 0;
        }
    }
    
    ImGui::EndChild();
}

void DiscoverView::RenderDeviceCard(const DeviceInfo& device, int index) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cardPos = ImGui::GetCursorScreenPos();
    ImVec2 cardSize(280, 140);
    
    // Check hover
    ImVec2 cardEnd(cardPos.x + cardSize.x, cardPos.y + cardSize.y);
    bool isHovered = ImGui::IsMouseHoveringRect(cardPos, cardEnd);
    
    // Animate hover
    float targetHover = isHovered ? 1.0f : 0.0f;
    cardHoverAnim_[index] += (targetHover - cardHoverAnim_[index]) * 0.2f;
    
    // Fade in animation
    float fadeIn = device.fadeIn;
    float scale = 0.95f + fadeIn * 0.05f;
    float alpha = fadeIn;
    
    // Card background with glass effect
    ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.11f, 0.11f, 0.125f, 0.75f * alpha + cardHoverAnim_[index] * 0.1f)
    );
    
    drawList->AddRectFilled(cardPos, cardEnd, bgColor, Theme::CardRadius);
    
    // Border
    ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.2f + cardHoverAnim_[index] * 0.2f, 0.2f, 0.22f + cardHoverAnim_[index] * 0.3f, 0.5f)
    );
    drawList->AddRect(cardPos, cardEnd, borderColor, Theme::CardRadius, 0, 1.0f);
    
    // Glow on hover
    if (cardHoverAnim_[index] > 0.01f) {
        ImVec4 glowVec = theme_->GetColorVec(ThemeColor::Primary);
        glowVec.w = 0.15f * cardHoverAnim_[index];
        ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(glowVec);
        
        for (int g = 3; g > 0; g--) {
            float offset = (float)g * 3.0f;
            drawList->AddRect(
                ImVec2(cardPos.x - offset, cardPos.y - offset),
                ImVec2(cardEnd.x + offset, cardEnd.y + offset),
                glowColor, Theme::CardRadius + offset, 0, 2.0f
            );
        }
    }
    
    // OS Icon
    float iconX = cardPos.x + 20;
    float iconY = cardPos.y + 25;
    
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    
    // OS-specific icon and color
    ImVec4 osColor = theme_->GetColorVec(ThemeColor::Primary);
    const char* osIcon = ICON_WINDOWS;
    
    if (device.os == "Android") {
        osColor = ImVec4(0.608f, 0.804f, 0.396f, 1.0f); // Android green
        osIcon = ICON_ANDROID;
    } else if (device.os == "macOS" || device.os == "iOS") {
        osColor = ImVec4(0.8f, 0.8f, 0.82f, 1.0f); // Apple silver
        osIcon = ICON_MACOS;
    }
    
    // Icon background circle
    drawList->AddCircleFilled(
        ImVec2(iconX + 20, iconY + 20),
        24,
        ImGui::ColorConvertFloat4ToU32(ImVec4(osColor.x, osColor.y, osColor.z, 0.15f))
    );
    
    // Icon
    ImGui::SetCursorScreenPos(ImVec2(iconX + 8, iconY + 8));
    ImGui::PushFont(theme_->GetIconFont());
    ImGui::TextColored(osColor, osIcon);
    ImGui::PopFont();
    
    // Device name
    ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 80, cardPos.y + 20));
    ImGui::PushFont(theme_->GetBodyFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s", device.name.c_str());
    
    // OS label
    ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 80, cardPos.y + 42));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s", device.os.c_str());
    ImGui::PopFont();
    
    // IP Address
    ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 20, cardPos.y + 85));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "IP:");
    ImGui::SameLine();
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s", device.ip.c_str());
    
    // Send button (on hover)
    if (cardHoverAnim_[index] > 0.3f) {
        ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 180, cardPos.y + 100));
        
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, cardHoverAnim_[index]);
        ImGui::PushStyleColor(ImGuiCol_Button, theme_->GetColorVec(ThemeColor::Primary));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme_->GetColorVec(ThemeColor::PrimaryLight));
        
        std::string buttonLabel = "Send Files##" + std::to_string(index);
        if (ImGui::Button(buttonLabel.c_str(), ImVec2(85, 28))) {
            selectedDeviceId_ = device.id;
            // TODO: Navigate to send view or open file picker
        }
        
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }
    
    ImGui::PopStyleVar(); // Alpha
    
    // Reserve space for the card
    ImGui::Dummy(cardSize);
}

void DiscoverView::RenderEmptyState() {
    ImVec2 available = ImGui::GetContentRegionAvail();
    ImVec2 center(ImGui::GetCursorScreenPos().x + available.x * 0.5f,
                  ImGui::GetCursorScreenPos().y + available.y * 0.4f);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Animated radar circles
    float time = emptyStateAnim_;
    for (int i = 0; i < 3; i++) {
        float phase = std::fmod(time + i * 2.0f, 6.0f);
        float radius = 30 + phase * 25;
        float alpha = std::max(0.0f, 1.0f - phase / 6.0f) * 0.3f;
        
        ImU32 circleColor = ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.486f, 0.228f, 0.929f, alpha)
        );
        drawList->AddCircle(center, radius, circleColor, 64, 2.0f);
    }
    
    // Center icon
    ImGui::PushFont(theme_->GetIconFont());
    ImVec2 iconSize = ImGui::CalcTextSize(ICON_SCAN);
    ImGui::SetCursorScreenPos(ImVec2(center.x - iconSize.x * 0.5f, center.y - iconSize.y * 0.5f));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::Primary), ICON_SCAN);
    ImGui::PopFont();
    
    // Text
    const char* text = bridge_->IsDiscovering() 
        ? "Scanning for devices..." 
        : "Start discovery to find devices";
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImGui::SetCursorScreenPos(ImVec2(center.x - textSize.x * 0.5f, center.y + 60));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s", text);
    
    // Help text
    const char* helpText = "Devices on the same network will appear here";
    ImVec2 helpSize = ImGui::CalcTextSize(helpText);
    ImGui::SetCursorScreenPos(ImVec2(center.x - helpSize.x * 0.5f, center.y + 85));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled), "%s", helpText);
}

} // namespace teleport::ui
