/**
 * @file FileDialog_macos.cpp
 * @brief macOS native file dialog implementation using osascript
 *
 * Uses AppleScript via osascript for native file dialogs.
 * This approach works without Objective-C dependencies.
 */

#include "FileDialog_macos.h"
#include <array>
#include <cstdio>
#include <memory>
#include <sstream>

namespace teleport::ui {

/**
 * @brief Execute a command and capture its output
 * @param cmd Command to execute
 * @return Command output (trimmed)
 */
static std::string ExecuteCommand(const std::string &cmd) {
  std::array<char, 256> buffer;
  std::string result;

  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"),
                                                pclose);
  if (!pipe) {
    return "";
  }

  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }

  // Trim trailing whitespace
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r' ||
                             result.back() == ' ')) {
    result.pop_back();
  }

  return result;
}

std::vector<std::string> OpenFileDialog(const std::string &title) {
  std::vector<std::string> result;

  // Use osascript to show native file picker
  std::string cmd =
      "osascript -e 'set theFiles to choose file with prompt \"" + title +
      "\" with multiple selections allowed' "
      "-e 'set output to \"\"' "
      "-e 'repeat with theFile in theFiles' "
      "-e 'set output to output & POSIX path of theFile & \"\\n\"' "
      "-e 'end repeat' "
      "-e 'return output' 2>/dev/null";

  std::string output = ExecuteCommand(cmd);

  if (!output.empty()) {
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
      if (!line.empty()) {
        result.push_back(line);
      }
    }
  }

  return result;
}

std::string SelectFolderDialog(const std::string &title) {
  // Use osascript to show native folder picker
  std::string cmd =
      "osascript -e 'POSIX path of (choose folder with prompt \"" + title +
      "\")' 2>/dev/null";

  std::string result = ExecuteCommand(cmd);

  // Remove trailing slash if present
  if (!result.empty() && result.back() == '/') {
    result.pop_back();
  }

  return result;
}

} // namespace teleport::ui
