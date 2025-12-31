/**
 * @file Application.cpp
 * @brief Main application implementation with DirectX 11 rendering
 */

#include "Application.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <shellapi.h>
#include <cstdio>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

// Forward declare ImGui Win32 message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace teleport::ui {

Application::Application() = default;

Application::~Application() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    CleanupDirectX();
    
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
}

void Application::GetWindowSize(int& width, int& height) const {
    RECT rect;
    GetClientRect(hwnd_, &rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
}

bool Application::Initialize(HINSTANCE hInstance, int nCmdShow) {
    if (!CreateAppWindow(hInstance, nCmdShow)) {
        return false;
    }

    if (!InitializeDirectX()) {
        return false;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Disable imgui.ini

    // Initialize theme and load fonts
    theme_ = std::make_unique<Theme>();
    theme_->Apply();
    theme_->LoadFonts(io);

    // Initialize platform/renderer backends
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_.Get(), context_.Get());

    // Initialize Teleport bridge
    bridge_ = std::make_unique<TeleportBridge>();
    bridge_->Initialize(); // Non-fatal if fails

    // Initialize views
    discoverView_ = std::make_unique<DiscoverView>(bridge_.get(), theme_.get());
    sendView_ = std::make_unique<SendView>(bridge_.get(), theme_.get());
    receiveView_ = std::make_unique<ReceiveView>(bridge_.get(), theme_.get());
    transfersView_ = std::make_unique<TransfersView>(bridge_.get(), theme_.get());

    // Apply blur effect
    EnableBlurBehind();

    return true;
}

bool Application::CreateAppWindow(HINSTANCE hInstance, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"TeleportWindowClass";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;

    ATOM classAtom = RegisterClassExW(&wc);
    if (!classAtom) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    // Calculate window position (centered)
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - width_) / 2;
    int y = (screenHeight - height_) / 2;

    // Create window
    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"Teleport",
        WS_OVERLAPPEDWINDOW,
        x, y, width_, height_,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!hwnd_) {
        return false;
    }

    // Dark title bar (Windows 10 1809+)
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd_, 20, &useDarkMode, sizeof(useDarkMode));

    // Enable rounded corners on Windows 11
    int cornerPreference = 2;
    DwmSetWindowAttribute(hwnd_, 33, &cornerPreference, sizeof(cornerPreference));

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

    return true;
}

void Application::EnableBlurBehind() {
    // Try Mica effect first (Windows 11)
    int micaValue = 2;
    HRESULT hr = DwmSetWindowAttribute(hwnd_, 38, &micaValue, sizeof(micaValue));
    
    if (FAILED(hr)) {
        // Fall back to acrylic/blur (Windows 10)
        struct ACCENTPOLICY {
            int AccentState;
            int AccentFlags;
            int GradientColor;
            int AnimationId;
        };
        struct WINCOMPATTRDATA {
            int Attribute;
            PVOID Data;
            ULONG DataSize;
        };
        
        ACCENTPOLICY policy = { 3, 0, 0, 0 }; // ACCENT_ENABLE_BLURBEHIND
        WINCOMPATTRDATA data = { 19, &policy, sizeof(policy) };
        
        typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);
        HMODULE user32 = GetModuleHandle(L"user32.dll");
        if (user32) {
            auto SetWindowCompositionAttribute = 
                (pSetWindowCompositionAttribute)GetProcAddress(user32, "SetWindowCompositionAttribute");
            if (SetWindowCompositionAttribute) {
                SetWindowCompositionAttribute(hwnd_, &data);
            }
        }
    }
}

