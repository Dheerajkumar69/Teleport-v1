/**
 * @file DiscoverView.cpp
 * @brief Device discovery view with delightful animations
 */

#include "DiscoverView.h"
#include "../../../core/src/pairing/third_party/qrcodegen.hpp"
#include "components/DeviceCard.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace teleport::ui {

// OS Text Labels (fallback when icon fonts unavailable)
static const char *OS_LABEL_WINDOWS = "W";
static const char *OS_LABEL_ANDROID = "A";
static const char *OS_LABEL_MACOS = "M";
static const char *OS_LABEL_LINUX = "L";
static const char *OS_LABEL_UNKNOWN = "?";

// Celebration colors - vibrant confetti palette
static const ImU32 CONFETTI_COLORS[] = {
    IM_COL32(255, 107, 107, 255), // Coral
    IM_COL32(78, 205, 196, 255),  // Teal
    IM_COL32(255, 230, 109, 255), // Yellow
    IM_COL32(170, 111, 217, 255), // Purple
    IM_COL32(95, 239, 145, 255),  // Green
    IM_COL32(255, 159, 243, 255), // Pink
};

DiscoverView::DiscoverView(TeleportBridge *bridge, Theme *theme)
    : bridge_(bridge), theme_(theme) {}

void DiscoverView::TriggerCelebration() {
  celebrating_ = true;
  celebrationTimer_ = 0.0f;
  successGlow_ = 1.0f;

  // Spawn confetti particles
  particles_.clear();
  std::uniform_real_distribution<float> xDist(0.0f,
                                              ImGui::GetIO().DisplaySize.x);
  std::uniform_real_distribution<float> vxDist(-100.0f, 100.0f);
  std::uniform_real_distribution<float> vyDist(-400.0f, -200.0f);
  std::uniform_real_distribution<float> sizeDist(4.0f, 12.0f);
  std::uniform_int_distribution<int> colorDist(0, 5);

  for (int i = 0; i < 80; i++) {
    Particle p;
    p.x = xDist(rng_);
    p.y = ImGui::GetIO().DisplaySize.y + 50.0f;
    p.vx = vxDist(rng_);
    p.vy = vyDist(rng_);
    p.life = 1.0f;
    p.size = sizeDist(rng_);
    p.color = CONFETTI_COLORS[colorDist(rng_)];
    particles_.push_back(p);
  }
}

void DiscoverView::UpdateParticles(float dt) {
  for (auto &p : particles_) {
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    p.vy += 600.0f * dt; // Gravity
    p.life -= dt * 0.4f;
    p.vx *= 0.99f; // Drag
  }

  // Remove dead particles
  particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                  [](const Particle &p) {
                                    return p.life <= 0 || p.y > 2000;
                                  }),
                   particles_.end());

  if (particles_.empty()) {
    celebrating_ = false;
  }
}

void DiscoverView::RenderCelebration() {
  if (!celebrating_ && particles_.empty())
    return;

  ImDrawList *drawList = ImGui::GetForegroundDrawList();

  for (const auto &p : particles_) {
    float alpha = std::min(1.0f, p.life * 2.0f);
    ImU32 color =
        (p.color & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 255) << 24);

    // Draw as rotated rectangles for confetti effect
    float angle = p.x * 0.05f + p.y * 0.03f;
    float s = sin(angle) * p.size * 0.5f;
    float c = cos(angle) * p.size * 0.5f;

    ImVec2 points[4] = {
        ImVec2(p.x - c - s, p.y - s + c),
        ImVec2(p.x + c - s, p.y + s + c),
        ImVec2(p.x + c + s, p.y + s - c),
        ImVec2(p.x - c + s, p.y - s - c),
    };
    drawList->AddConvexPolyFilled(points, 4, color);
  }
}

