/**
 * @file Application_sdl.cpp
 * @brief Main application implementation with SDL2 + OpenGL3 rendering
 */

#include "Application_sdl.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <GL/gl.h>
#include <cstdio>
#include <teleport/teleport.h>

namespace teleport::ui {

Application::Application() = default;

Application::~Application() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  CleanupOpenGL();

  if (window_) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

void Application::GetWindowSize(int &width, int &height) const {
  SDL_GetWindowSize(window_, &width, &height);
}

bool Application::Initialize() {
  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
    return false;
  }

  if (!CreateWindow()) {
    return false;
  }

  if (!InitializeOpenGL()) {
    return false;
  }

  // Initialize ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr; // Disable imgui.ini

  // Initialize theme and load fonts
  theme_ = std::make_unique<Theme>();
  theme_->Apply();
  theme_->LoadFonts(io);

  // Initialize platform/renderer backends
  ImGui_ImplSDL2_InitForOpenGL(window_, glContext_);
  ImGui_ImplOpenGL3_Init("#version 130");

  // Initialize Teleport bridge
  bridge_ = std::make_unique<TeleportBridge>();
  bridge_->Initialize();

  // Initialize all 5 views
  discoverView_ = std::make_unique<DiscoverView>(bridge_.get(), theme_.get());
  sendView_ = std::make_unique<SendView>(bridge_.get(), theme_.get());
  receiveView_ = std::make_unique<ReceiveView>(bridge_.get(), theme_.get());
  transfersView_ = std::make_unique<TransfersView>(bridge_.get(), theme_.get());
  settingsView_ = std::make_unique<SettingsView>(bridge_.get(), theme_.get());

  // Enable drag and drop
  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

  return true;
}

bool Application::CreateWindow() {
  // OpenGL attributes
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  // Create window
  Uint32 windowFlags =
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  window_ =
      SDL_CreateWindow("Teleport", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, width_, height_, windowFlags);

  if (!window_) {
    fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
    return false;
  }

  // Set minimum window size
  SDL_SetWindowMinimumSize(window_, 900, 600);

  return true;
}

bool Application::InitializeOpenGL() {
  glContext_ = SDL_GL_CreateContext(window_);
  if (!glContext_) {
    fprintf(stderr, "SDL_GL_CreateContext Error: %s\n", SDL_GetError());
    return false;
  }

  SDL_GL_MakeCurrent(window_, glContext_);
  SDL_GL_SetSwapInterval(1); // Enable vsync

  return true;
}

void Application::CleanupOpenGL() {
  if (glContext_) {
    SDL_GL_DeleteContext(glContext_);
    glContext_ = nullptr;
  }
}

int Application::Run() {
  while (running_) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      HandleEvent(event);
    }

    // Update all views
    if (discoverView_) {
      discoverView_->Update();

      // Check if user clicked Send on a device card
      std::string sendDeviceId = discoverView_->PopSendRequest();
      if (!sendDeviceId.empty() && sendView_) {
        sendView_->SetTargetDevice(sendDeviceId);
        previousTab_ = currentTab_;
        currentTab_ = Tab::Send;
        tabTransition_ = 0.0f;
      }
    }
    if (sendView_)
      sendView_->Update();
    if (receiveView_)
      receiveView_->Update();
    if (transfersView_)
      transfersView_->Update();
    if (settingsView_)
      settingsView_->Update();

    Render();
  }

  return 0;
}

void Application::HandleEvent(const SDL_Event &event) {
  switch (event.type) {
  case SDL_QUIT:
    running_ = false;
    break;

  case SDL_WINDOWEVENT:
    if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
        event.window.windowID == SDL_GetWindowID(window_)) {
      running_ = false;
    }
    break;

  case SDL_DROPFILE:
    HandleDropEvent(event);
    break;
  }
}

void Application::HandleDropEvent(const SDL_Event &event) {
  if (event.drop.file && sendView_ && currentTab_ == Tab::Send) {
    // Convert to vector of paths for SendView
    std::vector<std::string> files;
    files.push_back(event.drop.file);
    sendView_->AddFiles(files);
  }
  SDL_free(event.drop.file);
}

void Application::Render() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  RenderUI();

  ImGui::Render();

  int width, height;
  GetWindowSize(width, height);
  glViewport(0, 0, width, height);
  glClearColor(0.067f, 0.067f, 0.090f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  SDL_GL_SwapWindow(window_);
}

void Application::RenderUI() {
  int windowWidth, windowHeight;
  GetWindowSize(windowWidth, windowHeight);

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2((float)windowWidth, (float)windowHeight));

  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##MainWindow", nullptr, flags);
  ImGui::PopStyleVar();

  RenderSidebar();
  ImGui::SameLine(0, 0);
  RenderMainContent();

  ImGui::End();

  // GLOBAL: Check for incoming transfer request and show dialog
  if (bridge_ && bridge_->HasPendingRequest()) {
    RenderGlobalIncomingDialog();
  }
}