bool Application::InitializeDirectX() {
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[] = { 
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0, 
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0 
    };
    
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        &device_,
        &featureLevel,
        &context_
    );

    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createFlags,
            featureLevels,
            _countof(featureLevels),
            D3D11_SDK_VERSION,
            &device_,
            &featureLevel,
            &context_
        );
    }

    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = device_.As(&dxgiDevice);
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.BufferCount = 2;
    sd.Width = 0;
    sd.Height = 0;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    sd.Scaling = DXGI_SCALING_STRETCH;

    hr = factory->CreateSwapChainForHwnd(
        device_.Get(),
        hwnd_,
        &sd,
        nullptr,
        nullptr,
        &swapChain_
    );

    if (FAILED(hr)) {
        sd.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
        sd.BufferCount = 1;
        hr = factory->CreateSwapChainForHwnd(
            device_.Get(),
            hwnd_,
            &sd,
            nullptr,
            nullptr,
            &swapChain_
        );
    }

    if (FAILED(hr)) {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void Application::CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView_);
    }
}

void Application::CleanupRenderTarget() {
    renderTargetView_.Reset();
}

void Application::CleanupDirectX() {
    CleanupRenderTarget();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
}

int Application::Run() {
    MSG msg = {};
    
    while (true) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                return (int)msg.wParam;
            }
        }
        
        // Update views
        if (discoverView_) discoverView_->Update();
        if (sendView_) sendView_->Update();
        if (receiveView_) receiveView_->Update();
        if (transfersView_) transfersView_->Update();
        
        Render();
    }
    
    return 0;
}

void Application::Render() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    
    RenderUI();
    
    ImGui::Render();
    
    const float clearColor[] = { 0.067f, 0.067f, 0.090f, 1.0f };
    context_->OMSetRenderTargets(1, renderTargetView_.GetAddressOf(), nullptr);
    context_->ClearRenderTargetView(renderTargetView_.Get(), clearColor);
    
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    
    swapChain_->Present(1, 0);
}

void Application::RenderUI() {
    int windowWidth, windowHeight;
    GetWindowSize(windowWidth, windowHeight);
    
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)windowWidth, (float)windowHeight));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##MainWindow", nullptr, flags);
    ImGui::PopStyleVar();
    
    RenderSidebar();
    ImGui::SameLine(0, 0);
    RenderMainContent();
    
    ImGui::End();
}

