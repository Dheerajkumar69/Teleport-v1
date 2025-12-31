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

// Icon data (embedded)
static const char* ICON_DISCOVER = "\xef\x80\x82";  // fa-wifi
static const char* ICON_SEND = "\xef\x82\x93";      // fa-upload
static const char* ICON_RECEIVE = "\xef\x81\x99";   // fa-download
static const char* ICON_TRANSFERS = "\xef\x80\xb1"; // fa-exchange
static const char* ICON_SETTINGS = "\xef\x80\x93";  // fa-cog

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

bool Application::Initialize(HINSTANCE hInstance, int nCmdShow) {
    MessageBoxA(nullptr, "Step 1: Creating window...", "Debug", MB_OK);
    
    if (!CreateAppWindow(hInstance, nCmdShow)) {
        MessageBoxA(nullptr, "CreateAppWindow failed", "Error", MB_OK);
        return false;
    }
    
    MessageBoxA(nullptr, "Step 2: Initializing DirectX...", "Debug", MB_OK);

    if (!InitializeDirectX()) {
        MessageBoxA(nullptr, "InitializeDirectX failed", "Error", MB_OK);
        return false;
    }
    
    MessageBoxA(nullptr, "Step 3: Initializing ImGui...", "Debug", MB_OK);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Disable imgui.ini

    MessageBoxA(nullptr, "Step 4: Loading theme and fonts...", "Debug", MB_OK);

    // Initialize theme and load fonts
    theme_ = std::make_unique<Theme>();
    theme_->Apply();
    theme_->LoadFonts(io);

    MessageBoxA(nullptr, "Step 5: Initializing ImGui backends...", "Debug", MB_OK);

    // Initialize platform/renderer backends
    if (!ImGui_ImplWin32_Init(hwnd_)) {
        MessageBoxA(nullptr, "ImGui_ImplWin32_Init failed", "Error", MB_OK);
        return false;
    }
    if (!ImGui_ImplDX11_Init(device_.Get(), context_.Get())) {
        MessageBoxA(nullptr, "ImGui_ImplDX11_Init failed", "Error", MB_OK);
        return false;
    }

    MessageBoxA(nullptr, "Step 6: Initializing Teleport bridge...", "Debug", MB_OK);

    // Initialize Teleport bridge
    bridge_ = std::make_unique<TeleportBridge>();
    if (!bridge_->Initialize()) {
        // Non-fatal: continue without backend
    }

    MessageBoxA(nullptr, "Step 7: Creating views...", "Debug", MB_OK);

    // Initialize views
    discoverView_ = std::make_unique<DiscoverView>(bridge_.get(), theme_.get());
    sendView_ = std::make_unique<SendView>(bridge_.get(), theme_.get());
    receiveView_ = std::make_unique<ReceiveView>(bridge_.get(), theme_.get());
    transfersView_ = std::make_unique<TransfersView>(bridge_.get(), theme_.get());

    // Apply blur effect
    EnableBlurBehind();

    MessageBoxA(nullptr, "Initialization complete!", "Debug", MB_OK);

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
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);  // Default icon
    wc.hIconSm = wc.hIcon;

    ATOM classAtom = RegisterClassExW(&wc);
    if (!classAtom) {
        DWORD err = GetLastError();
        // Class may already be registered, try anyway
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            char msg[256];
            snprintf(msg, sizeof(msg), "RegisterClassExW failed. Error: %lu", err);
            MessageBoxA(nullptr, msg, "Error", MB_OK);
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
        DWORD err = GetLastError();
        char msg[256];
        snprintf(msg, sizeof(msg), "CreateWindowExW failed. Error: %lu", err);
        MessageBoxA(nullptr, msg, "Error", MB_OK);
        return false;
    }

    // Dark title bar (Windows 10 1809+) - ignore if fails
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &useDarkMode, sizeof(useDarkMode));

    // Enable rounded corners on Windows 11 - ignore if fails
    int cornerPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &cornerPreference, sizeof(cornerPreference));

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

    return true;
}

