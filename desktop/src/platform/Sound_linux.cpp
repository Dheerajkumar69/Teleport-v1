/**
 * @file Sound_linux.cpp
 * @brief Linux sound notification implementation - BULLETPROOF VERSION
 *
 * Features:
 * - Uses fork() to avoid blocking main thread
 * - Multiple fallback paths for different distros
 * - Graceful silent fallback if no sound system
 * - No exceptions thrown
 */

#ifndef _WIN32 // Only compile on non-Windows

#include "Sound.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace teleport::ui {

namespace {

// Check if a command exists (cached for performance)
bool CommandExists(const char *cmd) {
  if (!cmd || cmd[0] == '\0')
    return false;

  char check[256];
  int written =
      snprintf(check, sizeof(check), "which %s > /dev/null 2>&1", cmd);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(check)) {
    return false;
  }

  // Use popen to avoid dead child processes
  FILE *fp = popen(check, "r");
  if (!fp)
    return false;
  int status = pclose(fp);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Sound theme paths (freedesktop standard)
const char *SOUND_PATHS[] = {
    "/usr/share/sounds/freedesktop/stereo/complete.oga",
    "/usr/share/sounds/Yaru/stereo/complete.oga",
    "/usr/share/sounds/ubuntu/stereo/dialog-question.ogg",
    "/usr/share/sounds/freedesktop/stereo/message.oga", nullptr};

const char *MESSAGE_SOUND_PATHS[] = {
    "/usr/share/sounds/freedesktop/stereo/message.oga",
    "/usr/share/sounds/Yaru/stereo/message.ogg", nullptr};

const char *ERROR_SOUND_PATHS[] = {
    "/usr/share/sounds/freedesktop/stereo/dialog-error.oga",
    "/usr/share/sounds/Yaru/stereo/dialog-error.ogg", nullptr};

// Find first existing sound file
const char *FindSoundFile(const char *paths[]) {
  if (!paths)
    return nullptr;

  for (int i = 0; paths[i] != nullptr; i++) {
    if (access(paths[i], R_OK) == 0) {
      return paths[i];
    }
  }
  return nullptr;
}

// Play sound using fork to avoid blocking
void PlaySoundAsync(const char *soundFile, const char *eventId) {
  // Try to find a working sound
  const char *file = soundFile;
  bool useEvent = false;

  if (!file && eventId) {
    useEvent = true;
  }

  if (!file && !useEvent) {
    return; // No sound to play
  }

  // Fork to play sound asynchronously
  pid_t pid = fork();

  if (pid < 0) {
    // Fork failed - silently return
    return;
  }

  if (pid == 0) {
    // Child process
    // Close file descriptors to become a background process
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    // Try paplay first (PulseAudio/PipeWire)
    if (file && CommandExists("paplay")) {
      execlp("paplay", "paplay", file, nullptr);
    }

    // Try canberra-gtk-play with event ID
    if (useEvent && eventId && CommandExists("canberra-gtk-play")) {
      execlp("canberra-gtk-play", "canberra-gtk-play", "-i", eventId, nullptr);
    }

    // Try pw-play (PipeWire)
    if (file && CommandExists("pw-play")) {
      execlp("pw-play", "pw-play", file, nullptr);
    }

    // Try aplay (ALSA) - only for WAV files
    if (file && CommandExists("aplay")) {
      const char *ext = strrchr(file, '.');
      if (ext && (strcmp(ext, ".wav") == 0 || strcmp(ext, ".WAV") == 0)) {
        execlp("aplay", "aplay", "-q", file, nullptr);
      }
    }

    // All methods failed - exit quietly
    _exit(0);
  } else {
    // Parent process - don't wait for child (child becomes orphan)
    // Use signal to avoid zombie processes
    signal(SIGCHLD, SIG_IGN);
  }
}

// Cache for command availability
static int s_paplayAvailable = -1; // -1 = unchecked
static int s_canberraAvailable = -1;

void InitSoundCache() {
  if (s_paplayAvailable < 0) {
    s_paplayAvailable = CommandExists("paplay") ? 1 : 0;
  }
  if (s_canberraAvailable < 0) {
    s_canberraAvailable = CommandExists("canberra-gtk-play") ? 1 : 0;
  }
}

} // anonymous namespace

void PlaySuccessSound() {
  InitSoundCache();
  const char *soundFile = FindSoundFile(SOUND_PATHS);
  PlaySoundAsync(soundFile, "complete");
}

void PlayNotificationSound() {
  InitSoundCache();
  const char *soundFile = FindSoundFile(MESSAGE_SOUND_PATHS);
  PlaySoundAsync(soundFile, "message");
}

void PlayErrorSound() {
  InitSoundCache();
  const char *soundFile = FindSoundFile(ERROR_SOUND_PATHS);
  PlaySoundAsync(soundFile, "dialog-error");
}

bool IsSoundSupported() {
  InitSoundCache();
  return s_paplayAvailable > 0 || s_canberraAvailable > 0;
}

} // namespace teleport::ui

#endif // _WIN32