void Application::RenderSidebar() {
    int windowWidth, windowHeight;
    GetWindowSize(windowWidth, windowHeight);
    
    const float sidebarWidth = 70.0f;
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 sidebarStart = ImGui::GetCursorScreenPos();
    ImVec2 sidebarEnd(sidebarStart.x + sidebarWidth, sidebarStart.y + windowHeight);
    
    // Gradient background
    drawList->AddRectFilledMultiColor(
        sidebarStart, sidebarEnd,
        theme_->GetColor(ThemeColor::SidebarTop),
        theme_->GetColor(ThemeColor::SidebarTop),
        theme_->GetColor(ThemeColor::SidebarBottom),
        theme_->GetColor(ThemeColor::SidebarBottom)
    );
    
    // Right border
    drawList->AddLine(
        ImVec2(sidebarEnd.x, sidebarStart.y),
        ImVec2(sidebarEnd.x, sidebarEnd.y),
        theme_->GetColor(ThemeColor::Border),
        1.0f
    );
    
    ImGui::BeginChild("##Sidebar", ImVec2(sidebarWidth, (float)windowHeight), false);
    
    // Navigation items
    struct NavItem {
        const char* icon;
        const char* tooltip;
        Tab tab;
    };
    
    NavItem items[] = {
        { "D", "Discover", Tab::Discover },
        { "S", "Send", Tab::Send },
        { "R", "Receive", Tab::Receive },
        { "T", "Transfers", Tab::Transfers },
        { "O", "Settings", Tab::Settings }
    };
    
    ImGui::SetCursorPosY(20);
    
    ImGui::PushFont(theme_->GetHeadingFont());
    for (int i = 0; i < 5; i++) {
        ImVec2 buttonPos = ImGui::GetCursorScreenPos();
        ImVec2 buttonSize(sidebarWidth, 50);
        
        bool isSelected = (currentTab_ == items[i].tab);
        bool isHovered = ImGui::IsMouseHoveringRect(buttonPos, 
            ImVec2(buttonPos.x + buttonSize.x, buttonPos.y + buttonSize.y));
        
        // Animate hover
        float targetHover = isHovered ? 1.0f : 0.0f;
        sidebarHoverAnim_[i] += (targetHover - sidebarHoverAnim_[i]) * 0.2f;
        
        // Draw selection indicator
        if (isSelected) {
            drawList->AddRectFilled(
                ImVec2(buttonPos.x + 4, buttonPos.y + 10),
                ImVec2(buttonPos.x + 7, buttonPos.y + buttonSize.y - 10),
                theme_->GetColor(ThemeColor::Primary),
                2.0f
            );
        }
        
        // Hover background
        if (sidebarHoverAnim_[i] > 0.01f) {
            ImVec4 hoverColor = theme_->GetColorVec(ThemeColor::SurfaceLight);
            hoverColor.w = 0.3f * sidebarHoverAnim_[i];
            drawList->AddRectFilled(
                ImVec2(buttonPos.x + 8, buttonPos.y + 4),
                ImVec2(buttonPos.x + buttonSize.x - 8, buttonPos.y + buttonSize.y - 4),
                ImGui::ColorConvertFloat4ToU32(hoverColor),
                8.0f
            );
        }
        
        // Icon/text
        ImVec4 textColor = isSelected ? 
            theme_->GetColorVec(ThemeColor::Primary) : 
            (isHovered ? theme_->GetColorVec(ThemeColor::TextPrimary) : 
                        theme_->GetColorVec(ThemeColor::TextSecondary));
        
        ImVec2 textSize = ImGui::CalcTextSize(items[i].icon);
        ImGui::SetCursorScreenPos(ImVec2(
            buttonPos.x + (buttonSize.x - textSize.x) * 0.5f,
            buttonPos.y + (buttonSize.y - textSize.y) * 0.5f
        ));
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
    
    ImGui::BeginChild("##MainContent", ImVec2(contentWidth, (float)windowHeight), false);
    
    tabTransition_ += (1.0f - tabTransition_) * 0.12f;
    
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tabTransition_);
    
    switch (currentTab_) {
        case Tab::Discover:
            if (discoverView_) discoverView_->Render();
            break;
        case Tab::Send:
            if (sendView_) sendView_->Render();
            break;
        case Tab::Receive:
            if (receiveView_) receiveView_->Render();
            break;
        case Tab::Transfers:
            if (transfersView_) transfersView_->Render();
            break;
        case Tab::Settings:
            RenderSettingsPlaceholder();
            break;
    }
    
    ImGui::PopStyleVar();
    
    ImGui::EndChild();
}

void Application::RenderSettingsPlaceholder() {
    ImGui::SetCursorPos(ImVec2(40, 30));
    ImGui::PushFont(theme_->GetHeadingFont());
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextPrimary), "Settings");
    ImGui::PopFont();
    
    ImGui::SetCursorPos(ImVec2(40, 80));
    ImGui::TextColored(theme_->GetColorVec(ThemeColor::TextSecondary), "Coming soon...");
}

LRESULT CALLBACK Application::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Application* app = reinterpret_cast<Application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        app = static_cast<Application*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        if (app) {
            app->hwnd_ = hwnd;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    
    if (app && app->device_) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
            return true;
        }
    }
    
    if (app) {
        return app->HandleMessage(msg, wParam, lParam);
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT Application::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            if (device_ && wParam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                swapChain_->ResizeBuffers(0, 
                    LOWORD(lParam), HIWORD(lParam), 
                    DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 900;
            mmi->ptMinTrackSize.y = 600;
            return 0;
        }
        
        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            if (sendView_ && currentTab_ == Tab::Send) {
                sendView_->HandleFileDrop(hDrop);
            }
            DragFinish(hDrop);
            return 0;
        }
    }
    
    return DefWindowProc(hwnd_, msg, wParam, lParam);
}

} // namespace teleport::ui