void DiscoverView::Update() {
  float dt = ImGui::GetIO().DeltaTime;

  // Update pulse animation (smooth sine wave)
  pulseAnimation_ += dt * 3.0f;
  if (pulseAnimation_ > 6.28f)
    pulseAnimation_ = 0.0f;

  // Update empty state animation
  if (bridge_->GetDevices().empty()) {
    emptyStateAnim_ += dt * 1.5f;
  }

  // Update celebration particles
  if (celebrating_) {
    celebrationTimer_ += dt;
    UpdateParticles(dt);
  }

  // Decay success glow smoothly
  if (successGlow_ > 0) {
    successGlow_ -= dt * 0.8f;
    if (successGlow_ < 0)
      successGlow_ = 0;
  }

  // Decay modal fade in
  if (showQrModal_ || showHotspotModal_) {
    if (modalFadeIn_ < 1.0f) {
      modalFadeIn_ += dt * 6.0f;
      if (modalFadeIn_ > 1.0f)
        modalFadeIn_ = 1.0f;
    }
  } else {
    modalFadeIn_ = 0.0f;
  }
}

void DiscoverView::Render() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 20));

  // Subtle success glow overlay
  if (successGlow_ > 0.01f) {
    ImDrawList *bgList = ImGui::GetBackgroundDrawList();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.2f, 0.9f, 0.4f, successGlow_ * 0.15f));
    bgList->AddRectFilled(ImVec2(0, 0), displaySize, glowColor);
  }

  RenderHeader();
  ImGui::Spacing();
  ImGui::Spacing();
  RenderConnectionMethods();
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

  // Render modals
  RenderQrModal();
  RenderHotspotModal();
  RenderManualConnectModal();

  // Render celebration on top
  RenderCelebration();
}

void DiscoverView::RenderHeader() {
  ImGui::PushFont(theme_->GetHeadingFont());
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary),
                     "Discover Devices");
  ImGui::PopFont();
}

void DiscoverView::RenderStatusBar() {
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();

  // Status indicator
  bool isDiscovering = bridge_->IsDiscovering();

  // Background pill
  float pillWidth = isDiscovering ? 140.0f : 120.0f;
  drawList->AddRectFilled(pos, ImVec2(pos.x + pillWidth, pos.y + 32),
                          theme_->GetColor(ThemeColor::SurfaceLight), 16.0f);

  // Animated dot
  float dotRadius = 4.0f;
  ImVec2 dotCenter(pos.x + 16, pos.y + 16);

  if (isDiscovering) {
    // Pulsing glow
    float pulse = (std::sin(pulseAnimation_) + 1.0f) * 0.5f;
    ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.063f, 0.725f, 0.506f, 0.3f + pulse * 0.3f));
    drawList->AddCircleFilled(dotCenter, dotRadius + 4 + pulse * 4, glowColor);
    drawList->AddCircleFilled(dotCenter, dotRadius,
                              theme_->GetColor(ThemeColor::Success));
  } else {
    drawList->AddCircleFilled(dotCenter, dotRadius,
                              theme_->GetColor(ThemeColor::TextDisabled));
  }

  // Status text
  ImGui::SetCursorScreenPos(ImVec2(pos.x + 28, pos.y + 7));
  ImGui::TextColored(isDiscovering
                         ? theme_->GetColorVec(ThemeColor::Success)
                         : theme_->GetColorVec(ThemeColor::TextSecondary),
                     isDiscovering ? "Scanning..." : "Paused");

  // Toggle button
  ImGui::SameLine(0, 30);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 7);

  ImGui::PushStyleColor(ImGuiCol_Button,
                        isDiscovering
                            ? theme_->GetColorVec(ThemeColor::SurfaceLight)
                            : theme_->GetColorVec(ThemeColor::Primary));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        isDiscovering
                            ? ImVec4(0.2f, 0.2f, 0.22f, 0.9f)
                            : theme_->GetColorVec(ThemeColor::PrimaryLight));
  ImGui::PushStyleColor(ImGuiCol_Text,
                        theme_->GetColorVec(ThemeColor::TextPrimary));

  if (ImGui::Button(isDiscovering ? "  Stop  " : "  Start Discovery  ",
                    ImVec2(0, 32))) {
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
                       "%zu device%s found", devices.size(),
                       devices.size() == 1 ? "" : "s");
  }

  // Web connection status with connect button
  ImGui::SameLine(0, 30);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY());

  bool webConnected = bridge_->IsWebSignalingConnected();
  ImVec4 webStatusColor = webConnected
                              ? ImVec4(0.2f, 0.7f, 0.5f, 1.0f)
                              : ImVec4(0.5f, 0.5f, 0.55f, 1.0f);

  // Pulsing dot for connected state
  ImDrawList *statusDL = ImGui::GetWindowDrawList();
  ImVec2 statusPos = ImGui::GetCursorScreenPos();
  if (webConnected) {
    float pulse = (std::sin(pulseAnimation_) + 1.0f) * 0.5f;
    ImU32 glowC = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.2f, 0.7f, 0.5f, 0.3f + pulse * 0.2f));
    statusDL->AddCircleFilled(ImVec2(statusPos.x + 6, statusPos.y + 8),
                              7.0f, glowC);
  }
  statusDL->AddCircleFilled(ImVec2(statusPos.x + 6, statusPos.y + 8), 4.0f,
                            ImGui::ColorConvertFloat4ToU32(webStatusColor));

  ImGui::Dummy(ImVec2(16, 0));
  ImGui::SameLine();
  ImGui::TextColored(webStatusColor, webConnected ? "Online" : "Offline");

  // Connect/disconnect button
  ImGui::SameLine(0, 10);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 7);

  if (webConnected) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.3f, 0.25f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.3f, 0.4f, 0.35f, 0.9f));
    if (ImGui::Button("Disconnect##web", ImVec2(0, 28))) {
      bridge_->DisconnectFromWebSignaling();
    }
    ImGui::PopStyleColor(2);
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(0.15f, 0.35f, 0.55f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.2f, 0.45f, 0.7f, 0.9f));
    const bool isConnecting = bridge_->IsSignalingConnecting();
    if (isConnecting) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.55f, 0.5f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.35f, 0.55f, 0.5f));
      ImGui::Button("Connecting...##web", ImVec2(0, 28));
      ImGui::PopStyleColor(2);
    } else {
      if (ImGui::Button("Go Online##web", ImVec2(0, 28))) {
        bridge_->TriggerReconnect();
      }
    }
    ImGui::PopStyleColor(2);
  }

  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);
}

