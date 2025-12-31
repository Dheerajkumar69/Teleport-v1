/**
 * @file SendView.cpp
 * @brief File sending view with drag & drop
 */

#include "SendView.h"
#include "imgui.h"
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <cmath>
#include <algorithm>

namespace teleport::ui {

// Icons
static const char* ICON_UPLOAD = "\xEE\x89\x8C";    // Upload arrow
static const char* ICON_FILE = "\xEE\x8A\xB0";      // Document
static const char* ICON_FOLDER = "\xEE\x8A\xB8";    // Folder
static const char* ICON_CLOSE = "\xEE\x89\x8A";     // X

SendView::SendView(TeleportBridge* bridge, Theme* theme)
    : bridge_(bridge), theme_(theme) {}

void SendView::Update() {
    // Animate drop zone border
    float targetDrop = isDragging_ ? 1.0f : 0.0f;
    dropZoneAnim_ += (targetDrop - dropZoneAnim_) * 0.2f;
}

void SendView::Render() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 20));
    
    RenderHeader();
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Two-column layout
    ImVec2 available = ImGui::GetContentRegionAvail();
    float leftWidth = available.x * 0.6f - 15;
    float rightWidth = available.x * 0.4f - 15;
    
    // Left: File drop zone and list
    ImGui::BeginChild("##LeftPanel", ImVec2(leftWidth, available.y - 80), false);
    RenderFileDropZone();
    if (!selectedFiles_.empty()) {
        ImGui::Spacing();
        RenderFileList();
    }
    ImGui::EndChild();
    
    ImGui::SameLine(0, 30);
    
    // Right: Device selector
    ImGui::BeginChild("##RightPanel", ImVec2(rightWidth, available.y - 80), false);
    RenderDeviceSelector();
    ImGui::EndChild();
    
    // Bottom: Send button
    RenderSendButton();
    
    ImGui::PopStyleVar();
}

void SendView::RenderHeader() {
    ImGui::PushFont(theme_->GetHeadingFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Send Files");
    ImGui::PopFont();
    
    ImGui::SameLine(0, 20);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), 
        "Select files and choose a device");
}

