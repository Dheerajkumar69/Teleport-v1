/**
 * @file FileDialog_linux.cpp
 * @brief Linux file dialog implementation using zenity
 */

#include "FileDialog_linux.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace teleport::ui {

namespace {

std::string ExecuteCommand(const std::string &cmd) {
  std::array<char, 4096> buffer;
  std::string result;

  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    return "";
  }

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result += buffer.data();
  }

  pclose(pipe);

  // Remove trailing newline
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
    result.pop_back();
  }

  return result;
}

bool HasZenity() { return system("which zenity > /dev/null 2>&1") == 0; }

bool HasKdialog() { return system("which kdialog > /dev/null 2>&1") == 0; }

} // anonymous namespace

std::vector<std::string> OpenFileDialog(const std::string &title,
                                        bool multiple) {
  std::vector<std::string> files;
  std::string result;

  if (HasZenity()) {
    std::string cmd = "zenity --file-selection";
    cmd += " --title=\"" + title + "\"";
    if (multiple) {
      cmd += " --multiple --separator=\"|\"";
    }
    cmd += " 2>/dev/null";
    result = ExecuteCommand(cmd);
  } else if (HasKdialog()) {
    std::string cmd = "kdialog --getopenfilename";
    if (multiple) {
      cmd = "kdialog --getopenfilename . --multiple";
    }
    cmd += " --title \"" + title + "\"";
    cmd += " 2>/dev/null";
    result = ExecuteCommand(cmd);
  }

  if (result.empty()) {
    return files;
  }

  // Parse multiple files (separated by | in zenity)
  std::istringstream iss(result);
  std::string path;
  char delimiter = (HasZenity() && multiple) ? '|' : '\n';

  while (std::getline(iss, path, delimiter)) {
    if (!path.empty()) {
      files.push_back(path);
    }
  }

  return files;
}

std::string SelectFolderDialog(const std::string &title) {
  std::string result;

  if (HasZenity()) {
    std::string cmd = "zenity --file-selection --directory";
    cmd += " --title=\"" + title + "\"";
    cmd += " 2>/dev/null";
    result = ExecuteCommand(cmd);
  } else if (HasKdialog()) {
    std::string cmd = "kdialog --getexistingdirectory .";
    cmd += " --title \"" + title + "\"";
    cmd += " 2>/dev/null";
    result = ExecuteCommand(cmd);
  }

  return result;
}

std::string SaveFileDialog(const std::string &title,
                           const std::string &defaultName) {
  std::string result;

  if (HasZenity()) {
    std::string cmd = "zenity --file-selection --save --confirm-overwrite";
    cmd += " --title=\"" + title + "\"";
    if (!defaultName.empty()) {
      cmd += " --filename=\"" + defaultName + "\"";
    }
    cmd += " 2>/dev/null";
    result = ExecuteCommand(cmd);
  } else if (HasKdialog()) {
    std::string cmd = "kdialog --getsavefilename";
    if (!defaultName.empty()) {
      cmd += " \"" + defaultName + "\"";
    } else {
      cmd += " .";
    }
    cmd += " --title \"" + title + "\"";
    cmd += " 2>/dev/null";
    result = ExecuteCommand(cmd);
  }

  return result;
}

} // namespace teleport::ui
