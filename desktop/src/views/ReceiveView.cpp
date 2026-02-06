/**
 * @file ReceiveView.cpp
 * @brief File receiving view with celebrations
 */

#include "ReceiveView.h"
#include "Icons.h"
#include "imgui.h"
#include <teleport/teleport.h>

#ifdef _WIN32
#include <shobjidl.h>
#include <windows.h>
#else
#include "platform/FileDialog_linux.h"
#endif

#include "platform/Sound.h"

#include <algorithm>
#include <cmath>

// Local Lerp function for animations
static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

namespace teleport::ui {

// Confetti colors for celebration
static const unsigned int RECEIVE_CONFETTI_COLORS[] = {
    IM_COL32(78, 205, 196, 255),  // Teal
    IM_COL32(255, 230, 109, 255), // Yellow
    IM_COL32(95, 239, 145, 255),  // Green
    IM_COL32(147, 112, 219, 255), // Purple
    IM_COL32(255, 159, 243, 255), // Pink
    IM_COL32(16, 185, 129, 255),  // Success green
};

// Unicode icons for cross-platform display
static const char *ICON_DOWNLOAD = Icons::ARROW_DOWN;
static const char *ICON_FOLDER = Icons::FOLDER;
static const char *ICON_CHECK = Icons::CHECK;

ReceiveView::ReceiveView(TeleportBridge *bridge, Theme *theme)
    : bridge_(bridge), theme_(theme) {
  downloadPath_ = bridge_->GetDownloadPath();
}

void ReceiveView::Update() {
  float dt = ImGui::GetIO().DeltaTime;

  // Animate toggle
  float targetToggle = bridge_->IsReceiving() ? 1.0f : 0.0f;
  toggleAnim_ += (targetToggle - toggleAnim_) * 0.15f;

  // Pulse when active
  if (bridge_->IsReceiving()) {
    pulseAnim_ += 0.05f;
    if (pulseAnim_ > 6.28f)
      pulseAnim_ = 0.0f;
  }

  // Check for receive completion to trigger celebration
  auto transfers = bridge_->GetTransfers();
  for (const auto &t : transfers) {
    if (!t.isSending && t.state == TELEPORT_STATE_COMPLETE) {
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

void ReceiveView::Render() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 20));

  RenderHeader();
  ImGui::Spacing();
  ImGui::Spacing();

  // Show progress if receiving
  RenderProgressBar();

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

  // Celebration overlay
  RenderCelebration();
}

void ReceiveView::RenderHeader() {
  ImGui::PushFont(theme_->GetHeadingFont());
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary),
                     "Receive Files");
  ImGui::PopFont();

  ImGui::SameLine(0, 20);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                     "Accept files from other devices");
}

void ReceiveView::RenderProgressBar() {
  auto transfers = bridge_->GetTransfers();

  for (const auto &t : transfers) {
    if (t.isSending || t.state != TELEPORT_STATE_TRANSFERRING)
      continue;

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 12.0f;

    // Background track
    drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                            theme_->GetColor(ThemeColor::SurfaceLight), 6.0f);

    // Progress calculation
    float progress =
        (float)t.bytesTransferred / std::max(t.bytesTotal, (uint64_t)1);

    // Progress fill with success green gradient
    ImVec4 startColor(0.063f, 0.725f, 0.506f, 1.0f); // Success green
    ImVec4 endColor(0.2f, 0.85f, 0.6f, 1.0f);        // Lighter green

    drawList->AddRectFilledMultiColor(
        pos, ImVec2(pos.x + width * progress, pos.y + height),
        ImGui::ColorConvertFloat4ToU32(startColor),
        ImGui::ColorConvertFloat4ToU32(endColor),
        ImGui::ColorConvertFloat4ToU32(endColor),
        ImGui::ColorConvertFloat4ToU32(startColor));

    // Glow effect
    float glowIntensity = 0.3f + 0.2f * std::sin(ImGui::GetTime() * 4.0f);
    ImU32 glowColor = IM_COL32(16, 185, 129, (int)(glowIntensity * 100));
    drawList->AddRect(ImVec2(pos.x - 2, pos.y - 2),
                      ImVec2(pos.x + width * progress + 2, pos.y + height + 2),
                      glowColor, 8.0f, 0, 3.0f);

    // Show percentage
    char progressText[32];
    snprintf(progressText, sizeof(progressText), "Receiving... %.0f%%",
             progress * 100);
    ImGui::Dummy(ImVec2(width, height + 5));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s",
                       progressText);
    ImGui::Spacing();

    break; // Only show first active transfer
  }
}

