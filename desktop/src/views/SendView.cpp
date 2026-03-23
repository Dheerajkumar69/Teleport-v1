/**
 * @file SendView.cpp
 * @brief File sending view with lovable micro-interactions
 */

#include "SendView.h"
#include "Icons.h"
#include "imgui.h"

#ifdef _WIN32
#include <shellapi.h>
#include <shobjidl.h>
#include <windows.h>
#elif defined(__APPLE__)
#include "platform/FileDialog_macos.h"
#else
#include "platform/FileDialog_linux.h"
#endif

#include "platform/Sound.h"

#include <algorithm>
#include <cmath>

namespace teleport::ui {

// Confetti colors - vibrant celebration palette
static const unsigned int CONFETTI_COLORS[] = {
    IM_COL32(147, 112, 219, 255), // Purple
    IM_COL32(78, 205, 196, 255),  // Teal
    IM_COL32(255, 230, 109, 255), // Yellow
    IM_COL32(95, 239, 145, 255),  // Green
    IM_COL32(255, 159, 243, 255), // Pink
    IM_COL32(74, 222, 128, 255),  // Bright green
};

// Unicode icons for cross-platform display
static const char *ICON_UPLOAD = Icons::ARROW_UP;
static const char *ICON_FILE = Icons::FILE;
static const char *ICON_FOLDER = Icons::FOLDER;
static const char *ICON_CLOSE = Icons::CANCEL;

SendView::SendView(TeleportBridge *bridge, Theme *theme)
    : bridge_(bridge), theme_(theme) {}

void SendView::Update() {
  float dt = ImGui::GetIO().DeltaTime;

  // Animate drop zone border
  float targetDrop = isDragging_ ? 1.0f : 0.0f;
  dropZoneAnim_ += (targetDrop - dropZoneAnim_) * 0.2f;

  // File drop flash effect
  if (prevFileCount_ < (int)selectedFiles_.size()) {
    fileDropFlash_ = 1.0f;
  }
  prevFileCount_ = (int)selectedFiles_.size();
  fileDropFlash_ *= 0.9f;

  // Send button pulse when ready
  bool canSend = !selectedFiles_.empty() && !selectedDeviceId_.empty();
  sendButtonPulse_ += dt * 3.0f;
  if (sendButtonPulse_ > 6.28f)
    sendButtonPulse_ = 0.0f;

  // Check for transfer completion to trigger celebration
  auto transfers = bridge_->GetTransfers();
  for (const auto &t : transfers) {
    if (t.isSending && t.state == TELEPORT_STATE_COMPLETE) {
      if (lastTransferId_ != t.id) {
        lastTransferId_ = t.id;
        TriggerCelebration();
      }
    }
  }

  // Update celebration particles
  if (celebrating_) {
    celebrationTimer_ += dt;
    UpdateParticles(dt);
    successGlow_ *= 0.97f;

    if (celebrationTimer_ > 3.0f) {
      celebrating_ = false;
      particles_.clear();
    }
  }
}

void SendView::Render() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 20));

  RenderHeader();
  ImGui::Spacing();
  ImGui::Spacing();

  // Show progress bar if transfer active
  RenderProgressBar();

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
  ImGui::BeginChild("##RightPanel", ImVec2(rightWidth, available.y - 80),
                    false);
  RenderDeviceSelector();
  ImGui::EndChild();

  // Bottom: Send button
  RenderSendButton();

  ImGui::PopStyleVar();

  // Celebration overlay on top of everything
  RenderCelebration();
}

void SendView::RenderHeader() {
  ImGui::PushFont(theme_->GetHeadingFont());
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary),
                     "Send Files");
  ImGui::PopFont();

  ImGui::SameLine(0, 20);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                     "Select files and choose a device");
}

