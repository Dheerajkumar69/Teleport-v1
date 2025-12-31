/**
 * @file ReceiveView.cpp
 * @brief File receiving view with folder selector and status
 */

#include "ReceiveView.h"
#include "imgui.h"
#include <teleport/teleport.h>
#include <windows.h>
#include <shobjidl.h>
#include <cmath>

// Local Lerp function for animations
static inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

namespace teleport::ui {

// Icons
static const char* ICON_DOWNLOAD = "\xEE\x89\x96";  // Download
static const char* ICON_FOLDER = "\xEE\x8A\xB8";    // Folder
static const char* ICON_CHECK = "\xEE\x89\xBE";     // Checkmark

ReceiveView::ReceiveView(TeleportBridge* bridge, Theme* theme)
    : bridge_(bridge), theme_(theme) {
    downloadPath_ = bridge_->GetDownloadPath();
}

void ReceiveView::Update() {
    // Animate toggle
    float targetToggle = bridge_->IsReceiving() ? 1.0f : 0.0f;
    toggleAnim_ += (targetToggle - toggleAnim_) * 0.15f;
    
    // Pulse when active
    if (bridge_->IsReceiving()) {
        pulseAnim_ += 0.05f;
        if (pulseAnim_ > 6.28f) pulseAnim_ = 0.0f;
    }
}

void ReceiveView::Render() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 20));
    
    RenderHeader();
    ImGui::Spacing();
    ImGui::Spacing();
    RenderStatus();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    RenderFolderSelector();
    ImGui::Spacing();
    ImGui::Spacing();
    RenderToggle();
    
    // Check for incoming request dialog
    if (bridge_->HasPendingRequest()) {
        RenderIncomingDialog();
    }
    
    ImGui::PopStyleVar();
}

void ReceiveView::RenderHeader() {
    ImGui::PushFont(theme_->GetHeadingFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Receive Files");
    ImGui::PopFont();
    
    ImGui::SameLine(0, 20);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), 
        "Accept files from other devices");
}

void ReceiveView::RenderStatus() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, 120);
    
    // Background card
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        theme_->GetColor(ThemeColor::Card), Theme::CardRadius);
    
    bool isReceiving = bridge_->IsReceiving();
    
    // Status indicator circle
    ImVec2 circleCenter(pos.x + 60, pos.y + 60);
    float circleRadius = 30;
    
    if (isReceiving) {
        // Animated glow rings
        float pulse = (std::sin(pulseAnim_) + 1.0f) * 0.5f;
        for (int i = 3; i > 0; i--) {
            float r = circleRadius + i * 8 + pulse * 5;
            float alpha = 0.15f - i * 0.04f;
            ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(
                ImVec4(0.063f, 0.725f, 0.506f, alpha)
            );
            drawList->AddCircleFilled(circleCenter, r, glowColor, 48);
        }
        
        // Main circle
        drawList->AddCircleFilled(circleCenter, circleRadius, 
            theme_->GetColor(ThemeColor::Success), 48);
        
        // Download icon
        ImGui::PushFont(theme_->GetIconFont());
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_DOWNLOAD);
        ImGui::SetCursorScreenPos(ImVec2(circleCenter.x - iconSize.x * 0.5f, 
                                          circleCenter.y - iconSize.y * 0.5f));
        ImGui::TextColored(ImVec4(1, 1, 1, 1), ICON_DOWNLOAD);
        ImGui::PopFont();
    } else {
        // Inactive state
        drawList->AddCircleFilled(circleCenter, circleRadius,
            theme_->GetColor(ThemeColor::SurfaceLight), 48);
        drawList->AddCircle(circleCenter, circleRadius,
            theme_->GetColor(ThemeColor::Border), 48, 2.0f);
        
        // Icon
        ImGui::PushFont(theme_->GetIconFont());
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_DOWNLOAD);
        ImGui::SetCursorScreenPos(ImVec2(circleCenter.x - iconSize.x * 0.5f,
                                          circleCenter.y - iconSize.y * 0.5f));
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled), ICON_DOWNLOAD);
        ImGui::PopFont();
    }
    
    // Status text
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 120, pos.y + 35));
    ImGui::PushFont(theme_->GetBodyFont());
    
    if (isReceiving) {
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::Success), "Ready to receive");
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 120, pos.y + 60));
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), 
            "Waiting for incoming files...");
    } else {
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "Receiving disabled");
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 120, pos.y + 60));
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled),
            "Enable to accept files from other devices");
    }
    
    ImGui::PopFont();
    
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 10));
}