void ReceiveView::RenderStatus() {
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size(ImGui::GetContentRegionAvail().x, 120);

  // Background card
  drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          theme_->GetColor(ThemeColor::Card),
                          Theme::CardRadius);

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
      ImU32 glowColor =
          ImGui::ColorConvertFloat4ToU32(ImVec4(0.063f, 0.725f, 0.506f, alpha));
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
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", ICON_DOWNLOAD);
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
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled), "%s",
                       ICON_DOWNLOAD);
    ImGui::PopFont();
  }

  // Status text
  ImGui::SetCursorScreenPos(ImVec2(pos.x + 120, pos.y + 35));
  ImGui::PushFont(theme_->GetBodyFont());

  if (isReceiving) {
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::Success),
                       "Ready to receive");
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 120, pos.y + 60));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                       "Waiting for incoming files...");
  } else {
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                       "Receiving disabled");
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 120, pos.y + 60));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled),
                       "Enable to accept files from other devices");
  }

  ImGui::PopFont();

  ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 10));
}

void ReceiveView::RenderFolderSelector() {
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size(ImGui::GetContentRegionAvail().x, 70);

  // Background
  drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          theme_->GetColor(ThemeColor::SurfaceLight),
                          Theme::CardRadius);

  // Folder icon
  ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 20));
  ImGui::PushFont(theme_->GetIconFont());
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::Accent), "%s",
                     ICON_FOLDER);
  ImGui::PopFont();

  // Label
  ImGui::SetCursorScreenPos(ImVec2(pos.x + 55, pos.y + 12));
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                     "Download folder");

  // Path
  ImGui::SetCursorScreenPos(ImVec2(pos.x + 55, pos.y + 32));

  // Truncate path if too long
  std::string displayPath = downloadPath_;
  if (displayPath.length() > 50) {
    displayPath = "..." + displayPath.substr(displayPath.length() - 47);
  }
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s",
                     displayPath.c_str());

  // Browse button
  ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - 100, pos.y + 20));
  ImGui::PushStyleColor(ImGuiCol_Button,
                        theme_->GetColorVec(ThemeColor::Surface));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        theme_->GetColorVec(ThemeColor::Card));

  if (ImGui::Button("Browse", ImVec2(80, 30))) {
#ifdef _WIN32
    IFileDialog *pDialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                  IID_IFileOpenDialog, (void **)&pDialog);
    if (SUCCEEDED(hr)) {
      DWORD options;
      pDialog->GetOptions(&options);
      pDialog->SetOptions(options | FOS_PICKFOLDERS);

      hr = pDialog->Show(nullptr);
      if (SUCCEEDED(hr)) {
        IShellItem *pItem;
        hr = pDialog->GetResult(&pItem);
        if (SUCCEEDED(hr)) {
          PWSTR path;
          pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);

          char pathA[MAX_PATH];
          WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, MAX_PATH, nullptr,
                              nullptr);
          downloadPath_ = pathA;
          bridge_->SetDownloadPath(downloadPath_);

          CoTaskMemFree(path);
          pItem->Release();
        }
      }
      pDialog->Release();
    }
#else
    auto path = SelectFolderDialog("Select Download Folder");
    if (!path.empty()) {
      downloadPath_ = path;
      bridge_->SetDownloadPath(downloadPath_);
    }
#endif
  }

  ImGui::PopStyleColor(2);

  ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 10));
}

void ReceiveView::RenderToggle() {
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  bool isReceiving = bridge_->IsReceiving();

  // Toggle switch dimensions
  float switchWidth = 60;
  float switchHeight = 32;
  float knobRadius = 12;

  ImVec2 pos = ImGui::GetCursorScreenPos();

  // Label
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary),
                     "Enable Receiving");

  ImGui::SameLine(ImGui::GetContentRegionAvail().x - switchWidth - 30);
  ImVec2 switchPos = ImGui::GetCursorScreenPos();

  // Switch background
  float knobX =
      switchPos.x + 6 + toggleAnim_ * (switchWidth - 2 * knobRadius - 8);

  ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(
      ImVec4(Lerp(0.2f, 0.063f, toggleAnim_), Lerp(0.2f, 0.725f, toggleAnim_),
             Lerp(0.22f, 0.506f, toggleAnim_), 1.0f));

  drawList->AddRectFilled(
      switchPos, ImVec2(switchPos.x + switchWidth, switchPos.y + switchHeight),
      bgColor, switchHeight * 0.5f);

  // Knob shadow
  drawList->AddCircleFilled(
      ImVec2(knobX + knobRadius + 2, switchPos.y + switchHeight * 0.5f + 2),
      knobRadius, IM_COL32(0, 0, 0, 40));

  // Knob
  drawList->AddCircleFilled(
      ImVec2(knobX + knobRadius, switchPos.y + switchHeight * 0.5f), knobRadius,
      IM_COL32(255, 255, 255, 255));

  // Clickable area
  ImGui::SetCursorScreenPos(switchPos);
  if (ImGui::InvisibleButton("##ReceiveToggle",
                             ImVec2(switchWidth, switchHeight))) {
    if (isReceiving) {
      bridge_->StopReceiving();
    } else {
      bridge_->StartReceiving(downloadPath_);
    }
  }
}

