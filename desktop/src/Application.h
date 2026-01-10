/**
 * @file Application.h
 * @brief Main application class with 5-tab navigation
 */

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <memory>
#include <string>

#include "TeleportBridge.h"
#include "Theme.h"
#include "views/DiscoverView.h"
#include "views/SendView.h"
#include "views/ReceiveView.h"
#include "views/TransfersView.h"
#include "views/SettingsView.h"

namespace teleport::ui {

using Microsoft::WRL::ComPtr;

/**
 * @brief Main application with 5-tab interface
 * 
 * Tabs: Discover | Send | Receive | Transfers | Settings
 */
class Application {
public:
    Application();
    ~Application();

    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    int Run();

    HWND GetHwnd() const { return hwnd_; }
    void GetWindowSize(int& width, int& height) const;

private:
    // Window setup
    bool CreateAppWindow(HINSTANCE hInstance, int nCmdShow);
    bool InitializeDirectX();
    void CleanupDirectX();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    
    // Rendering
    void Render();
    void RenderUI();
    void RenderSidebar();
    void RenderMainContent();
    void RenderSettingsPlaceholder();
    void RenderGlobalIncomingDialog();
    
    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    
    // Window blur effect (Windows 10/11)
    void EnableBlurBehind();

private:
    HWND hwnd_ = nullptr;
    int width_ = 1280;
    int height_ = 800;
    
    // DirectX 11
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11RenderTargetView> renderTargetView_;
    
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
    enum class Tab {
        Discover,
        Send,
        Receive,
        Transfers,
        Settings
    };
    Tab currentTab_ = Tab::Discover;
    Tab previousTab_ = Tab::Discover;
    
    // Animation state
    float sidebarHoverAnim_[5] = {0};
    float tabTransition_ = 1.0f;
};

} // namespace teleport::ui
