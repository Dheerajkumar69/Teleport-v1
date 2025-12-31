/**
 * @file TransfersView.cpp
 * @brief Active transfers view with progress indicators
 */

#include "TransfersView.h"
#include "imgui.h"
#include <teleport/teleport.h>
#include <cmath>
#include <string>

namespace teleport::ui {

// Icons
static const char* ICON_UPLOAD = "\xEE\x89\x8C";
static const char* ICON_DOWNLOAD = "\xEE\x89\x96";
static const char* ICON_PAUSE = "\xEE\x89\xB4";
static const char* ICON_PLAY = "\xEE\x89\xB6";
static const char* ICON_CANCEL = "\xEE\x89\x8A";
static const char* ICON_CHECK = "\xEE\x89\xBE";
static const char* ICON_ERROR = "\xEE\x89\xA0";

TransfersView::TransfersView(TeleportBridge* bridge, Theme* theme)
    : bridge_(bridge), theme_(theme) {}

void TransfersView::Update() {
    auto transfers = bridge_->GetTransfers();
    if (transfers.empty()) {
        emptyAnim_ += 0.02f;
    }
}

void TransfersView::Render() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 20));
    
    RenderHeader();
    ImGui::Spacing();
    ImGui::Spacing();
    
    auto transfers = bridge_->GetTransfers();
    if (transfers.empty()) {
        RenderEmptyState();
    } else {
        RenderTransferList();
    }
    
    ImGui::PopStyleVar();
}

void TransfersView::RenderHeader() {
    ImGui::PushFont(theme_->GetHeadingFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Transfers");
    ImGui::PopFont();
    
    auto transfers = bridge_->GetTransfers();
    int activeCount = 0;
    for (const auto& t : transfers) {
        if (t.state == TELEPORT_STATE_TRANSFERRING || 
            t.state == TELEPORT_STATE_CONNECTING ||
            t.state == TELEPORT_STATE_HANDSHAKING) {
            activeCount++;
        }
    }
    
    if (activeCount > 0) {
        ImGui::SameLine(0, 20);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::Success), 
            "%d active transfer%s", activeCount, activeCount == 1 ? "" : "s");
    }
}

void TransfersView::RenderTransferList() {
    auto transfers = bridge_->GetTransfers();
    
    ImGui::BeginChild("##TransferList", ImVec2(0, 0), false);
    
    for (size_t i = 0; i < transfers.size(); i++) {
        RenderTransferCard(transfers[i], (int)i);
        ImGui::Spacing();
    }
    
    ImGui::EndChild();
}