void DiscoverView::RenderDeviceGrid() {
  auto devices = bridge_->GetDevices();
  constexpr size_t kMaxAnimatedCards = 32;
  const size_t renderCount = std::min(devices.size(), kMaxAnimatedCards);

  // Calculate grid layout
  float availableWidth = ImGui::GetContentRegionAvail().x - 30;
  float cardWidth = 280.0f;
  float cardSpacing = 20.0f;
  int columns = std::max(
      1, (int)((availableWidth + cardSpacing) / (cardWidth + cardSpacing)));

  ImGui::BeginChild("##DeviceGrid", ImVec2(0, 0), false,
                    ImGuiWindowFlags_NoBackground);

  int col = 0;
  for (size_t i = 0; i < renderCount; i++) {
    if (col > 0) {
      ImGui::SameLine(0, cardSpacing);
    }

    RenderDeviceCard(devices[i], (int)i);

    col++;
    if (col >= columns) {
      col = 0;
    }
  }

  if (devices.size() > renderCount) {
    ImGui::Spacing();
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                       "+%zu more devices hidden (UI limit %zu)",
                       devices.size() - renderCount, kMaxAnimatedCards);
  }

  ImGui::EndChild();
}

void DiscoverView::RenderDeviceCard(const DeviceInfo &device, int index) {
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 cardPos = ImGui::GetCursorScreenPos();
  const ImVec2 cardSize(280.0f, 148.0f);
  ImVec2 cardEnd(cardPos.x + cardSize.x, cardPos.y + cardSize.y);

  // Hover animation
  bool isHovered = ImGui::IsMouseHoveringRect(cardPos, cardEnd);
  float targetHover = isHovered ? 1.0f : 0.0f;
  cardHoverAnim_[index] += (targetHover - cardHoverAnim_[index]) * 0.18f;
  float hov = cardHoverAnim_[index];

  // ── Platform label & avatar colour ───────────────────────────────────────
  const char *osLabel;
  const char *typeLabel;
  ImVec4 accentColor = theme_->GetColorVec(ThemeColor::Primary); // default purple

  if (device.isWeb) {
    osLabel  = "W";
    typeLabel = "Web Browser";
    accentColor = ImVec4(0.18f, 0.56f, 0.94f, 1.0f);
  } else if (device.os.find("Win") != std::string::npos) {
    osLabel  = "W";
    typeLabel = "Windows";
    accentColor = ImVec4(0.0f, 0.47f, 0.84f, 1.0f);
  } else if (device.os == "Android") {
    osLabel  = "A";
    typeLabel = "Android";
    accentColor = ImVec4(0.40f, 0.73f, 0.27f, 1.0f);
  } else if (device.os == "macOS" || device.os == "Darwin") {
    osLabel  = "M";
    typeLabel = "macOS";
    accentColor = ImVec4(0.70f, 0.70f, 0.72f, 1.0f);
  } else if (device.os == "iOS") {
    osLabel  = "i";
    typeLabel = "iOS";
    accentColor = ImVec4(0.70f, 0.70f, 0.72f, 1.0f);
  } else if (device.os.find("inux") != std::string::npos ||
             device.os.find("linux") != std::string::npos) {
    osLabel  = "L";
    typeLabel = "Linux";
    accentColor = ImVec4(0.90f, 0.49f, 0.13f, 1.0f);
  } else {
    osLabel  = "?";
    typeLabel = device.os.empty() ? "Unknown" : device.os.c_str();
  }

  // ── Card background ───────────────────────────────────────────────────────
  ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(
      ImVec4(0.11f + hov * 0.02f, 0.11f + hov * 0.02f,
             0.13f + hov * 0.02f, 0.95f));
  drawList->AddRectFilled(cardPos, cardEnd, bgCol, Theme::CardRadius);

  // Border: dim grey → primary purple on hover
  ImVec4 borderBase(0.22f, 0.22f, 0.26f, 0.7f);
  ImVec4 primVec = theme_->GetColorVec(ThemeColor::Primary);
  ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
      borderBase.x + (primVec.x - borderBase.x) * hov,
      borderBase.y + (primVec.y - borderBase.y) * hov,
      borderBase.z + (primVec.z - borderBase.z) * hov,
      borderBase.w + (1.0f - borderBase.w) * hov));
  drawList->AddRect(cardPos, cardEnd, borderCol, Theme::CardRadius, 0, 1.4f);

  // Subtle purple glow on hover
  if (hov > 0.02f) {
    ImU32 glowCol = ImGui::ColorConvertFloat4ToU32(
        ImVec4(primVec.x, primVec.y, primVec.z, 0.15f * hov));
    for (int g = 1; g <= 3; g++) {
      float off = (float)g * 2.5f;
      drawList->AddRect(
          ImVec2(cardPos.x - off, cardPos.y - off),
          ImVec2(cardEnd.x + off, cardEnd.y + off),
          glowCol, Theme::CardRadius + off, 0, 1.5f);
    }
  }

  // ── Avatar circle ─────────────────────────────────────────────────────────
  const float BADGE_R = 22.0f;
  ImVec2 badgeCenter(cardPos.x + 22.0f + BADGE_R, cardPos.y + 20.0f + BADGE_R);

  drawList->AddCircleFilled(badgeCenter, BADGE_R,
      ImGui::ColorConvertFloat4ToU32(
          ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.18f)));
  drawList->AddCircle(badgeCenter, BADGE_R,
      ImGui::ColorConvertFloat4ToU32(
          ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.55f)),
      32, 1.5f);

  ImGui::PushFont(theme_->GetHeadingFont());
  ImVec2 lsz = ImGui::CalcTextSize(osLabel);
  ImGui::SetCursorScreenPos(ImVec2(badgeCenter.x - lsz.x * 0.5f,
                                   badgeCenter.y - lsz.y * 0.5f));
  ImGui::TextColored(accentColor, "%s", osLabel);
  ImGui::PopFont();

  // ── Device name ───────────────────────────────────────────────────────────
  const float TX = cardPos.x + 22.0f + BADGE_R * 2.0f + 14.0f;

  ImGui::PushFont(theme_->GetHeadingFont());
  ImGui::SetCursorScreenPos(ImVec2(TX, cardPos.y + 24.0f));
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s",
                     device.name.empty() ? "Unknown Device"
                                         : device.name.c_str());
  ImGui::PopFont();

  // ── Type subtitle ─────────────────────────────────────────────────────────
  ImGui::PushFont(theme_->GetBodyFont());
  ImGui::SetCursorScreenPos(ImVec2(TX, cardPos.y + 48.0f));
  ImGui::TextColored(ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.8f),
                     "%s", typeLabel);
  ImGui::PopFont();

  // ── Thin divider ──────────────────────────────────────────────────────────
  float divY = cardPos.y + 84.0f;
  drawList->AddLine(ImVec2(cardPos.x + 16.0f, divY),
                    ImVec2(cardEnd.x - 16.0f, divY),
                    IM_COL32(55, 55, 68, 160), 1.0f);

  // ── Connection info ───────────────────────────────────────────────────────
  ImGui::PushFont(theme_->GetBodyFont());
  ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 18.0f, divY + 10.0f));

  if (device.isWeb) {
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                       "Via:");
    ImGui::SameLine(0, 6);
    ImGui::TextColored(ImVec4(0.2f, 0.75f, 0.55f, 1.0f), "Cloud Relay");
  } else {
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "IP:");
    ImGui::SameLine(0, 6);
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s",
                       device.ip.empty() ? "N/A" : device.ip.c_str());
    if (device.port > 0) {
      ImGui::SameLine(0, 14);
      ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                         "Port:");
      ImGui::SameLine(0, 6);
      ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary),
                         "%d", device.port);
    }
  }
  ImGui::PopFont();

  // ── Send button (right-aligned, small, themed) ────────────────────────────
  const float BTN_W = 80.0f, BTN_H = 28.0f;
  const float BTN_X = cardEnd.x - BTN_W - 14.0f;
  const float BTN_Y = cardEnd.y - BTN_H - 12.0f;

  ImGui::SetCursorScreenPos(ImVec2(BTN_X, BTN_Y));
  ImGui::PushStyleColor(ImGuiCol_Button,
                        theme_->GetColorVec(ThemeColor::Primary));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        theme_->GetColorVec(ThemeColor::PrimaryLight));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        theme_->GetColorVec(ThemeColor::Primary));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

  std::string btnId = "Send##card" + std::to_string(index);
  if (ImGui::Button(btnId.c_str(), ImVec2(BTN_W, BTN_H))) {
    selectedDeviceId_    = device.id;
    sendRequestDeviceId_ = device.id;
  }

  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);

  // Reserve card space
  ImGui::SetCursorScreenPos(cardPos);
  ImGui::Dummy(cardSize);
}


