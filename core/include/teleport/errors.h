/**
 * @file errors.h
 * @brief Error handling utilities for Teleport core
 */

#ifndef TELEPORT_ERRORS_H
#define TELEPORT_ERRORS_H

#include "teleport/teleport.h"
#include "teleport/types.h"
#include <string>

namespace teleport {

/**
 * @brief Get human-readable error description
 */
inline const char* error_to_string(TeleportError err) {
    switch (err) {
        case TELEPORT_OK: return "Success";
        case TELEPORT_ERROR_INVALID_ARGUMENT: return "Invalid argument";
        case TELEPORT_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case TELEPORT_ERROR_SOCKET_CREATE: return "Failed to create socket";
        case TELEPORT_ERROR_SOCKET_BIND: return "Failed to bind socket";
        case TELEPORT_ERROR_SOCKET_CONNECT: return "Failed to connect";
        case TELEPORT_ERROR_SOCKET_SEND: return "Failed to send data";
        case TELEPORT_ERROR_SOCKET_RECV: return "Failed to receive data";
        case TELEPORT_ERROR_FILE_OPEN: return "Failed to open file";
        case TELEPORT_ERROR_FILE_READ: return "Failed to read file";
        case TELEPORT_ERROR_FILE_WRITE: return "Failed to write file";
        case TELEPORT_ERROR_PROTOCOL: return "Protocol error";
        case TELEPORT_ERROR_TIMEOUT: return "Operation timed out";
        case TELEPORT_ERROR_CANCELLED: return "Operation cancelled";
        case TELEPORT_ERROR_REJECTED: return "Transfer rejected";
        case TELEPORT_ERROR_ALREADY_RUNNING: return "Operation already running";
        case TELEPORT_ERROR_NOT_RUNNING: return "Operation not running";
        case TELEPORT_ERROR_NETWORK_UNREACHABLE: return "Network unreachable";
        case TELEPORT_ERROR_DEVICE_NOT_FOUND: return "Device not found";
        case TELEPORT_ERROR_TRANSFER_FAILED: return "Transfer failed";
        case TELEPORT_ERROR_INTERNAL: return "Internal error";
        default: return "Unknown error";
    }
}

/**
 * @brief Create an Error from TeleportError code
 */
inline Error make_error(TeleportError code, const std::string& detail = "") {
    std::string msg = error_to_string(code);
    if (!detail.empty()) {
        msg += ": " + detail;
    }
    return Error(static_cast<int>(code), std::move(msg));
}

/**
 * @brief Success result
 */
inline Result<void> ok() {
    return Result<void>();
}

/**
 * @brief Helper macro for early return on error
 */
#define TELEPORT_TRY(expr) \
    do { \
        auto _result = (expr); \
        if (!_result.ok()) { \
            return _result.error(); \
        } \
    } while (0)

/**
 * @brief Helper macro for early return on error, extracting value
 */
#define TELEPORT_TRY_ASSIGN(var, expr) \
    auto _result_##var = (expr); \
    if (!_result_##var.ok()) { \
        return _result_##var.error(); \
    } \
    auto var = std::move(*_result_##var)

} // namespace teleport

#endif // TELEPORT_ERRORS_H
