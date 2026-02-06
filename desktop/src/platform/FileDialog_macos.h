/**
 * @file FileDialog_macos.h
 * @brief macOS native file dialog interface
 */

#ifndef TELEPORT_FILEDIALOG_MACOS_H
#define TELEPORT_FILEDIALOG_MACOS_H

#include <string>
#include <vector>

namespace teleport::ui {

/**
 * @brief Open a native file picker dialog
 * @param title Dialog window title
 * @return Vector of selected file paths (empty if cancelled)
 */
std::vector<std::string> OpenFileDialog(const std::string &title);

/**
 * @brief Open a native folder picker dialog
 * @param title Dialog window title
 * @return Selected folder path (empty if cancelled)
 */
std::string SelectFolderDialog(const std::string &title);

} // namespace teleport::ui

#endif // TELEPORT_FILEDIALOG_MACOS_H
