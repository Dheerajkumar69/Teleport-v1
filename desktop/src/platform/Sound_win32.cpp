/**
 * @file Sound_win32.cpp
 * @brief Windows sound notification implementation
 */

#include "Sound.h"

#ifdef _WIN32
#include <windows.h>

namespace teleport::ui {

void PlaySuccessSound() { MessageBeep(MB_OK); }

void PlayNotificationSound() { MessageBeep(MB_ICONASTERISK); }

void PlayErrorSound() { MessageBeep(MB_ICONERROR); }

bool IsSoundSupported() {
  return true; // Windows always has MessageBeep
}

} // namespace teleport::ui

#endif // _WIN32
