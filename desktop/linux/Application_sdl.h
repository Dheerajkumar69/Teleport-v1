/**
 * @file Application_sdl.h
 * @brief Main application class with 5-tab navigation (SDL2/OpenGL version)
 */

#pragma once

#include <SDL2/SDL.h>
#include <memory>
#include <string>

#include "TeleportBridge.h"
#include "Theme.h"
#include "views/DiscoverView.h"
#include "views/ReceiveView.h"
#include "views/SendView.h"
#include "views/SettingsView.h"
#include "views/TransfersView.h"

namespace teleport::ui {

/**
 * @brief Main application with 5-tab interface (Linux/SDL2 version)
 *
 * Tabs: Discover | Send | Receive | Transfers | Settings
 */
class Application {
public:
  Application();
  ~Application();

  bool Initialize();
  int Run();

  void GetWindowSize(int &width, int &height) const;

private:
  // Window setup
  bool CreateWindow();
  bool InitializeOpenGL();
  void CleanupOpenGL();

  // Rendering
  void Render();
  void RenderUI();
  void RenderSidebar();
  void RenderMainContent();
  void RenderSettingsPlaceholder();
  void RenderGlobalIncomingDialog();

  // Event handling
  void HandleEvent(const SDL_Event &event);
  void HandleDropEvent(const SDL_Event &event);

private:
  SDL_Window *window_ = nullptr;
  SDL_GLContext glContext_ = nullptr;
  int width_ = 1280;
  int height_ = 800;
  bool running_ = true;

  // Application state
  std::unique_ptr<TeleportBridge> bridge_;
  std::unique_ptr<Theme> theme_;

  // Views - All 5 tabs
  std::unique_ptr<DiscoverView> discoverView_;
  std::unique_ptr<SendView> sendView_;
  std::unique_ptr<ReceiveView> receiveView_;
  std::unique_ptr<TransfersView> transfersView_;
  std::unique_ptr<SettingsView> settingsView_;

  // Navigation - 5 tabs
  enum class Tab { Discover, Send, Receive, Transfers, Settings };
  Tab currentTab_ = Tab::Discover;
  Tab previousTab_ = Tab::Discover;

  // Animation state
  float sidebarHoverAnim_[5] = {0};
  float tabTransition_ = 1.0f;
};

} // namespace teleport::ui