void ReceiveView::RenderIncomingDialog() {
  // Semi-transparent overlay
  ImDrawList *drawList = ImGui::GetForegroundDrawList();
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;

  drawList->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(0, 0, 0, 180));

  // Dialog box
  ImVec2 dialogSize(450, 350);
  ImVec2 dialogPos((displaySize.x - dialogSize.x) * 0.5f,
                   (displaySize.y - dialogSize.y) * 0.5f);

  drawList->AddRectFilled(
      dialogPos, ImVec2(dialogPos.x + dialogSize.x, dialogPos.y + dialogSize.y),
      theme_->GetColor(ThemeColor::Surface), Theme::CardRadius);

  // Dialog content
  auto request = bridge_->GetPendingRequest();

  // Header
  ImGui::SetCursorScreenPos(ImVec2(dialogPos.x + 30, dialogPos.y + 25));
  ImGui::PushFont(theme_->GetHeadingFont());
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary),
                     "Incoming Transfer");
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

  for (const auto &[name, size] : request.files) {
    char sizeStr[32];
    teleport_format_bytes(size, sizeStr, sizeof(sizeStr));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s",
                       name.c_str());
    ImGui::SameLine(dialogSize.x - 120);
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s",
                       sizeStr);
  }

  ImGui::EndChild();

  // Total size
  char totalStr[32];
  teleport_format_bytes(request.totalSize, totalStr, sizeof(totalStr));
  ImGui::SetCursorScreenPos(ImVec2(dialogPos.x + 30, dialogPos.y + 255));
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                     "Total size:");
  ImGui::SameLine();
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s",
                     totalStr);

  // Buttons
  float buttonWidth = 120;
  float buttonHeight = 40;
  float buttonY = dialogPos.y + dialogSize.y - 60;

  // Reject button
  ImGui::SetCursorScreenPos(ImVec2(dialogPos.x + 30, buttonY));
  ImGui::PushStyleColor(ImGuiCol_Button,
                        theme_->GetColorVec(ThemeColor::SurfaceLight));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.3f, 0.15f, 0.15f, 0.8f));

  if (ImGui::Button("Reject", ImVec2(buttonWidth, buttonHeight))) {
    bridge_->RejectPendingRequest();
  }

  ImGui::PopStyleColor(2);

  // Accept button
  ImGui::SetCursorScreenPos(
      ImVec2(dialogPos.x + dialogSize.x - buttonWidth - 30, buttonY));
  ImGui::PushStyleColor(ImGuiCol_Button,
                        theme_->GetColorVec(ThemeColor::Success));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.1f, 0.8f, 0.55f, 1.0f));

  if (ImGui::Button("Accept", ImVec2(buttonWidth, buttonHeight))) {
    bridge_->AcceptPendingRequest();
  }

  ImGui::PopStyleColor(2);
}

// ============ Celebration Effects ============

void ReceiveView::TriggerCelebration() {
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
    ReceiveParticle p;
    p.x = xDist(rng_);
    p.y = ImGui::GetIO().DisplaySize.y + 50.0f;
    p.vx = vxDist(rng_);
    p.vy = vyDist(rng_);
    p.life = 1.0f;
    p.size = sizeDist(rng_);
    p.rotation = rotDist(rng_);
    p.color = RECEIVE_CONFETTI_COLORS[colorDist(rng_)];
    particles_.push_back(p);
  }
}

void ReceiveView::UpdateParticles(float dt) {
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

    // Fade out
    if (p.y > ImGui::GetIO().DisplaySize.y * 0.7f) {
      p.life -= dt * 0.8f;
    }
  }

  // Remove dead particles
  particles_.erase(
      std::remove_if(particles_.begin(), particles_.end(),
                     [](const ReceiveParticle &p) { return p.life <= 0; }),
      particles_.end());
}

void ReceiveView::RenderCelebration() {
  if (!celebrating_ && particles_.empty())
    return;

  ImDrawList *drawList = ImGui::GetForegroundDrawList();

  // Success glow overlay (green tint for receive)
  if (successGlow_ > 0.01f) {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImU32 glowColor = IM_COL32(16, 185, 129, (int)(successGlow_ * 40));
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

void ReceiveView::PlaySuccessSound() { ::teleport::ui::PlaySuccessSound(); }

} // namespace teleport::ui