void SendView::RenderFileDropZone() {
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size(ImGui::GetContentRegionAvail().x,
              selectedFiles_.empty() ? 250 : 150);

  // Background
  ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(
      ImVec4(0.1f, 0.1f, 0.115f, 0.6f + dropZoneAnim_ * 0.2f));
  drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor,
                          Theme::CardRadius);

  // Animated dashed border
  ImVec4 borderVec = theme_->GetColorVec(ThemeColor::Primary);
  borderVec.w = 0.4f + dropZoneAnim_ * 0.4f;
  ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(borderVec);

  // Draw dashed border using line segments
  float dashLen = 10.0f;
  float gapLen = 8.0f;
  float offset = std::fmod((float)ImGui::GetTime() * 30.0f * dropZoneAnim_,
                           dashLen + gapLen);

  auto drawDashedLine = [&](ImVec2 p1, ImVec2 p2) {
    float len = std::sqrt(std::pow(p2.x - p1.x, 2) + std::pow(p2.y - p1.y, 2));
    ImVec2 dir((p2.x - p1.x) / len, (p2.y - p1.y) / len);

    float pos = -offset;
    while (pos < len) {
      float start = std::max(0.0f, pos);
      float end = std::min(len, pos + dashLen);
      if (start < end) {
        drawList->AddLine(ImVec2(p1.x + dir.x * start, p1.y + dir.y * start),
                          ImVec2(p1.x + dir.x * end, p1.y + dir.y * end),
                          borderColor, 2.0f);
      }
      pos += dashLen + gapLen;
    }
  };

  float r = Theme::CardRadius;
  drawDashedLine(ImVec2(pos.x + r, pos.y), ImVec2(pos.x + size.x - r, pos.y));
  drawDashedLine(ImVec2(pos.x + size.x, pos.y + r),
                 ImVec2(pos.x + size.x, pos.y + size.y - r));
  drawDashedLine(ImVec2(pos.x + size.x - r, pos.y + size.y),
                 ImVec2(pos.x + r, pos.y + size.y));
  drawDashedLine(ImVec2(pos.x, pos.y + size.y - r), ImVec2(pos.x, pos.y + r));

  // Content
  ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);

  // Upload icon
  ImGui::PushFont(theme_->GetIconFont());
  ImVec2 iconSize = ImGui::CalcTextSize(ICON_UPLOAD);
  float iconScale = 1.5f + dropZoneAnim_ * 0.2f;
  ImGui::SetCursorScreenPos(
      ImVec2(center.x - iconSize.x * iconScale * 0.5f, center.y - 40));

  ImVec4 iconColor = theme_->GetColorVec(ThemeColor::Primary);
  iconColor.w = 0.6f + dropZoneAnim_ * 0.4f;
  ImGui::TextColored(iconColor, "%s", ICON_UPLOAD);
  ImGui::PopFont();

  // Text
  const char *mainText =
      isDragging_ ? "Drop files here" : "Drag & drop files here";
  ImVec2 textSize = ImGui::CalcTextSize(mainText);
  ImGui::SetCursorScreenPos(
      ImVec2(center.x - textSize.x * 0.5f, center.y + 10));
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s",
                     mainText);

  // Or browse button
  ImGui::SetCursorScreenPos(ImVec2(center.x - 50, center.y + 40));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, theme_->GetColorVec(ThemeColor::Accent));

  if (ImGui::Button("or browse files")) {
#ifdef _WIN32
    // Open file picker (Windows)
    IFileOpenDialog *pFileOpen;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                  IID_IFileOpenDialog, (void **)&pFileOpen);
    if (SUCCEEDED(hr)) {
      DWORD options;
      pFileOpen->GetOptions(&options);
      pFileOpen->SetOptions(options | FOS_ALLOWMULTISELECT);

      hr = pFileOpen->Show(nullptr);
      if (SUCCEEDED(hr)) {
        IShellItemArray *pItems;
        hr = pFileOpen->GetResults(&pItems);
        if (SUCCEEDED(hr)) {
          DWORD count;
          pItems->GetCount(&count);
          for (DWORD i = 0; i < count; i++) {
            IShellItem *pItem;
            pItems->GetItemAt(i, &pItem);
            PWSTR path;
            pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);

            char pathA[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, MAX_PATH, nullptr,
                                nullptr);
            selectedFiles_.push_back(pathA);

            CoTaskMemFree(path);
            pItem->Release();
          }
          pItems->Release();
        }
      }
      pFileOpen->Release();
    }
#else
    // Open file picker (Linux)
    auto files = OpenFileDialog("Select Files");
    for (const auto &f : files) {
      selectedFiles_.push_back(f);
    }
#endif
  }

  ImGui::PopStyleColor(3);

  // Reserve space
  ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y));
}

