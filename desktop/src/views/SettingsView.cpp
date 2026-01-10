/**
 * @file SettingsView.cpp
 * @brief Application settings implementation
 */

#include "SettingsView.h"
#include "imgui.h"
#include <windows.h>
#include <shobjidl.h>
#include <cmath>

namespace teleport::ui {

SettingsView::SettingsView(TeleportBridge* bridge, Theme* theme)
    : bridge_(bridge), theme_(theme) {
    downloadPath_ = bridge_->GetDownloadPath();
    
    // Get computer name as default device name
    DWORD size = sizeof(deviceName_);
    GetComputerNameA(deviceName_, &size);
    
    LoadSettings();
}

void SettingsView::Update() {
    float dt = ImGui::GetIO().DeltaTime;
    
    // Animate toggles
    float targets[4] = {
        darkMode_ ? 1.0f : 0.0f,
        autoStart_ ? 1.0f : 0.0f,
        showNotifications_ ? 1.0f : 0.0f,
        0.0f
    };
    
    for (int i = 0; i < 4; i++) {
        toggleAnim_[i] += (targets[i] - toggleAnim_[i]) * dt * 10.0f;
    }
}

void SettingsView::Render() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 20));
    
    RenderHeader();
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Scrollable content area
    ImGui::BeginChild("##SettingsContent", ImVec2(0, 0), false);
    
    RenderDeviceSettings();
    ImGui::Spacing();
    ImGui::Spacing();
    
    RenderTransferSettings();
    ImGui::Spacing();
    ImGui::Spacing();
    
    RenderAppearanceSettings();
    ImGui::Spacing();
    ImGui::Spacing();
    
    RenderAbout();
    
    ImGui::EndChild();
    
    ImGui::PopStyleVar();
}

void SettingsView::RenderHeader() {
    ImGui::PushFont(theme_->GetHeadingFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Settings");
    ImGui::PopFont();
    
    ImGui::SameLine(0, 20);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), 
        "Configure Teleport");
}

void SettingsView::RenderDeviceSettings() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, 100);
    
    // Section background
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        theme_->GetColor(ThemeColor::Card), Theme::CardRadius);
    
    // Section title
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 15));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "DEVICE");
    
    // Device name input
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 45));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Device Name");
    
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 200, pos.y + 42));
    ImGui::PushItemWidth(size.x - 240);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme_->GetColorVec(ThemeColor::SurfaceLight));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    
    if (ImGui::InputText("##DeviceName", deviceName_, sizeof(deviceName_))) {
        bridge_->SetDeviceName(deviceName_);
        SaveSettings();
    }
    
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopItemWidth();
    
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 15));
}