void SendView::RenderFileDropZone() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, selectedFiles_.empty() ? 250 : 150);
    
    // Background
    ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.1f, 0.1f, 0.115f, 0.6f + dropZoneAnim_ * 0.2f)
    );
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor, Theme::CardRadius);
    
    // Animated dashed border
    ImVec4 borderVec = theme_->GetColorVec(ThemeColor::Primary);
    borderVec.w = 0.4f + dropZoneAnim_ * 0.4f;
    ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(borderVec);
    
    // Draw dashed border using line segments
    float dashLen = 10.0f;
    float gapLen = 8.0f;
    float offset = std::fmod((float)ImGui::GetTime() * 30.0f * dropZoneAnim_, dashLen + gapLen);
    
    auto drawDashedLine = [&](ImVec2 p1, ImVec2 p2) {
        float len = std::sqrt(std::pow(p2.x - p1.x, 2) + std::pow(p2.y - p1.y, 2));
        ImVec2 dir((p2.x - p1.x) / len, (p2.y - p1.y) / len);
        
        float pos = -offset;
        while (pos < len) {
            float start = std::max(0.0f, pos);
            float end = std::min(len, pos + dashLen);
            if (start < end) {
                drawList->AddLine(
                    ImVec2(p1.x + dir.x * start, p1.y + dir.y * start),
                    ImVec2(p1.x + dir.x * end, p1.y + dir.y * end),
                    borderColor, 2.0f
                );
            }
            pos += dashLen + gapLen;
        }
    };
    
    float r = Theme::CardRadius;
    drawDashedLine(ImVec2(pos.x + r, pos.y), ImVec2(pos.x + size.x - r, pos.y));
    drawDashedLine(ImVec2(pos.x + size.x, pos.y + r), ImVec2(pos.x + size.x, pos.y + size.y - r));
    drawDashedLine(ImVec2(pos.x + size.x - r, pos.y + size.y), ImVec2(pos.x + r, pos.y + size.y));
    drawDashedLine(ImVec2(pos.x, pos.y + size.y - r), ImVec2(pos.x, pos.y + r));
    
    // Content
    ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    
    // Upload icon
    ImGui::PushFont(theme_->GetIconFont());
    ImVec2 iconSize = ImGui::CalcTextSize(ICON_UPLOAD);
    float iconScale = 1.5f + dropZoneAnim_ * 0.2f;
    ImGui::SetCursorScreenPos(ImVec2(center.x - iconSize.x * iconScale * 0.5f, center.y - 40));
    
    ImVec4 iconColor = theme_->GetColorVec(ThemeColor::Primary);
    iconColor.w = 0.6f + dropZoneAnim_ * 0.4f;
    ImGui::TextColored(iconColor, ICON_UPLOAD);
    ImGui::PopFont();
    
    // Text
    const char* mainText = isDragging_ ? "Drop files here" : "Drag & drop files here";
    ImVec2 textSize = ImGui::CalcTextSize(mainText);
    ImGui::SetCursorScreenPos(ImVec2(center.x - textSize.x * 0.5f, center.y + 10));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s", mainText);
    
    // Or browse button
    ImGui::SetCursorScreenPos(ImVec2(center.x - 50, center.y + 40));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, theme_->GetColorVec(ThemeColor::Accent));
    
    if (ImGui::Button("or browse files")) {
        // Open file picker
        IFileOpenDialog* pFileOpen;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                       IID_IFileOpenDialog, (void**)&pFileOpen);
        if (SUCCEEDED(hr)) {
            DWORD options;
            pFileOpen->GetOptions(&options);
            pFileOpen->SetOptions(options | FOS_ALLOWMULTISELECT);
            
            hr = pFileOpen->Show(nullptr);
            if (SUCCEEDED(hr)) {
                IShellItemArray* pItems;
                hr = pFileOpen->GetResults(&pItems);
                if (SUCCEEDED(hr)) {
                    DWORD count;
                    pItems->GetCount(&count);
                    for (DWORD i = 0; i < count; i++) {
                        IShellItem* pItem;
                        pItems->GetItemAt(i, &pItem);
                        PWSTR path;
                        pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
                        
                        char pathA[MAX_PATH];
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, MAX_PATH, nullptr, nullptr);
                        selectedFiles_.push_back(pathA);
                        
                        CoTaskMemFree(path);
                        pItem->Release();
                    }
                    pItems->Release();
                }
            }
            pFileOpen->Release();
        }
    }
    
    ImGui::PopStyleColor(3);
    
    // Reserve space
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y));
}

void SendView::RenderFileList() {
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), 
        "%zu file%s selected", selectedFiles_.size(), selectedFiles_.size() == 1 ? "" : "s");
    ImGui::Spacing();
    
    ImGui::BeginChild("##FileList", ImVec2(0, 200), false);
    
    for (size_t i = 0; i < selectedFiles_.size(); i++) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size(ImGui::GetContentRegionAvail().x, 40);
        
        // Background
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            theme_->GetColor(ThemeColor::SurfaceLight), Theme::SmallRadius);
        
        // File icon
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 12, pos.y + 10));
        ImGui::PushFont(theme_->GetIconFont());
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::Accent), ICON_FILE);
        ImGui::PopFont();
        
        // File name
        std::string filename = selectedFiles_[i];
        size_t lastSlash = filename.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            filename = filename.substr(lastSlash + 1);
        }
        
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 45, pos.y + 10));
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s", filename.c_str());
        
        // Remove button
        ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - 35, pos.y + 8));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.1f, 0.1f, 0.5f));
        ImGui::PushFont(theme_->GetIconFont());
        
        std::string btnId = "##Remove" + std::to_string(i);
        if (ImGui::Button((std::string(ICON_CLOSE) + btnId).c_str(), ImVec2(24, 24))) {
            selectedFiles_.erase(selectedFiles_.begin() + i);
            i--;
        }
        
        ImGui::PopFont();
        ImGui::PopStyleColor(2);
        
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 8));
    }
    
    ImGui::EndChild();
}

