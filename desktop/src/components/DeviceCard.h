/**
 * @file DeviceCard.h
 * @brief Animated device card component
 */

#pragma once

#include "TeleportBridge.h"
#include "Theme.h"

namespace teleport::ui {

/**
 * @brief Draws an animated device card
 */
class DeviceCard {
public:
    static void Draw(
        const DeviceInfo& device,
        Theme* theme,
        bool isSelected,
        float hoverAnim,
        float fadeIn
    );
};

} // namespace teleport::ui