void SendView::RenderFileList() {
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                     "%zu file%s selected", selectedFiles_.size(),
                     selectedFiles_.size() == 1 ? "" : "s");
  ImGui::Spacing();

  ImGui::BeginChild("##FileList", ImVec2(0, 200), false);

  for (size_t i = 0; i < selectedFiles_.size(); i++) {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, 40);

    // Background
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                            theme_->GetColor(ThemeColor::SurfaceLight),
                            Theme::SmallRadius);

    // File icon
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 12, pos.y + 10));
    ImGui::PushFont(theme_->GetIconFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::Accent), "%s",
                       ICON_FILE);
    ImGui::PopFont();

    // File name
    std::string filename = selectedFiles_[i];
    size_t lastSlash = filename.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
      filename = filename.substr(lastSlash + 1);
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x + 45, pos.y + 10));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s",
                       filename.c_str());

    // Remove button
    ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - 35, pos.y + 8));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.3f, 0.1f, 0.1f, 0.5f));
    ImGui::PushFont(theme_->GetIconFont());

    std::string btnId = "##Remove" + std::to_string(i);
    if (ImGui::Button((std::string(ICON_CLOSE) + btnId).c_str(),
                      ImVec2(24, 24))) {
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
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                     "Select Destination");
  ImGui::Spacing();
  ImGui::Spacing();

  auto devices = bridge_->GetDevices();

  if (devices.empty()) {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, 100);

    // Pulsing glow for "looking for devices"
    float pulse = 0.3f + 0.1f * std::sin(ImGui::GetTime() * 2.0f);
    ImVec4 bgColor = theme_->GetColorVec(ThemeColor::SurfaceLight);
    bgColor.w = pulse + 0.5f;

    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                            ImGui::ColorConvertFloat4ToU32(bgColor),
                            Theme::CardRadius);

    ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);

    // Animated dots
    float dotAnim = std::fmod(ImGui::GetTime(), 1.5f);
    int dotCount = (int)(dotAnim / 0.5f) + 1;
    std::string dots(dotCount, '.');

    std::string text = "Looking for devices" + dots;
    ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    ImGui::SetCursorScreenPos(
        ImVec2(center.x - textSize.x * 0.5f, center.y - 10));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s",
                       text.c_str());

    const char *hint = "Make sure other devices are on the same network";
    ImVec2 hintSize = ImGui::CalcTextSize(hint);
    ImGui::SetCursorScreenPos(
        ImVec2(center.x - hintSize.x * 0.5f, center.y + 10));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled), "%s",
                       hint);

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y));
  } else {
    int idx = 0;
    for (const auto &device : devices) {
      if (idx >= 16)
        break; // Safety limit

      ImDrawList *drawList = ImGui::GetWindowDrawList();
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 size(ImGui::GetContentRegionAvail().x, 65);

      bool isSelected = (selectedDeviceId_ == device.id);
      bool isHovered = ImGui::IsMouseHoveringRect(
          pos, ImVec2(pos.x + size.x, pos.y + size.y));

      // Animate hover and selection
      float targetHover = isHovered ? 1.0f : 0.0f;
      deviceHoverAnim_[idx] += (targetHover - deviceHoverAnim_[idx]) * 0.2f;

      float targetScale = (isHovered || isSelected) ? 1.0f : 0.0f;
      deviceScaleAnim_[idx] += (targetScale - deviceScaleAnim_[idx]) * 0.15f;

      float targetGlow = isSelected ? 1.0f : 0.0f;
      deviceGlowAnim_[idx] += (targetGlow - deviceGlowAnim_[idx]) * 0.1f;

      // Scale effect (subtle)
      float scale = 1.0f + deviceHoverAnim_[idx] * 0.015f;
      ImVec2 scaledPos(pos.x - (size.x * (scale - 1.0f)) * 0.5f,
                       pos.y - (size.y * (scale - 1.0f)) * 0.5f);
      ImVec2 scaledSize(size.x * scale, size.y * scale);

      // Glow effect for selected
      if (deviceGlowAnim_[idx] > 0.01f) {
        ImU32 glowColor =
            IM_COL32(147, 112, 219, (int)(deviceGlowAnim_[idx] * 60));
        drawList->AddRect(ImVec2(scaledPos.x - 3, scaledPos.y - 3),
                          ImVec2(scaledPos.x + scaledSize.x + 3,
                                 scaledPos.y + scaledSize.y + 3),
                          glowColor, Theme::CardRadius + 3, 0, 4.0f);
      }

      // Background with hover effect
      ImVec4 bgVec = isSelected ? theme_->GetColorVec(ThemeColor::Primary)
                                : theme_->GetColorVec(ThemeColor::SurfaceLight);
      if (!isSelected && deviceHoverAnim_[idx] > 0.01f) {
        bgVec.x += 0.05f * deviceHoverAnim_[idx];
        bgVec.y += 0.05f * deviceHoverAnim_[idx];
        bgVec.z += 0.08f * deviceHoverAnim_[idx];
      }

      drawList->AddRectFilled(
          scaledPos,
          ImVec2(scaledPos.x + scaledSize.x, scaledPos.y + scaledSize.y),
          ImGui::ColorConvertFloat4ToU32(bgVec), Theme::CardRadius);

      // Radio button indicator with animation
      float radioX = scaledPos.x + 20;
      float radioY = scaledPos.y + scaledSize.y * 0.5f;

      if (isSelected) {
        // Animated fill
        float fillScale = 0.8f + 0.2f * deviceGlowAnim_[idx];
        drawList->AddCircleFilled(ImVec2(radioX, radioY), 9 * fillScale,
                                  IM_COL32(255, 255, 255, 255));
        drawList->AddCircleFilled(ImVec2(radioX, radioY), 4 * fillScale,
                                  theme_->GetColor(ThemeColor::Primary));
      } else {
        drawList->AddCircle(ImVec2(radioX, radioY), 8,
                            theme_->GetColor(ThemeColor::Border), 16, 2.0f);
      }

      // Device name (slightly brighter on hover)
      ImVec4 nameColor = theme_->GetColorVec(ThemeColor::TextPrimary);
      if (deviceHoverAnim_[idx] > 0.01f) {
        nameColor.x =
            std::min(1.0f, nameColor.x + 0.2f * deviceHoverAnim_[idx]);
        nameColor.y =
            std::min(1.0f, nameColor.y + 0.2f * deviceHoverAnim_[idx]);
        nameColor.z =
            std::min(1.0f, nameColor.z + 0.2f * deviceHoverAnim_[idx]);
      }
      ImGui::SetCursorScreenPos(ImVec2(scaledPos.x + 42, scaledPos.y + 14));
      ImGui::TextColored(nameColor, "%s", device.name.c_str());

      // IP address
      ImGui::SetCursorScreenPos(ImVec2(scaledPos.x + 42, scaledPos.y + 36));
      ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s",
                         device.ip.c_str());

      // Clickable area
      ImGui::SetCursorScreenPos(pos);
      if (ImGui::InvisibleButton(("##DeviceBtn" + device.id).c_str(), size)) {
        selectedDeviceId_ = device.id;
      }

      ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 12));
      idx++;
    }
  }
}

