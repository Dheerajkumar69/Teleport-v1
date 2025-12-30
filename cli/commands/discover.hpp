/**
 * @file discover.hpp
 * @brief Discovery command implementation
 */

#ifndef TELEPORT_DISCOVER_CMD_HPP
#define TELEPORT_DISCOVER_CMD_HPP

#include <vector>
#include <string>

namespace teleport {
namespace cli {

/**
 * @brief Execute discover command
 */
int discover_command(const std::vector<std::string>& args);

} // namespace cli
} // namespace teleport

#endif // TELEPORT_DISCOVER_CMD_HPP
