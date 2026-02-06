/**
 * @file FileDialog_linux.h
 * @brief Cross-platform file dialog interface for Linux
 */

#pragma once

#include <string>
#include <vector>

namespace teleport::ui {

/**
 * @brief Open a file selection dialog
 * @param title Dialog title
 * @param multiple Allow multiple file selection
 * @return Vector of selected file paths (empty if cancelled)
 */
std::vector<std::string>
OpenFileDialog(const std::string &title = "Select Files", bool multiple = true);

/**
 * @brief Open a folder selection dialog
 * @param title Dialog title
 * @return Selected folder path (empty if cancelled)
 */
std::string SelectFolderDialog(const std::string &title = "Select Folder");

/**
 * @brief Open a save file dialog
 * @param title Dialog title
 * @param defaultName Default filename
 * @return Selected save path (empty if cancelled)
 */
std::string SaveFileDialog(const std::string &title = "Save As",
                           const std::string &defaultName = "");

} // namespace teleport::ui
