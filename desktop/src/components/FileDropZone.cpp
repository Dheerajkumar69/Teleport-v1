/**
 * @file FileDropZone.cpp
 * @brief File drop zone implementation
 */

#include "FileDropZone.h"
#include "imgui.h"

namespace teleport::ui {

FileDropZone::FileDropZone(Theme* theme) : theme_(theme) {}

void FileDropZone::Update() {
    // Animation updates handled in SendView for now
}

void FileDropZone::Render(bool isDragging, std::vector<std::string>& files) {
    // Rendering handled in SendView for context-specific integration
}

} // namespace teleport::ui
