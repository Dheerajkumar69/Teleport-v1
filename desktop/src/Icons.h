/**
 * @file Icons.h
 * @brief Unicode text-based icons for cross-platform compatibility
 *
 * Uses Unicode symbols that work in most system fonts.
 * No icon font dependency required.
 */

#ifndef TELEPORT_ICONS_H
#define TELEPORT_ICONS_H

namespace teleport::ui {
namespace Icons {

// Navigation & Actions
constexpr const char *SEND = "↑";     // U+2191 Upwards Arrow
constexpr const char *RECEIVE = "↓";  // U+2193 Downwards Arrow
constexpr const char *TRANSFER = "⇄"; // U+21C4 Rightwards Over Leftwards
constexpr const char *SETTINGS = "⚙"; // U+2699 Gear
constexpr const char *DISCOVER = "◎"; // U+25CE Bullseye

// File Operations
constexpr const char *FOLDER = "📁"; // U+1F4C1 File Folder
constexpr const char *FILE = "📄";   // U+1F4C4 Page Facing Up
constexpr const char *ADD = "+";     // Plus
constexpr const char *REMOVE = "−";  // U+2212 Minus Sign

// Status
constexpr const char *CHECK = "✓"; // U+2713 Check Mark
constexpr const char *CROSS = "✗"; // U+2717 Ballot X
constexpr const char *ERROR = "⚠"; // U+26A0 Warning
constexpr const char *INFO = "ℹ";  // U+2139 Information

// Media Controls
constexpr const char *PLAY = "▶";   // U+25B6 Play
constexpr const char *PAUSE = "⏸";  // U+23F8 Pause
constexpr const char *STOP = "◼";   // U+25FC Stop
constexpr const char *CANCEL = "✕"; // U+2715 Multiplication X

// Connection
constexpr const char *QR_CODE = "⊞";    // U+229E Squared Plus
constexpr const char *WIFI = "◉";       // U+25C9 Fisheye
constexpr const char *HOTSPOT = "☀";    // U+2600 Sun
constexpr const char *LINK = "🔗";      // U+1F517 Link
constexpr const char *DISCONNECT = "⊝"; // U+229D Circle Minus

// Devices
constexpr const char *DESKTOP = "🖥"; // U+1F5A5 Desktop Computer
constexpr const char *LAPTOP = "💻";  // U+1F4BB Laptop
constexpr const char *PHONE = "📱";   // U+1F4F1 Mobile Phone
constexpr const char *TABLET = "📱";  // U+1F4F1 (same as phone)
constexpr const char *DEVICE = "📟";  // U+1F4DF Generic device

// Arrows & Direction
constexpr const char *ARROW_UP = "↑";    // U+2191
constexpr const char *ARROW_DOWN = "↓";  // U+2193
constexpr const char *ARROW_LEFT = "←";  // U+2190
constexpr const char *ARROW_RIGHT = "→"; // U+2192
constexpr const char *REFRESH = "↻";     // U+21BB Clockwise Arrow

// Misc
constexpr const char *SEARCH = "🔍";  // U+1F50D Magnifying Glass
constexpr const char *COPY = "📋";    // U+1F4CB Clipboard
constexpr const char *EDIT = "✎";     // U+270E Pencil
constexpr const char *TRASH = "🗑";   // U+1F5D1 Wastebasket
constexpr const char *EXPAND = "⊕";   // U+2295 Circled Plus
constexpr const char *COLLAPSE = "⊖"; // U+2296 Circled Minus

// Dark/Light mode
constexpr const char *SUN = "☀";  // U+2600 Light mode
constexpr const char *MOON = "☾"; // U+263E Dark mode

} // namespace Icons
} // namespace teleport::ui

#endif // TELEPORT_ICONS_H