void TransfersView::RenderTransferCard(const TransferInfo& transfer, int index) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x - 10, 120);
    
    // Card background
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        theme_->GetColor(ThemeColor::Card), Theme::CardRadius);
    
    // Direction icon
    float iconX = pos.x + 25;
    float iconY = pos.y + 25;
    
    ImVec4 iconColor = transfer.isSending 
        ? theme_->GetColorVec(ThemeColor::Primary)
        : theme_->GetColorVec(ThemeColor::Accent);
    const char* icon = transfer.isSending ? ICON_UPLOAD : ICON_DOWNLOAD;
    
    // Icon background
    drawList->AddCircleFilled(ImVec2(iconX + 12, iconY + 12), 22,
        ImGui::ColorConvertFloat4ToU32(ImVec4(iconColor.x, iconColor.y, iconColor.z, 0.15f)));
    
    ImGui::SetCursorScreenPos(ImVec2(iconX, iconY));
    ImGui::PushFont(theme_->GetIconFont());
    ImGui::TextColored(iconColor, icon);
    ImGui::PopFont();
    
    // Transfer info
    float infoX = pos.x + 75;
    
    // Device name and direction
    ImGui::SetCursorScreenPos(ImVec2(infoX, pos.y + 15));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
        transfer.isSending ? "Sending to" : "Receiving from");
    ImGui::SameLine();
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), 
        "%s", transfer.deviceName.c_str());
    
    // Current file
    ImGui::SetCursorScreenPos(ImVec2(infoX, pos.y + 38));
    std::string fileName = transfer.currentFile;
    if (fileName.length() > 40) {
        fileName = fileName.substr(0, 37) + "...";
    }
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), 
        "%s", fileName.empty() ? "Preparing..." : fileName.c_str());
    
    // Progress bar
    float progressBarX = infoX;
    float progressBarY = pos.y + 65;
    float progressBarWidth = size.x - 200;
    float progressBarHeight = 8;
    
    // Background
    drawList->AddRectFilled(
        ImVec2(progressBarX, progressBarY),
        ImVec2(progressBarX + progressBarWidth, progressBarY + progressBarHeight),
        theme_->GetColor(ThemeColor::SurfaceLight),
        progressBarHeight * 0.5f
    );
    
    // Progress fill with gradient
    float progress = transfer.progress;
    if (progress > 0) {
        ImU32 progressColor = transfer.state == TELEPORT_STATE_COMPLETE
            ? theme_->GetColor(ThemeColor::Success)
            : (transfer.state == TELEPORT_STATE_FAILED
                ? theme_->GetColor(ThemeColor::Error)
                : theme_->GetColor(ThemeColor::Primary));
        
        drawList->AddRectFilled(
            ImVec2(progressBarX, progressBarY),
            ImVec2(progressBarX + progressBarWidth * progress, progressBarY + progressBarHeight),
            progressColor,
            progressBarHeight * 0.5f
        );
        
        // Glow effect on active transfers
        if (transfer.state == TELEPORT_STATE_TRANSFERRING) {
            ImVec4 glowVec = theme_->GetColorVec(ThemeColor::Primary);
            glowVec.w = 0.3f;
            drawList->AddRectFilled(
                ImVec2(progressBarX + progressBarWidth * progress - 20, progressBarY - 4),
                ImVec2(progressBarX + progressBarWidth * progress + 5, progressBarY + progressBarHeight + 4),
                ImGui::ColorConvertFloat4ToU32(glowVec),
                6.0f
            );
        }
    }
    
    // Progress text
    ImGui::SetCursorScreenPos(ImVec2(infoX, pos.y + 85));
    
    char transferredStr[32], totalStr[32], speedStr[32];
    teleport_format_bytes(transfer.bytesTransferred, transferredStr, sizeof(transferredStr));
    teleport_format_bytes(transfer.bytesTotal, totalStr, sizeof(totalStr));
    teleport_format_bytes((uint64_t)transfer.speedBps, speedStr, sizeof(speedStr));
    
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
        "%s / %s", transferredStr, totalStr);
    
    if (transfer.state == TELEPORT_STATE_TRANSFERRING) {
        ImGui::SameLine(0, 20);
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary),
            "%s/s", speedStr);
        
        if (transfer.etaSeconds > 0) {
            char etaStr[32];
            teleport_format_duration(transfer.etaSeconds, etaStr, sizeof(etaStr));
            ImGui::SameLine(0, 20);
            ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled),
                "%s remaining", etaStr);
        }
    }
    
    // Status badge and controls
    float controlsX = pos.x + size.x - 120;
    float controlsY = pos.y + 40;
    
    switch (transfer.state) {
        case TELEPORT_STATE_CONNECTING:
        case TELEPORT_STATE_HANDSHAKING:
            ImGui::SetCursorScreenPos(ImVec2(controlsX, controlsY));
            ImGui::TextColored(theme_->GetColorVec(ThemeColor::Warning), "Connecting...");
            break;
            
        case TELEPORT_STATE_TRANSFERRING: {
            // Pause button
            ImGui::SetCursorScreenPos(ImVec2(controlsX, controlsY));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme_->GetColorVec(ThemeColor::SurfaceLight));
            ImGui::PushFont(theme_->GetIconFont());
            
            if (ImGui::Button((std::string(ICON_PAUSE) + "##Pause" + std::to_string(index)).c_str(), 
                              ImVec2(36, 36))) {
                bridge_->PauseTransfer(transfer.id);
            }
            
            ImGui::PopFont();
            ImGui::PopStyleColor(2);
            
            // Cancel button
            ImGui::SameLine(0, 10);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.1f, 0.1f, 0.5f));
            ImGui::PushFont(theme_->GetIconFont());
            
            if (ImGui::Button((std::string(ICON_CANCEL) + "##Cancel" + std::to_string(index)).c_str(),
                              ImVec2(36, 36))) {
                bridge_->CancelTransfer(transfer.id);
            }
            
            ImGui::PopFont();
            ImGui::PopStyleColor(2);
            break;
        }
        
        case TELEPORT_STATE_PAUSED: {
            // Resume button
            ImGui::SetCursorScreenPos(ImVec2(controlsX, controlsY));
            ImGui::PushStyleColor(ImGuiCol_Button, theme_->GetColorVec(ThemeColor::Primary));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme_->GetColorVec(ThemeColor::PrimaryLight));
            ImGui::PushFont(theme_->GetIconFont());
            
            if (ImGui::Button((std::string(ICON_PLAY) + "##Resume" + std::to_string(index)).c_str(),
                              ImVec2(36, 36))) {
                bridge_->ResumeTransfer(transfer.id);
            }
            
            ImGui::PopFont();
            ImGui::PopStyleColor(2);
            
            ImGui::SameLine(0, 10);
            ImGui::TextColored(theme_->GetColorVec(ThemeColor::Warning), "Paused");
            break;
        }
        
        case TELEPORT_STATE_COMPLETE: {
            ImGui::SetCursorScreenPos(ImVec2(controlsX, controlsY));
            ImGui::PushFont(theme_->GetIconFont());
            ImGui::TextColored(theme_->GetColorVec(ThemeColor::Success), ICON_CHECK);
            ImGui::PopFont();
            ImGui::SameLine(0, 10);
            ImGui::TextColored(theme_->GetColorVec(ThemeColor::Success), "Complete");
            break;
        }
        
        case TELEPORT_STATE_FAILED:
        case TELEPORT_STATE_CANCELLED: {
            ImGui::SetCursorScreenPos(ImVec2(controlsX, controlsY));
            ImGui::PushFont(theme_->GetIconFont());
            ImGui::TextColored(theme_->GetColorVec(ThemeColor::Error), ICON_ERROR);
            ImGui::PopFont();
            ImGui::SameLine(0, 10);
            ImGui::TextColored(theme_->GetColorVec(ThemeColor::Error), 
                transfer.state == TELEPORT_STATE_CANCELLED ? "Cancelled" : "Failed");
            break;
        }
        
        default:
            break;
    }
    
    // Reserve space
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y));
    ImGui::Dummy(ImVec2(size.x, 10));
}

