/**
 * @file send.hpp
 * @brief Send command implementation
 */

#ifndef TELEPORT_SEND_CMD_HPP
#define TELEPORT_SEND_CMD_HPP

#include <vector>
#include <string>

namespace teleport {
namespace cli {

/**
 * @brief Execute send command
 */
int send_command(const std::vector<std::string>& args);

} // namespace cli
} // namespace teleport

#endif // TELEPORT_SEND_CMD_HPP