void DiscoverView::RenderEmptyState() {
  ImVec2 available = ImGui::GetContentRegionAvail();
  ImVec2 center(ImGui::GetCursorScreenPos().x + available.x * 0.5f,
                ImGui::GetCursorScreenPos().y + available.y * 0.4f);

  ImDrawList *drawList = ImGui::GetWindowDrawList();

  // Animated radar circles
  float time = emptyStateAnim_;
  for (int i = 0; i < 3; i++) {
    float phase = std::fmod(time + i * 2.0f, 6.0f);
    float radius = 30 + phase * 25;
    float alpha = std::max(0.0f, 1.0f - phase / 6.0f) * 0.3f;

    ImU32 circleColor =
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.486f, 0.228f, 0.929f, alpha));
    drawList->AddCircle(center, radius, circleColor, 64, 2.0f);
  }

  // Center dot (instead of icon font which may not be available)
  drawList->AddCircleFilled(center, 12, theme_->GetColor(ThemeColor::Primary));
  drawList->AddCircle(center, 18, theme_->GetColor(ThemeColor::PrimaryLight),
                      32, 2.0f);

  // Text
  const char *text = bridge_->IsDiscovering()
                         ? "Scanning for devices..."
                         : "Start discovery to find devices";
  ImVec2 textSize = ImGui::CalcTextSize(text);
  ImGui::SetCursorScreenPos(
      ImVec2(center.x - textSize.x * 0.5f, center.y + 60));
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s",
                     text);

  // Help text
  const char *helpText = "Devices on the same network will appear here";
  ImVec2 helpSize = ImGui::CalcTextSize(helpText);
  ImGui::SetCursorScreenPos(
      ImVec2(center.x - helpSize.x * 0.5f, center.y + 85));
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextDisabled), "%s",
                     helpText);
}

