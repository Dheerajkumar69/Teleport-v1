/**
 * @file DeviceCard.cpp
 * @brief Device card implementation (shared rendering logic)
 */

#include "DeviceCard.h"
#include "imgui.h"

namespace teleport::ui {

void DeviceCard::Draw(
    const DeviceInfo& device,
    Theme* theme,
    bool isSelected,
    float hoverAnim,
    float fadeIn
) {
    // Card rendering is implemented directly in DiscoverView for now
    // This could be refactored to use this component for reusability
}

} // namespace teleport::ui