void Application::EnableBlurBehind() {
    // Try Mica effect first (Windows 11)
    int micaValue = 2; // DWMSBT_MAINWINDOW (Mica)
    HRESULT hr = DwmSetWindowAttribute(hwnd_, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &micaValue, sizeof(micaValue));
    
    if (FAILED(hr)) {
        // Fall back to Acrylic/Blur (Windows 10)
        struct ACCENTPOLICY {
            int nAccentState;
            int nFlags;
            unsigned int nColor;
            int nAnimationId;
        };
        struct WINCOMPATTRDATA {
            int nAttribute;
            PVOID pData;
            ULONG ulDataSize;
        };

        // Try to enable blur
        typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);
        HMODULE hUser32 = GetModuleHandle(L"user32.dll");
        if (hUser32) {
            auto SetWindowCompositionAttribute = 
                (pSetWindowCompositionAttribute)GetProcAddress(hUser32, "SetWindowCompositionAttribute");
            if (SetWindowCompositionAttribute) {
                ACCENTPOLICY accent = { 3, 0, 0x99000000, 0 }; // ACCENT_ENABLE_BLURBEHIND
                WINCOMPATTRDATA data = { 19, &accent, sizeof(accent) };
                SetWindowCompositionAttribute(hwnd_, &data);
            }
        }
    }
}

bool Application::InitializeDirectX() {
    // Create device and context
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[] = { 
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0, 
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0 
    };
    
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

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

    // Fallback to WARP if hardware not available
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
        MessageBoxA(nullptr, "Failed to create D3D11 device", "DirectX Error", MB_OK);
        return false;
    }

    // Get DXGI factory
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = device_.As(&dxgiDevice);
    if (FAILED(hr)) {
        MessageBoxA(nullptr, "Failed to get DXGI device", "DirectX Error", MB_OK);
        return false;
    }
    
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        MessageBoxA(nullptr, "Failed to get adapter", "DirectX Error", MB_OK);
        return false;
    }
    
    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        MessageBoxA(nullptr, "Failed to get factory", "DirectX Error", MB_OK);
        return false;
    }

    // Create swap chain with compatible settings
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.BufferCount = 2;
    sd.Width = 0;  // Auto
    sd.Height = 0; // Auto
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;  // More compatible
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
        // Try with even simpler settings
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
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to create swap chain. HRESULT: 0x%08X", (unsigned int)hr);
        MessageBoxA(nullptr, msg, "DirectX Error", MB_OK);
        return false;
    }

    CreateRenderTarget();
    return true;
}

void Application::CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView_);
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

void Application::GetWindowSize(int& width, int& height) const {
    RECT rect;
    GetClientRect(hwnd_, &rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
}

int Application::Run() {
    MSG msg = {};
    
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        // Update Teleport bridge
        if (bridge_) {
            bridge_->Update();
        }

        // Update views
        if (discoverView_) discoverView_->Update();
        if (sendView_) sendView_->Update();
        if (receiveView_) receiveView_->Update();
        if (transfersView_) transfersView_->Update();

        // Render frame
        Render();
    }

    return static_cast<int>(msg.wParam);
}

void Application::Render() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RenderUI();

    ImGui::Render();

    // Clear with dark transparent background
    const float clearColor[4] = { 0.07f, 0.07f, 0.09f, 0.95f };
    context_->OMSetRenderTargets(1, renderTargetView_.GetAddressOf(), nullptr);
    context_->ClearRenderTargetView(renderTargetView_.Get(), clearColor);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Present with vsync
    swapChain_->Present(1, 0);
}

void Application::RenderUI() {
    int windowWidth, windowHeight;
    GetWindowSize(windowWidth, windowHeight);

    // Make ImGui fill the entire window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)windowWidth, (float)windowHeight));
    
    ImGuiWindowFlags flags = 
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("##MainWindow", nullptr, flags);
    
    RenderSidebar();
    ImGui::SameLine();
    RenderMainContent();
    
    ImGui::End();
}

