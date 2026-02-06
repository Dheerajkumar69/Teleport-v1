/**
 * @file main_macos.cpp
 * @brief macOS entry point for Teleport desktop application
 */

#include "Application_sdl.h"
#include <cstdio>

int main(int argc, char *argv[]) {
  teleport::ui::Application app;

  if (!app.Initialize()) {
    fprintf(stderr, "Failed to initialize application\n");
    return 1;
  }

  return app.Run();
}
