/**
 * @file FileDropZone.h
 * @brief Drag and drop file zone component
 */

#pragma once

#include "Theme.h"
#include <vector>
#include <string>

namespace teleport::ui {

class FileDropZone {
public:
    FileDropZone(Theme* theme);
    
    void Render(bool isDragging, std::vector<std::string>& files);
    void Update();
    
private:
    Theme* theme_;
    float borderAnimation_ = 0.0f;
    float iconAnimation_ = 0.0f;
};

} // namespace teleport::ui
