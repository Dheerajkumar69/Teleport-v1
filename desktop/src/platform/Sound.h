/**
 * @file Sound.h
 * @brief Cross-platform sound notification interface
 */

#ifndef TELEPORT_SOUND_H
#define TELEPORT_SOUND_H

namespace teleport::ui {

/**
 * @brief Play success/completion sound
 */
void PlaySuccessSound();

/**
 * @brief Play notification/message sound
 */
void PlayNotificationSound();

/**
 * @brief Play error sound
 */
void PlayErrorSound();

/**
 * @brief Check if sound is supported on this platform
 */
bool IsSoundSupported();

} // namespace teleport::ui

#endif // TELEPORT_SOUND_H