void Application::RenderSidebar() {
  int windowWidth, windowHeight;
  GetWindowSize(windowWidth, windowHeight);

  const float sidebarWidth = 70.0f;

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 sidebarStart = ImGui::GetCursorScreenPos();
  ImVec2 sidebarEnd(sidebarStart.x + sidebarWidth,
                    sidebarStart.y + windowHeight);

  // Gradient background
  drawList->AddRectFilledMultiColor(
      sidebarStart, sidebarEnd, theme_->GetColor(ThemeColor::SidebarTop),
      theme_->GetColor(ThemeColor::SidebarTop),
      theme_->GetColor(ThemeColor::SidebarBottom),
      theme_->GetColor(ThemeColor::SidebarBottom));

  // Right border
  drawList->AddLine(ImVec2(sidebarEnd.x, sidebarStart.y),
                    ImVec2(sidebarEnd.x, sidebarEnd.y),
                    theme_->GetColor(ThemeColor::Border), 1.0f);

  ImGui::BeginChild("##Sidebar", ImVec2(sidebarWidth, (float)windowHeight),
                    false);

  // 5 navigation items
  struct NavItem {
    const char *icon;
    const char *tooltip;
    Tab tab;
  };

  NavItem items[] = {{"D", "Discover", Tab::Discover},
                     {"S", "Send", Tab::Send},
                     {"R", "Receive", Tab::Receive},
                     {"T", "Transfers", Tab::Transfers},
                     {"O", "Settings", Tab::Settings}};

  ImGui::SetCursorPosY(20);

  ImGui::PushFont(theme_->GetHeadingFont());
  for (int i = 0; i < 5; i++) {
    ImVec2 buttonPos = ImGui::GetCursorScreenPos();
    ImVec2 buttonSize(sidebarWidth, 50);

    bool isSelected = (currentTab_ == items[i].tab);
    bool isHovered = ImGui::IsMouseHoveringRect(
        buttonPos,
        ImVec2(buttonPos.x + buttonSize.x, buttonPos.y + buttonSize.y));

    // Animate hover
    float targetHover = isHovered ? 1.0f : 0.0f;
    sidebarHoverAnim_[i] += (targetHover - sidebarHoverAnim_[i]) * 0.2f;

    // Draw selection indicator
    if (isSelected) {
      drawList->AddRectFilled(
          ImVec2(buttonPos.x + 4, buttonPos.y + 10),
          ImVec2(buttonPos.x + 7, buttonPos.y + buttonSize.y - 10),
          theme_->GetColor(ThemeColor::Primary), 2.0f);
    }

    // Hover background
    if (sidebarHoverAnim_[i] > 0.01f) {
      ImVec4 hoverColor = theme_->GetColorVec(ThemeColor::SurfaceLight);
      hoverColor.w = 0.3f * sidebarHoverAnim_[i];
      drawList->AddRectFilled(ImVec2(buttonPos.x + 8, buttonPos.y + 4),
                              ImVec2(buttonPos.x + buttonSize.x - 8,
                                     buttonPos.y + buttonSize.y - 4),
                              ImGui::ColorConvertFloat4ToU32(hoverColor), 8.0f);
    }

    // Icon/text
    ImVec4 textColor =
        isSelected
            ? theme_->GetColorVec(ThemeColor::Primary)
            : (isHovered ? theme_->GetColorVec(ThemeColor::TextPrimary)
                         : theme_->GetColorVec(ThemeColor::TextSecondary));

    ImVec2 textSize = ImGui::CalcTextSize(items[i].icon);
    ImGui::SetCursorScreenPos(
        ImVec2(buttonPos.x + (buttonSize.x - textSize.x) * 0.5f,
               buttonPos.y + (buttonSize.y - textSize.y) * 0.5f));
    ImGui::TextColored(textColor, "%s", items[i].icon);

    // Invisible button for clicks
    ImGui::SetCursorScreenPos(buttonPos);
    char buttonId[32];
    snprintf(buttonId, sizeof(buttonId), "##Nav%d", i);
    if (ImGui::InvisibleButton(buttonId, buttonSize)) {
      if (currentTab_ != items[i].tab) {
        previousTab_ = currentTab_;
        currentTab_ = items[i].tab;
        tabTransition_ = 0.0f;
      }
    }

    // Tooltip
    if (isHovered && ImGui::BeginTooltip()) {
      ImGui::Text("%s", items[i].tooltip);
      ImGui::EndTooltip();
    }
  }
  ImGui::PopFont();

  ImGui::EndChild();
}

