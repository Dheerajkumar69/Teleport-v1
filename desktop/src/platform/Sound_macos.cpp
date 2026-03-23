/**
 * @file Sound_macos.cpp
 * @brief macOS sound notification implementation using afplay
 */

#include "Sound.h"
#include <cstdlib>
#include <thread>

namespace teleport::ui {

void PlaySuccessSound() {
  // Play system sound asynchronously using afplay
  std::thread([]() {
    // Use macOS system sound - Glass is a pleasant notification sound
    system("afplay /System/Library/Sounds/Glass.aiff &");
  }).detach();
}

void PlayErrorSound() {
  std::thread([]() {
    // Use macOS system error sound
    system("afplay /System/Library/Sounds/Basso.aiff &");
  }).detach();
}

void PlayNotificationSound() {
  std::thread([]() {
    // Use macOS notification sound
    system("afplay /System/Library/Sounds/Ping.aiff &");
  }).detach();
}

bool IsSoundSupported() { return true; }

} // namespace teleport::ui