void DiscoverView::RenderConnectionMethods() {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

  // Section header - simplified
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                     "Alternative methods:");
  ImGui::SameLine(0, 15);

  // QR Code button - primary alternative
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.486f, 0.228f, 0.929f, 0.8f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.586f, 0.328f, 1.0f, 0.9f));
  if (ImGui::Button("  QR Code  ", ImVec2(100, 32))) {
    if (bridge_->GenerateQrPairing(qrExpirySeconds_)) {
      qrImageData_ = bridge_->GetQrImageData();
      auto info = bridge_->GetQrPairingInfo();
      qrSessionToken_ = info.session_token;
    }
    showQrModal_ = true;
  }
  ImGui::PopStyleColor(2);

  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Scan with phone to connect instantly");
  }

  ImGui::SameLine(0, 8);

  // Hotspot button - sync state
  hotspotActive_ = bridge_->IsHotspotActive();
  if (hotspotActive_) {
    auto info = bridge_->GetHotspotInfo();
    hotspotSsid_ = info.ssid;
    hotspotPassword_ = info.password;
    hotspotGatewayIp_ = info.gateway_ip;
  }

  ImGui::PushStyleColor(ImGuiCol_Button,
                        hotspotActive_ ? ImVec4(0.063f, 0.725f, 0.506f, 0.8f)
                                       : ImVec4(0.2f, 0.2f, 0.22f, 0.8f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        hotspotActive_ ? ImVec4(0.1f, 0.8f, 0.56f, 0.9f)
                                       : ImVec4(0.3f, 0.3f, 0.32f, 0.9f));

  const char *hotspotLabel = hotspotActive_ ? " Hotspot ON " : " Hotspot ";
  if (ImGui::Button(hotspotLabel, ImVec2(110, 32))) {
    showHotspotModal_ = true;
  }
  ImGui::PopStyleColor(2);

  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(hotspotActive_ ? "View hotspot details"
                                     : "No WiFi? Create a hotspot");
  }

  ImGui::SameLine(0, 8);

  // Manual IP button - direct connection
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.22f, 0.8f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.3f, 0.3f, 0.32f, 0.9f));
  if (ImGui::Button(" Manual IP ", ImVec2(100, 32))) {
    showManualConnectModal_ = true;
    modalFadeIn_ = 0.0f;
  }
  ImGui::PopStyleColor(2);

  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Connect by entering IP address");
  }

  // Note: WiFi Direct requires separate DLL built with MSVC
  // Button shown as disabled until wifi_direct.dll is present

  ImGui::PopStyleVar();
}