void SendView::RenderSendButton() {
  bool canSend = !selectedFiles_.empty() && !selectedDeviceId_.empty();

  float buttonWidth = 200;
  float buttonHeight = 48;
  ImVec2 available = ImGui::GetContentRegionAvail();

  ImGui::SetCursorPos(
      ImVec2(ImGui::GetCursorPosX() + available.x - buttonWidth - 30,
             ImGui::GetCursorPosY()));

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 24.0f);

  if (canSend) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          theme_->GetColorVec(ThemeColor::Primary));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          theme_->GetColorVec(ThemeColor::PrimaryLight));
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          theme_->GetColorVec(ThemeColor::SurfaceLight));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          theme_->GetColorVec(ThemeColor::SurfaceLight));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
  }

  if (ImGui::Button("Send Files", ImVec2(buttonWidth, buttonHeight)) &&
      canSend) {
    bridge_->SendFiles(selectedDeviceId_, selectedFiles_);
    selectedFiles_.clear();
  }

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(canSend ? 1 : 2);
}

#ifdef _WIN32
void SendView::HandleFileDrop(HDROP hDrop) {
  UINT count = DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);

  for (UINT i = 0; i < count; i++) {
    wchar_t pathW[MAX_PATH];
    DragQueryFileW(hDrop, i, pathW, MAX_PATH);

    char pathA[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, pathW, -1, pathA, MAX_PATH, nullptr,
                        nullptr);
    selectedFiles_.push_back(pathA);
  }
}
#endif

// ============ Celebration Effects ============