void ReceiveView::RenderFolderSelector() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, 70);
    
    // Background
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        theme_->GetColor(ThemeColor::SurfaceLight), Theme::CardRadius);
    
    // Folder icon
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 20));
    ImGui::PushFont(theme_->GetIconFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::Accent), ICON_FOLDER);
    ImGui::PopFont();
    
    // Label
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 55, pos.y + 12));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "Download folder");
    
    // Path
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 55, pos.y + 32));
    
    // Truncate path if too long
    std::string displayPath = downloadPath_;
    if (displayPath.length() > 50) {
        displayPath = "..." + displayPath.substr(displayPath.length() - 47);
    }
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s", displayPath.c_str());
    
    // Browse button
    ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - 100, pos.y + 20));
    ImGui::PushStyleColor(ImGuiCol_Button, theme_->GetColorVec(ThemeColor::Surface));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme_->GetColorVec(ThemeColor::Card));
    
    if (ImGui::Button("Browse", ImVec2(80, 30))) {
        IFileDialog* pDialog;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                       IID_IFileOpenDialog, (void**)&pDialog);
        if (SUCCEEDED(hr)) {
            DWORD options;
            pDialog->GetOptions(&options);
            pDialog->SetOptions(options | FOS_PICKFOLDERS);
            
            hr = pDialog->Show(nullptr);
            if (SUCCEEDED(hr)) {
                IShellItem* pItem;
                hr = pDialog->GetResult(&pItem);
                if (SUCCEEDED(hr)) {
                    PWSTR path;
                    pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
                    
                    char pathA[MAX_PATH];
                    WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, MAX_PATH, nullptr, nullptr);
                    downloadPath_ = pathA;
                    bridge_->SetDownloadPath(downloadPath_);
                    
                    CoTaskMemFree(path);
                    pItem->Release();
                }
            }
            pDialog->Release();
        }
    }
    
    ImGui::PopStyleColor(2);
    
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 10));
}

void ReceiveView::RenderToggle() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    bool isReceiving = bridge_->IsReceiving();
    
    // Toggle switch dimensions
    float switchWidth = 60;
    float switchHeight = 32;
    float knobRadius = 12;
    
    ImVec2 pos = ImGui::GetCursorScreenPos();
    
    // Label
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Enable Receiving");
    
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - switchWidth - 30);
    ImVec2 switchPos = ImGui::GetCursorScreenPos();
    
    // Switch background
    float knobX = switchPos.x + 6 + toggleAnim_ * (switchWidth - 2 * knobRadius - 8);
    
    ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(
            Lerp(0.2f, 0.063f, toggleAnim_),
            Lerp(0.2f, 0.725f, toggleAnim_),
            Lerp(0.22f, 0.506f, toggleAnim_),
            1.0f
        )
    );
    
    drawList->AddRectFilled(
        switchPos,
        ImVec2(switchPos.x + switchWidth, switchPos.y + switchHeight),
        bgColor,
        switchHeight * 0.5f
    );
    
    // Knob shadow
    drawList->AddCircleFilled(
        ImVec2(knobX + knobRadius + 2, switchPos.y + switchHeight * 0.5f + 2),
        knobRadius,
        IM_COL32(0, 0, 0, 40)
    );
    
    // Knob
    drawList->AddCircleFilled(
        ImVec2(knobX + knobRadius, switchPos.y + switchHeight * 0.5f),
        knobRadius,
        IM_COL32(255, 255, 255, 255)
    );
    
    // Clickable area
    ImGui::SetCursorScreenPos(switchPos);
    if (ImGui::InvisibleButton("##ReceiveToggle", ImVec2(switchWidth, switchHeight))) {
        if (isReceiving) {
            bridge_->StopReceiving();
        } else {
            bridge_->StartReceiving(downloadPath_);
        }
    }
}