void DiscoverView::RenderQrModal() {
  if (!showQrModal_)
    return;

  ImGui::SetNextWindowSize(ImVec2(420, 480), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.1f, 0.98f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(25, 20));

  if (ImGui::Begin("Pair with Phone", &showQrModal_,
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {

    // Simple centered title
    float titleWidth = ImGui::CalcTextSize("Scan with Teleport app").x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - titleWidth) / 2);
    ImGui::PushFont(theme_->GetHeadingFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary),
                       "Scan with Teleport app");
    ImGui::PopFont();

    ImGui::Spacing();
    ImGui::Spacing();

    // Get QR pairing info from bridge
    auto qrInfo = bridge_->GetQrPairingInfo();

    // Generate QR code data string (JSON format)
    std::ostringstream oss;
    oss << "{\"ip\":\"" << qrInfo.ip << "\",\"port\":" << qrInfo.port
        << ",\"token\":\"" << qrInfo.session_token << "\",\"device\":\""
        << qrInfo.device_name << "\"}";
    std::string qrData = oss.str();

    // Generate QR code
    try {
      qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
          qrData.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
      int qrSize = qr.getSize();

      // Calculate rendering dimensions
      float displaySize = 260.0f;
      float cellSize = displaySize / (qrSize + 8); // Add quiet zone
      float totalSize = cellSize * (qrSize + 8);

      ImVec2 qrPos = ImGui::GetCursorScreenPos();
      qrPos.x += (ImGui::GetContentRegionAvail().x - totalSize) * 0.5f;

      ImDrawList *drawList = ImGui::GetWindowDrawList();

      // White background with rounded corners
      drawList->AddRectFilled(qrPos,
                              ImVec2(qrPos.x + totalSize, qrPos.y + totalSize),
                              IM_COL32(255, 255, 255, 255), 8.0f);

      // Draw QR modules
      float offset = cellSize * 4; // Quiet zone offset
      for (int y = 0; y < qrSize; y++) {
        for (int x = 0; x < qrSize; x++) {
          if (qr.getModule(x, y)) {
            ImVec2 cellPos(qrPos.x + offset + x * cellSize,
                           qrPos.y + offset + y * cellSize);
            drawList->AddRectFilled(
                cellPos, ImVec2(cellPos.x + cellSize, cellPos.y + cellSize),
                IM_COL32(0, 0, 0, 255));
          }
        }
      }

      ImGui::Dummy(ImVec2(totalSize, totalSize + 10));

    } catch (...) {
      // Fallback: show error message
      float qrSize = 220.0f;
      ImVec2 qrPos = ImGui::GetCursorScreenPos();
      qrPos.x += (ImGui::GetContentRegionAvail().x - qrSize) * 0.5f;

      ImDrawList *drawList = ImGui::GetWindowDrawList();
      drawList->AddRectFilled(qrPos, ImVec2(qrPos.x + qrSize, qrPos.y + qrSize),
                              IM_COL32(50, 50, 60, 255), 8.0f);

      drawList->AddText(ImVec2(qrPos.x + 30, qrPos.y + qrSize * 0.45f),
                        IM_COL32(200, 100, 100, 255),
                        "Click 'Generate' to create QR");

      ImGui::Dummy(ImVec2(qrSize, qrSize + 10));
    }

    ImGui::Spacing();

    // Simple instruction
    const char *helpText = "Open Teleport on your phone and tap 'Scan QR'";
    float helpWidth = ImGui::CalcTextSize(helpText).x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - helpWidth) / 2);
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s",
                       helpText);

    ImGui::Spacing();
    ImGui::Spacing();

    // Generate button
    float buttonWidth = 140.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          theme_->GetColorVec(ThemeColor::Primary));
    if (ImGui::Button("Generate QR", ImVec2(buttonWidth, 40))) {
      qrExpirySeconds_ = 300;
      bridge_->GenerateQrPairing(qrExpirySeconds_);
      qrImageData_ = bridge_->GetQrImageData();
    }
    ImGui::PopStyleColor();
  }
  ImGui::End();

  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void DiscoverView::RenderHotspotModal() {
  if (!showHotspotModal_)
    return;

  ImGui::SetNextWindowSize(ImVec2(380, 280), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.1f, 0.98f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(25, 20));

  if (ImGui::Begin("Direct Connect", &showHotspotModal_,
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {

    if (hotspotActive_) {
      // Active state - show connection info prominently
      float titleWidth = ImGui::CalcTextSize("Hotspot Ready!").x;
      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - titleWidth) / 2);
      ImGui::PushFont(theme_->GetHeadingFont());
      ImGui::TextColored(ImVec4(0.063f, 0.725f, 0.506f, 1.0f),
                         "Hotspot Ready!");
      ImGui::PopFont();

      ImGui::Spacing();
      ImGui::Spacing();

      // Connection info - simple layout
      ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                         "WiFi Name:");
      ImGui::SameLine(120);
      ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s",
                         hotspotSsid_.empty() ? "Teleport"
                                              : hotspotSsid_.c_str());

      ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                         "Password:");
      ImGui::SameLine(120);
      ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "%s",
                         hotspotPassword_.empty() ? "********"
                                                  : hotspotPassword_.c_str());

      ImGui::Spacing();
      ImGui::Spacing();

      // Simple instruction
      const char *helpText = "Connect your phone to this network";
      float helpWidth = ImGui::CalcTextSize(helpText).x;
      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - helpWidth) / 2);
      ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s",
                         helpText);

      ImGui::Spacing();
      ImGui::Spacing();

      // Stop button
      float buttonWidth = 120.0f;
      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.15f, 0.8f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(0.6f, 0.2f, 0.2f, 0.9f));
      if (ImGui::Button("Turn Off", ImVec2(buttonWidth, 36))) {
        bridge_->StopHotspot();
        hotspotActive_ = false;
        hotspotSsid_.clear();
        hotspotPassword_.clear();
        hotspotGatewayIp_.clear();
      }
      ImGui::PopStyleColor(2);
    } else {
      // Inactive state - simple explanation + button
      float titleWidth = ImGui::CalcTextSize("No WiFi?").x;
      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - titleWidth) / 2);
      ImGui::PushFont(theme_->GetHeadingFont());
      ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary),
                         "No WiFi?");
      ImGui::PopFont();

      ImGui::Spacing();
      ImGui::Spacing();

      // Simple centered explanation
      const char *helpText = "Create a temporary network for direct transfers";
      float helpWidth = ImGui::CalcTextSize(helpText).x;
      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - helpWidth) / 2);
      ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "%s",
                         helpText);

      ImGui::Spacing();
      ImGui::Spacing();
      ImGui::Spacing();

      // Start button
      float buttonWidth = 160.0f;
      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
      ImGui::PushStyleColor(ImGuiCol_Button,
                            theme_->GetColorVec(ThemeColor::Primary));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            theme_->GetColorVec(ThemeColor::PrimaryLight));
      if (ImGui::Button("Create Hotspot", ImVec2(buttonWidth, 44))) {
        if (bridge_->StartHotspot()) {
          auto info = bridge_->GetHotspotInfo();
          hotspotActive_ = true;
          hotspotSsid_ = info.ssid;
          hotspotPassword_ = info.password;
          hotspotGatewayIp_ = info.gateway_ip;
        }
      }
      ImGui::PopStyleColor(2);
    }
  }
  ImGui::End();

  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void DiscoverView::RenderManualConnectModal() {
  if (!showManualConnectModal_)
    return;

  ImGui::SetNextWindowSize(ImVec2(420, 320), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  // Match QR modal styling - bright purple-ish background
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.11f, 0.18f, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.486f, 0.228f, 0.929f, 0.9f));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                        ImVec4(0.586f, 0.328f, 1.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(25, 20));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

  if (ImGui::Begin("Manual Connect", &showManualConnectModal_,
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {

    // Description
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1.0f),
                       "Connect directly by entering the IP address");

    ImGui::Spacing();
    ImGui::Spacing();

    // IP input - with label
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "IP Address");
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.18f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ImVec4(0.25f, 0.22f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          ImVec4(0.3f, 0.25f, 0.4f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##ManualIP", "192.168.1.100", manualIp_,
                             sizeof(manualIp_));
    ImGui::PopStyleColor(4);

    ImGui::Spacing();

    // Port input
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Port");
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.18f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ImVec4(0.25f, 0.22f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          ImVec4(0.3f, 0.25f, 0.4f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("##ManualPort", manualPort_, sizeof(manualPort_));
    ImGui::PopStyleColor(4);

    ImGui::Spacing();

    // Device name input
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                       "Device Name (optional)");
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.18f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ImVec4(0.25f, 0.22f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          ImVec4(0.3f, 0.25f, 0.4f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##ManualName", "Remote Device", manualName_,
                             sizeof(manualName_));
    ImGui::PopStyleColor(4);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    // Buttons - centered
    float buttonWidth = 120.0f;
    float totalWidth = buttonWidth * 2 + 20;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) / 2);

    // Cancel button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.28f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.4f, 0.35f, 0.5f, 1.0f));
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 40))) {
      showManualConnectModal_ = false;
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine(0, 20);

    // Connect button - bright purple
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(0.486f, 0.228f, 0.929f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.586f, 0.328f, 1.0f, 1.0f));
    if (ImGui::Button("Connect", ImVec2(buttonWidth, 40))) {
      if (strlen(manualIp_) > 0) {
        bridge_->AddManualDevice(manualIp_, (uint16_t)atoi(manualPort_),
                                 strlen(manualName_) > 0 ? manualName_
                                                         : "Remote Device");
        showManualConnectModal_ = false;
      }
    }
    ImGui::PopStyleColor(2);
  }
  ImGui::End();

  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(3);
}

} // namespace teleport::ui
