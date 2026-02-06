/**
 * @file main_linux.cpp
 * @brief Teleport Desktop UI Entry Point for Linux
 * 
 * Linux GUI using Dear ImGui with SDL2 + OpenGL3 backend.
 */

#include "Application_sdl.h"
#include <memory>
#include <cstdio>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    // Create and run the application
    try {
        auto app = std::make_unique<teleport::ui::Application>();
        
        if (!app->Initialize()) {
            fprintf(stderr, "Failed to initialize application\n");
            return 1;
        }
        
        int result = app->Run();
        return result;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Teleport Error: %s\n", e.what());
        return 1;
    }
}
