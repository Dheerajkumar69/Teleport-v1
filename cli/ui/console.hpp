/**
 * @file console.hpp
 * @brief Console UI utilities
 */

#ifndef TELEPORT_CONSOLE_HPP
#define TELEPORT_CONSOLE_HPP

#include <string>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace teleport {
namespace cli {

/**
 * @brief Enable ANSI colors on Windows
 */
inline void enable_colors() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

/**
 * @brief ANSI color codes
 */
namespace color {
    constexpr const char* reset   = "\033[0m";
    constexpr const char* bold    = "\033[1m";
    constexpr const char* dim     = "\033[2m";
    constexpr const char* red     = "\033[31m";
    constexpr const char* green   = "\033[32m";
    constexpr const char* yellow  = "\033[33m";
    constexpr const char* blue    = "\033[34m";
    constexpr const char* magenta = "\033[35m";
    constexpr const char* cyan    = "\033[36m";
    constexpr const char* white   = "\033[37m";
}

/**
 * @brief Clear current line
 */
inline void clear_line() {
    std::cout << "\r\033[K" << std::flush;
}

/**
 * @brief Move cursor up
 */
inline void cursor_up(int lines = 1) {
    std::cout << "\033[" << lines << "A" << std::flush;
}

/**
 * @brief Print a styled header
 */
inline void print_header(const std::string& text) {
    std::cout << color::cyan << color::bold << "▶ " << text << color::reset << std::endl;
}

/**
 * @brief Print an info message
 */
inline void print_info(const std::string& text) {
    std::cout << color::blue << "ℹ " << color::reset << text << std::endl;
}

/**
 * @brief Print a success message
 */
inline void print_success(const std::string& text) {
    std::cout << color::green << "✓ " << color::reset << text << std::endl;
}

/**
 * @brief Print a warning message
 */
inline void print_warning(const std::string& text) {
    std::cout << color::yellow << "⚠ " << color::reset << text << std::endl;
}

/**
 * @brief Print an error message
 */
inline void print_error(const std::string& text) {
    std::cout << color::red << "✗ " << color::reset << text << std::endl;
}

/**
 * @brief Print device in a formatted way
 */
inline void print_device(int index, const std::string& name, const std::string& ip, 
                          const std::string& os) {
    std::cout << color::yellow << "[" << index << "] " << color::reset
              << color::bold << name << color::reset
              << "  " << color::dim << ip << color::reset
              << "  " << color::cyan << os << color::reset << std::endl;
}

/**
 * @brief Simple yes/no prompt
 */
inline bool prompt_yes_no(const std::string& question, bool default_yes = true) {
    std::cout << question << (default_yes ? " [Y/n]: " : " [y/N]: ");
    std::string response;
    std::getline(std::cin, response);
    
    if (response.empty()) return default_yes;
    
    char c = std::tolower(response[0]);
    return c == 'y';
}

} // namespace cli
} // namespace teleport

#endif // TELEPORT_CONSOLE_HPP