void Application::RenderSidebar() {
    int windowWidth, windowHeight;
    GetWindowSize(windowWidth, windowHeight);
    
    const float sidebarWidth = 70.0f;
    
    // Sidebar background
    ImGui::BeginChild("##Sidebar", ImVec2(sidebarWidth, (float)windowHeight), false);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetWindowPos();
    
    // Sidebar background gradient
    drawList->AddRectFilledMultiColor(
        pos,
        ImVec2(pos.x + sidebarWidth, pos.y + windowHeight),
        theme_->GetColor(ThemeColor::SidebarTop),
        theme_->GetColor(ThemeColor::SidebarTop),
        theme_->GetColor(ThemeColor::SidebarBottom),
        theme_->GetColor(ThemeColor::SidebarBottom)
    );

    // Logo area
    ImGui::SetCursorPos(ImVec2(15, 20));
    ImGui::PushFont(theme_->GetIconFont());
    ImGui::TextColored(ImVec4(0.49f, 0.23f, 0.93f, 1.0f), "\xef\x84\xa0"); // Teleport icon
    ImGui::PopFont();

    // Navigation buttons
    struct NavItem {
        const char* icon;
        const char* tooltip;
        Tab tab;
    };

    NavItem items[] = {
        { "\xef\x80\x82", "Discover Devices", Tab::Discover },   // wifi
        { "\xef\x82\x93", "Send Files", Tab::Send },             // upload
        { "\xef\x81\x99", "Receive Files", Tab::Receive },       // download
        { "\xef\x8e\xa1", "Active Transfers", Tab::Transfers },  // exchange
        { "\xef\x80\x93", "Settings", Tab::Settings }            // cog
    };

    ImGui::SetCursorPos(ImVec2(0, 80));
    
    ImGui::PushFont(theme_->GetIconFont());
    for (int i = 0; i < 5; i++) {
        const auto& item = items[i];
        bool isActive = (currentTab_ == item.tab);
        
        // Animate hover
        ImVec2 buttonPos = ImGui::GetCursorScreenPos();
        ImVec2 buttonSize(sidebarWidth, 50);
        bool isHovered = ImGui::IsMouseHoveringRect(buttonPos, ImVec2(buttonPos.x + buttonSize.x, buttonPos.y + buttonSize.y));
        
        float targetAnim = isHovered ? 1.0f : 0.0f;
        sidebarHoverAnim_[i] += (targetAnim - sidebarHoverAnim_[i]) * 0.15f;
        
        // Active indicator
        if (isActive) {
            drawList->AddRectFilled(
                ImVec2(buttonPos.x, buttonPos.y + 10),
                ImVec2(buttonPos.x + 3, buttonPos.y + 40),
                theme_->GetColor(ThemeColor::Primary),
                2.0f
            );
        }
        
        // Hover background
        if (sidebarHoverAnim_[i] > 0.01f) {
            ImU32 hoverCol = ImGui::ColorConvertFloat4ToU32(
                ImVec4(1.0f, 1.0f, 1.0f, 0.05f * sidebarHoverAnim_[i])
            );
            drawList->AddRectFilled(buttonPos, ImVec2(buttonPos.x + buttonSize.x, buttonPos.y + buttonSize.y), hoverCol);
        }
        
        // Icon
        ImGui::SetCursorPosX((sidebarWidth - 20) / 2);
        ImVec4 iconColor = isActive 
            ? ImVec4(0.49f, 0.23f, 0.93f, 1.0f)  // Primary purple
            : ImVec4(0.6f, 0.6f, 0.65f, 1.0f);    // Gray
        
        ImGui::PushStyleColor(ImGuiCol_Text, iconColor);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
        
        if (ImGui::Button(item.icon, ImVec2(sidebarWidth, 50))) {
            previousTab_ = currentTab_;
            currentTab_ = item.tab;
            tabTransition_ = 0.0f;
        }
        
        ImGui::PopStyleColor(4);
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", item.tooltip);
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
    
    // Animate tab transition
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
    
    // Handle WM_NCCREATE to store the Application pointer
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        app = static_cast<Application*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        if (app) {
            app->hwnd_ = hwnd;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    
    // Only call ImGui handler if app is initialized (device exists)
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
            if (sendView_) {
                sendView_->HandleFileDrop(hDrop);
            }
            DragFinish(hDrop);
            return 0;
        }
    }
    
    return DefWindowProc(hwnd_, msg, wParam, lParam);
}

} // namespace teleport::ui