void SendView::RenderDeviceSelector() {
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "Select Destination");
    ImGui::Spacing();
    ImGui::Spacing();
    
    auto devices = bridge_->GetDevices();
    
    if (devices.empty()) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size(ImGui::GetContentRegionAvail().x, 100);
        
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            theme_->GetColor(ThemeColor::SurfaceLight), Theme::CardRadius);
        
        ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        
        const char* text = "No devices found";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImGui::SetCursorScreenPos(ImVec2(center.x - textSize.x * 0.5f, center.y - 10));
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled), "%s", text);
        
        const char* hint = "Start discovery in Discover tab";
        ImVec2 hintSize = ImGui::CalcTextSize(hint);
        ImGui::SetCursorScreenPos(ImVec2(center.x - hintSize.x * 0.5f, center.y + 10));
        ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled), "%s", hint);
        
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y));
    } else {
        for (const auto& device : devices) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 size(ImGui::GetContentRegionAvail().x, 60);
            
            bool isSelected = (selectedDeviceId_ == device.id);
            
            // Background
            ImU32 bgColor = isSelected 
                ? theme_->GetColor(ThemeColor::Primary)
                : theme_->GetColor(ThemeColor::SurfaceLight);
            
            drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                bgColor, Theme::CardRadius);
            
            // Radio button indicator
            if (isSelected) {
                drawList->AddCircleFilled(ImVec2(pos.x + 20, pos.y + 30), 8,
                    IM_COL32(255, 255, 255, 255));
                drawList->AddCircleFilled(ImVec2(pos.x + 20, pos.y + 30), 4,
                    theme_->GetColor(ThemeColor::Primary));
            } else {
                drawList->AddCircle(ImVec2(pos.x + 20, pos.y + 30), 8,
                    theme_->GetColor(ThemeColor::Border), 16, 2.0f);
            }
            
            // Device name
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 40, pos.y + 12));
            ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s", device.name.c_str());
            
            // IP
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 40, pos.y + 32));
            ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s", device.ip.c_str());
            
            // Clickable area
            ImGui::SetCursorScreenPos(pos);
            if (ImGui::InvisibleButton(("##DeviceBtn" + device.id).c_str(), size)) {
                selectedDeviceId_ = device.id;
            }
            
            ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 10));
        }
    }
}

void SendView::RenderSendButton() {
    bool canSend = !selectedFiles_.empty() && !selectedDeviceId_.empty();
    
    float buttonWidth = 200;
    float buttonHeight = 48;
    ImVec2 available = ImGui::GetContentRegionAvail();
    
    ImGui::SetCursorPos(ImVec2(
        ImGui::GetCursorPosX() + available.x - buttonWidth - 30,
        ImGui::GetCursorPosY()
    ));
    
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 24.0f);
    
    if (canSend) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme_->GetColorVec(ThemeColor::Primary));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme_->GetColorVec(ThemeColor::PrimaryLight));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, theme_->GetColorVec(ThemeColor::SurfaceLight));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme_->GetColorVec(ThemeColor::SurfaceLight));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    }
    
    if (ImGui::Button("Send Files", ImVec2(buttonWidth, buttonHeight)) && canSend) {
        bridge_->SendFiles(selectedDeviceId_, selectedFiles_);
        selectedFiles_.clear();
    }
    
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(canSend ? 1 : 2);
}

void SendView::HandleFileDrop(HDROP hDrop) {
    UINT count = DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);
    
    for (UINT i = 0; i < count; i++) {
        wchar_t pathW[MAX_PATH];
        DragQueryFileW(hDrop, i, pathW, MAX_PATH);
        
        char pathA[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, pathW, -1, pathA, MAX_PATH, nullptr, nullptr);
        selectedFiles_.push_back(pathA);
    }
}

} // namespace teleport::ui