void SendView::TriggerCelebration() {
  celebrating_ = true;
  celebrationTimer_ = 0.0f;
  successGlow_ = 1.0f;

  // Play success sound
  PlaySuccessSound();

  // Spawn confetti particles
  particles_.clear();
  std::uniform_real_distribution<float> xDist(0.0f,
                                              ImGui::GetIO().DisplaySize.x);
  std::uniform_real_distribution<float> vxDist(-150.0f, 150.0f);
  std::uniform_real_distribution<float> vyDist(-500.0f, -250.0f);
  std::uniform_real_distribution<float> sizeDist(6.0f, 14.0f);
  std::uniform_real_distribution<float> rotDist(0.0f, 6.28f);
  std::uniform_int_distribution<int> colorDist(0, 5);

  for (int i = 0; i < 100; i++) {
    SendParticle p;
    p.x = xDist(rng_);
    p.y = ImGui::GetIO().DisplaySize.y + 50.0f;
    p.vx = vxDist(rng_);
    p.vy = vyDist(rng_);
    p.life = 1.0f;
    p.size = sizeDist(rng_);
    p.rotation = rotDist(rng_);
    p.color = CONFETTI_COLORS[colorDist(rng_)];
    particles_.push_back(p);
  }
}

void SendView::UpdateParticles(float dt) {
  for (auto &p : particles_) {
    // Gravity
    p.vy += 600.0f * dt;

    // Air resistance
    p.vx *= 0.99f;

    // Movement
    p.x += p.vx * dt;
    p.y += p.vy * dt;

    // Spin
    p.rotation += p.vx * 0.01f * dt;

    // Fade out based on position and time
    if (p.y > ImGui::GetIO().DisplaySize.y * 0.7f) {
      p.life -= dt * 0.8f;
    }
  }

  // Remove dead particles
  particles_.erase(
      std::remove_if(particles_.begin(), particles_.end(),
                     [](const SendParticle &p) { return p.life <= 0; }),
      particles_.end());
}

void SendView::RenderCelebration() {
  if (!celebrating_ && particles_.empty())
    return;

  ImDrawList *drawList = ImGui::GetForegroundDrawList();

  // Success glow overlay
  if (successGlow_ > 0.01f) {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImU32 glowColor = IM_COL32(100, 255, 150, (int)(successGlow_ * 40));
    drawList->AddRectFilled(ImVec2(0, 0), displaySize, glowColor);
  }

  // Draw confetti particles
  for (const auto &p : particles_) {
    ImU32 color = (p.color & 0x00FFFFFF) | ((int)(p.life * 255) << 24);

    // Draw as rotated squares
    float c = std::cos(p.rotation);
    float s = std::sin(p.rotation);
    float hs = p.size * 0.5f;

    ImVec2 corners[4] = {
        ImVec2(p.x + (-hs * c - -hs * s), p.y + (-hs * s + -hs * c)),
        ImVec2(p.x + (hs * c - -hs * s), p.y + (hs * s + -hs * c)),
        ImVec2(p.x + (hs * c - hs * s), p.y + (hs * s + hs * c)),
        ImVec2(p.x + (-hs * c - hs * s), p.y + (-hs * s + hs * c))};

    drawList->AddQuadFilled(corners[0], corners[1], corners[2], corners[3],
                            color);
  }
}

void SendView::PlaySuccessSound() { ::teleport::ui::PlaySuccessSound(); }

void SendView::RenderProgressBar() {
  auto transfers = bridge_->GetTransfers();

  for (const auto &t : transfers) {
    if (!t.isSending || t.state != TELEPORT_STATE_TRANSFERRING)
      continue;

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 8.0f;

    // Background track
    drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                            theme_->GetColor(ThemeColor::SurfaceLight), 4.0f);

    // Progress fill with gradient
    float progress =
        (float)t.bytesTransferred / std::max(t.bytesTotal, (uint64_t)1);
    ImVec4 startColor = theme_->GetColorVec(ThemeColor::Primary);
    ImVec4 endColor = theme_->GetColorVec(ThemeColor::Accent);

    drawList->AddRectFilledMultiColor(
        pos, ImVec2(pos.x + width * progress, pos.y + height),
        ImGui::ColorConvertFloat4ToU32(startColor),
        ImGui::ColorConvertFloat4ToU32(endColor),
        ImGui::ColorConvertFloat4ToU32(endColor),
        ImGui::ColorConvertFloat4ToU32(startColor));

    // Glow effect
    float glowIntensity = 0.3f + 0.2f * std::sin(ImGui::GetTime() * 4.0f);
    ImU32 glowColor = IM_COL32(147, 112, 219, (int)(glowIntensity * 100));
    drawList->AddRect(ImVec2(pos.x - 2, pos.y - 2),
                      ImVec2(pos.x + width * progress + 2, pos.y + height + 2),
                      glowColor, 6.0f, 0, 3.0f);

    ImGui::Dummy(ImVec2(width, height + 8));
    break; // Only show first active transfer
  }
}

} // namespace teleport::ui