void Application::RenderMainContent() {
  int windowWidth, windowHeight;
  GetWindowSize(windowWidth, windowHeight);

  const float sidebarWidth = 70.0f;
  const float contentWidth = windowWidth - sidebarWidth - 10;

  ImGui::BeginChild("##MainContent", ImVec2(contentWidth, (float)windowHeight),
                    false);

  tabTransition_ += (1.0f - tabTransition_) * 0.15f;

  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tabTransition_);

  // Handle all 5 tabs
  switch (currentTab_) {
  case Tab::Discover:
    if (discoverView_)
      discoverView_->Render();
    break;
  case Tab::Send:
    if (sendView_)
      sendView_->Render();
    break;
  case Tab::Receive:
    if (receiveView_)
      receiveView_->Render();
    break;
  case Tab::Transfers:
    if (transfersView_)
      transfersView_->Render();
    break;
  case Tab::Settings:
    if (settingsView_)
      settingsView_->Render();
    break;
  }

  ImGui::PopStyleVar();

  ImGui::EndChild();
}

void Application::RenderSettingsPlaceholder() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 20));

  ImGui::PushFont(theme_->GetHeadingFont());
  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Settings");
  ImGui::PopFont();

  ImGui::Spacing();
  ImGui::Spacing();

  ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary),
                     "Coming soon...");

  ImGui::PopStyleVar();
}

void Application::RenderGlobalIncomingDialog() {
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;
  ImVec2 dialogSize(660, 520);
  ImVec2 dialogPos((displaySize.x - dialogSize.x) * 0.5f,
                   (displaySize.y - dialogSize.y) * 0.5f);

  // Semi-transparent dark overlay behind dialog
  ImDrawList *bgList = ImGui::GetForegroundDrawList();
  bgList->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(0, 0, 0, 160));

  // Set up the popup window
  ImGui::SetNextWindowPos(dialogPos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(dialogSize, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(1.0f);

  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40, 35));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.36f, 0.36f, 0.42f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.7f, 0.5f, 1.0f, 1.0f));

  ImGui::Begin("##IncomingTransferDialog", nullptr, flags);

  auto request = bridge_->GetPendingRequest();

  // Header
  ImGui::PushFont(theme_->GetHeadingFont());
  ImGui::TextColored(ImVec4(0.85f, 0.7f, 1.0f, 1.0f), "Incoming File Transfer");
  ImGui::PopFont();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Sender info
  ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "From:");
  ImGui::SameLine();
  ImGui::PushFont(theme_->GetHeadingFont());
  ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s",
                     request.sender.name.c_str());
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%s)",
                     request.sender.ip.c_str());

  ImGui::Spacing();
  ImGui::Spacing();

  // File list header
  ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Files to receive: %zu",
                     request.files.size());

  ImGui::Spacing();

  // File list
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.45f, 0.45f, 0.55f, 1.0f));
  ImGui::BeginChild("##FileList", ImVec2(0, 180), true);

  for (const auto &[name, size] : request.files) {
    char sizeStr[32];
    teleport_format_bytes(size, sizeStr, sizeof(sizeStr));

    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "*");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", name.c_str());
    ImGui::SameLine(dialogSize.x - 180);
    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", sizeStr);
  }

  ImGui::EndChild();
  ImGui::PopStyleColor(2);

  ImGui::Spacing();

  // Total size
  char totalStr[32];
  teleport_format_bytes(request.totalSize, totalStr, sizeof(totalStr));
  ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Total size:");
  ImGui::SameLine();
  ImGui::PushFont(theme_->GetHeadingFont());
  ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.8f, 1.0f), "%s", totalStr);
  ImGui::PopFont();

  ImGui::Spacing();
  ImGui::Spacing();
  ImGui::Spacing();

  // Buttons - centered at bottom
  float buttonWidth = 200;
  float buttonHeight = 50;
  float totalButtonWidth = buttonWidth * 2 + 40;
  float startX = (dialogSize.x - 80 - totalButtonWidth) / 2;

  ImGui::SetCursorPosX(startX);

  // REJECT button
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.25f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.2f, 0.2f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);

  if (ImGui::Button("Reject", ImVec2(buttonWidth, buttonHeight))) {
    bridge_->RejectPendingRequest();
  }

  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);

  ImGui::SameLine(0, 40);

  // ACCEPT button
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.75f, 0.45f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.3f, 0.9f, 0.55f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.65f, 0.4f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);

  if (ImGui::Button("Accept", ImVec2(buttonWidth, buttonHeight))) {
    bridge_->AcceptPendingRequest();
  }

  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);

  ImGui::End();

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);
}

} // namespace teleport::ui