void SettingsView::RenderTransferSettings() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, 140);
    
    // Section background
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        theme_->GetColor(ThemeColor::Card), Theme::CardRadius);
    
    // Section title
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 15));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "TRANSFERS");
    
    // Download folder
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 45));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Download Folder");
    
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 200, pos.y + 42));
    
    // Truncate path for display
    std::string displayPath = downloadPath_;
    if (displayPath.length() > 35) {
        displayPath = "..." + displayPath.substr(displayPath.length() - 32);
    }
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s", displayPath.c_str());
    
    // Browse button
    ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - 100, pos.y + 42));
    ImGui::PushStyleColor(ImGuiCol_Button, theme_->GetColorVec(ThemeColor::Primary));
    if (ImGui::Button("Browse##Download", ImVec2(80, 28))) {
        IFileDialog* pDialog;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                       IID_IFileOpenDialog, (void**)&pDialog);
        if (SUCCEEDED(hr)) {
            DWORD options;
            pDialog->GetOptions(&options);
            pDialog->SetOptions(options | FOS_PICKFOLDERS);
            
            if (SUCCEEDED(pDialog->Show(nullptr))) {
                IShellItem* pItem;
                if (SUCCEEDED(pDialog->GetResult(&pItem))) {
                    PWSTR path;
                    pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
                    
                    char pathA[MAX_PATH];
                    WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, MAX_PATH, nullptr, nullptr);
                    downloadPath_ = pathA;
                    bridge_->SetDownloadPath(downloadPath_);
                    SaveSettings();
                    
                    CoTaskMemFree(path);
                    pItem->Release();
                }
            }
            pDialog->Release();
        }
    }
    ImGui::PopStyleColor();
    
    // Notifications toggle
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 90));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Show Notifications");
    
    // Toggle switch
    float switchWidth = 50, switchHeight = 26;
    ImVec2 switchPos(pos.x + size.x - switchWidth - 20, pos.y + 88);
    float knobRadius = 10;
    float knobX = switchPos.x + 4 + toggleAnim_[2] * (switchWidth - 2 * knobRadius - 4);
    
    ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
        0.2f + 0.4f * toggleAnim_[2],
        0.2f + 0.5f * toggleAnim_[2],
        0.2f + 0.3f * toggleAnim_[2],
        1.0f
    ));
    
    drawList->AddRectFilled(switchPos, 
        ImVec2(switchPos.x + switchWidth, switchPos.y + switchHeight),
        bgColor, switchHeight * 0.5f);
    drawList->AddCircleFilled(
        ImVec2(knobX + knobRadius, switchPos.y + switchHeight * 0.5f),
        knobRadius, IM_COL32(255, 255, 255, 255));
    
    ImGui::SetCursorScreenPos(switchPos);
    if (ImGui::InvisibleButton("##NotifToggle", ImVec2(switchWidth, switchHeight))) {
        showNotifications_ = !showNotifications_;
        SaveSettings();
    }
    
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 15));
}

void SettingsView::RenderAppearanceSettings() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, 100);
    
    // Section background
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        theme_->GetColor(ThemeColor::Card), Theme::CardRadius);
    
    // Section title
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 15));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "APPEARANCE");
    
    // Dark mode toggle
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 50));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Dark Mode");
    
    // Toggle switch
    float switchWidth = 50, switchHeight = 26;
    ImVec2 switchPos(pos.x + size.x - switchWidth - 20, pos.y + 48);
    float knobRadius = 10;
    float knobX = switchPos.x + 4 + toggleAnim_[0] * (switchWidth - 2 * knobRadius - 4);
    
    ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
        0.2f + 0.4f * toggleAnim_[0],
        0.2f + 0.5f * toggleAnim_[0],
        0.2f + 0.3f * toggleAnim_[0],
        1.0f
    ));
    
    drawList->AddRectFilled(switchPos, 
        ImVec2(switchPos.x + switchWidth, switchPos.y + switchHeight),
        bgColor, switchHeight * 0.5f);
    drawList->AddCircleFilled(
        ImVec2(knobX + knobRadius, switchPos.y + switchHeight * 0.5f),
        knobRadius, IM_COL32(255, 255, 255, 255));
    
    ImGui::SetCursorScreenPos(switchPos);
    if (ImGui::InvisibleButton("##DarkModeToggle", ImVec2(switchWidth, switchHeight))) {
        darkMode_ = !darkMode_;
        // TODO: Apply theme change
        SaveSettings();
    }
    
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 15));
}

void SettingsView::RenderAbout() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, 120);
    
    // Section background
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        theme_->GetColor(ThemeColor::Card), Theme::CardRadius);
    
    // Section title
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 15));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "ABOUT");
    
    // App name and version
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 45));
    ImGui::PushFont(theme_->GetHeadingFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::Primary), "Teleport");
    ImGui::PopFont();
    
    ImGui::SameLine();
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "v1.0.0");
    
    // Description
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 75));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), 
        "Fast, secure file transfer across devices");
    
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 15));
}

void SettingsView::SaveSettings() {
    // TODO: Save to config file
    // For now, settings are session-only
}

void SettingsView::LoadSettings() {
    // TODO: Load from config file
    // For now, use defaults
}

} // namespace teleport::ui