void ReceiveView::RenderIncomingDialog() {
    // Semi-transparent overlay
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    
    drawList->AddRectFilled(
        ImVec2(0, 0), displaySize,
        IM_COL32(0, 0, 0, 180)
    );
    
    // Dialog box
    ImVec2 dialogSize(450, 350);
    ImVec2 dialogPos(
        (displaySize.x - dialogSize.x) * 0.5f,
        (displaySize.y - dialogSize.y) * 0.5f
    );
    
    drawList->AddRectFilled(dialogPos, 
        ImVec2(dialogPos.x + dialogSize.x, dialogPos.y + dialogSize.y),
        theme_->GetColor(ThemeColor::Surface),
        Theme::CardRadius
    );
    
    // Dialog content
    auto request = bridge_->GetPendingRequest();
    
    // Header
    ImGui::SetCursorScreenPos(ImVec2(dialogPos.x + 30, dialogPos.y + 25));
    ImGui::PushFont(theme_->GetHeadingFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Incoming Transfer");
    ImGui::PopFont();
    
    // Sender info
    ImGui::SetCursorScreenPos(ImVec2(dialogPos.x + 30, dialogPos.y + 70));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "From:");
    ImGui::SameLine();
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s", 
        request.sender.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled), "(%s)", 
        request.sender.ip.c_str());
    
    // File list
    ImGui::SetCursorScreenPos(ImVec2(dialogPos.x + 30, dialogPos.y + 100));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), 
        "%zu file(s):", request.files.size());
    
    ImGui::SetCursorScreenPos(ImVec2(dialogPos.x + 30, dialogPos.y + 125));
    ImGui::BeginChild("##IncomingFiles", ImVec2(dialogSize.x - 60, 120), false);
    
    for (const auto& [name, size] : request.files) {
        char sizeStr[32];
        teleport_format_bytes(size, sizeStr, sizeof(sizeStr));
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s", name.c_str());
        ImGui::SameLine(dialogSize.x - 120);
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s", sizeStr);
    }
    
    ImGui::EndChild();
    
    // Total size
    char totalStr[32];
    teleport_format_bytes(request.totalSize, totalStr, sizeof(totalStr));
    ImGui::SetCursorScreenPos(ImVec2(dialogPos.x + 30, dialogPos.y + 255));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "Total size:");
    ImGui::SameLine();
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s", totalStr);
    
    // Buttons
    float buttonWidth = 120;
    float buttonHeight = 40;
    float buttonY = dialogPos.y + dialogSize.y - 60;
    
    // Reject button
    ImGui::SetCursorScreenPos(ImVec2(dialogPos.x + 30, buttonY));
    ImGui::PushStyleColor(ImGuiCol_Button, theme_->GetColorVec(ThemeColor::SurfaceLight));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.15f, 0.15f, 0.8f));
    
    if (ImGui::Button("Reject", ImVec2(buttonWidth, buttonHeight))) {
        bridge_->RejectPendingRequest();
    }
    
    ImGui::PopStyleColor(2);
    
    // Accept button
    ImGui::SetCursorScreenPos(ImVec2(dialogPos.x + dialogSize.x - buttonWidth - 30, buttonY));
    ImGui::PushStyleColor(ImGuiCol_Button, theme_->GetColorVec(ThemeColor::Success));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.8f, 0.55f, 1.0f));
    
    if (ImGui::Button("Accept", ImVec2(buttonWidth, buttonHeight))) {
        bridge_->AcceptPendingRequest();
    }
    
    ImGui::PopStyleColor(2);
}

} // namespace teleport::ui