void TransfersView::RenderEmptyState() {
    ImVec2 available = ImGui::GetContentRegionAvail();
    ImVec2 center(
        ImGui::GetCursorScreenPos().x + available.x * 0.5f,
        ImGui::GetCursorScreenPos().y + available.y * 0.4f
    );
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Animated circles
    float time = emptyAnim_;
    for (int i = 0; i < 3; i++) {
        float phase = std::fmod(time * 0.8f + i * 1.5f, 4.5f);
        float scale = std::sin(phase * 0.7f) * 0.3f + 0.7f;
        float alpha = std::max(0.0f, 0.2f - phase * 0.04f);
        
        ImU32 color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(0.486f, 0.228f, 0.929f, alpha)
        );
        
        // Draw arrow shapes
        float offset = i * 25.0f;
        drawList->AddTriangleFilled(
            ImVec2(center.x, center.y - 30 - offset * scale),
            ImVec2(center.x - 20 * scale, center.y - offset * scale),
            ImVec2(center.x + 20 * scale, center.y - offset * scale),
            color
        );
    }
    
    // Center icon box
    drawList->AddRectFilled(
        ImVec2(center.x - 35, center.y - 15),
        ImVec2(center.x + 35, center.y + 25),
        theme_->GetColor(ThemeColor::SurfaceLight),
        8.0f
    );
    
    ImGui::PushFont(theme_->GetIconFont());
    ImVec2 iconSize = ImGui::CalcTextSize(ICON_UPLOAD);
    ImGui::SetCursorScreenPos(ImVec2(center.x - 18, center.y - 5));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::Primary), ICON_UPLOAD);
    ImGui::SameLine(0, 4);
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::Accent), ICON_DOWNLOAD);
    ImGui::PopFont();
    
    // Text
    const char* text = "No active transfers";
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImGui::SetCursorScreenPos(ImVec2(center.x - textSize.x * 0.5f, center.y + 50));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s", text);
    
    const char* hint = "Send or receive files to see them here";
    ImVec2 hintSize = ImGui::CalcTextSize(hint);
    ImGui::SetCursorScreenPos(ImVec2(center.x - hintSize.x * 0.5f, center.y + 75));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled), "%s", hint);
}

} // namespace teleport::ui
