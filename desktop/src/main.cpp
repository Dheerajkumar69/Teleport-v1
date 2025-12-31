/**
 * @file main.cpp
 * @brief Teleport Desktop UI Entry Point
 * 
 * Modern Windows GUI using Dear ImGui with DirectX 11 backend.
 * Features dark glassmorphism theme with smooth animations.
 */

#include "Application.h"
#include <windows.h>
#include <memory>

// Enable visual styles
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    
    // Set basic DPI awareness (works on all Windows versions)
    SetProcessDPIAware();
    
    // Initialize COM for file dialogs
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Failed to initialize COM", L"Teleport", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create and run the application
    try {
        auto app = std::make_unique<teleport::ui::Application>();
        
        if (!app->Initialize(hInstance, nCmdShow)) {
            MessageBoxW(nullptr, L"Failed to initialize application", L"Teleport", MB_OK | MB_ICONERROR);
            CoUninitialize();
            return 1;
        }
        
        int result = app->Run();
        
        CoUninitialize();
        return result;
    }
    catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Teleport Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
}
