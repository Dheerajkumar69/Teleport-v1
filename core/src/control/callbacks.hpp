/**
 * @file callbacks.hpp
 * @brief Common callback type definitions for transfer operations
 */

#ifndef TELEPORT_CALLBACKS_HPP
#define TELEPORT_CALLBACKS_HPP

#include "teleport/teleport.h"
#include "teleport/types.h"
#include "platform/pal.hpp"
#include <functional>
#include <memory>

namespace teleport {

/**
 * @brief Callback for transfer progress updates
 */
using OnTransferProgress = std::function<void(const TransferStats& stats)>;

/**
 * @brief Callback for incoming transfer requests
 */
struct IncomingTransfer {
    Device sender;
    std::vector<FileInfo> files;
    uint64_t total_size;
    std::unique_ptr<pal::TcpSocket> socket;  // For server use
};

using OnIncomingTransfer = std::function<bool(const IncomingTransfer& transfer)>;

/**
 * @brief Callback for transfer completion
 */
using OnTransferComplete = std::function<void(TeleportError error)>;

} // namespace teleport

#endif // TELEPORT_CALLBACKS_HPP
