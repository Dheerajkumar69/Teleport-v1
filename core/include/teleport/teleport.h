/**
 * @file teleport.h
 * @brief Public C API for Teleport file transfer engine
 * 
 * This header provides the stable C ABI interface for cross-platform bindings.
 * All platform UIs (Windows, Android, macOS) use this API to interact with
 * the core transfer engine.
 * 
 * @note This API is designed for FFI (Foreign Function Interface) compatibility.
 *       All types are C-compatible, no C++ constructs exposed.
 */

#ifndef TELEPORT_H
#define TELEPORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Platform Export Macros
 * ============================================================================ */

#if defined(_WIN32) || defined(_WIN64)
    #ifdef TELEPORT_STATIC
        /* Static library - no import/export needed */
        #define TELEPORT_API
    #elif defined(TELEPORT_EXPORTS)
        #define TELEPORT_API __declspec(dllexport)
    #else
        #define TELEPORT_API __declspec(dllimport)
    #endif
#else
    #define TELEPORT_API __attribute__((visibility("default")))
#endif

/* ============================================================================
 * Version Information
 * ============================================================================ */

#define TELEPORT_VERSION_MAJOR 1
#define TELEPORT_VERSION_MINOR 0
#define TELEPORT_VERSION_PATCH 0
#define TELEPORT_PROTOCOL_VERSION 1

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define TELEPORT_DISCOVERY_PORT     45454
#define TELEPORT_CONTROL_PORT_MIN   45455
#define TELEPORT_CONTROL_PORT_MAX   45555
#define TELEPORT_CHUNK_SIZE         (2 * 1024 * 1024)  /* 2 MB */
#define TELEPORT_PARALLEL_STREAMS   4
#define TELEPORT_DISCOVERY_INTERVAL 1000  /* milliseconds */
#define TELEPORT_DEVICE_TTL         5000  /* milliseconds */
#define TELEPORT_MAX_DEVICE_NAME    64
#define TELEPORT_UUID_SIZE          37    /* 36 chars + null terminator */

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum TeleportError {
    TELEPORT_OK = 0,
    TELEPORT_ERROR_INVALID_ARGUMENT = -1,
    TELEPORT_ERROR_OUT_OF_MEMORY = -2,
    TELEPORT_ERROR_SOCKET_CREATE = -3,
    TELEPORT_ERROR_SOCKET_BIND = -4,
    TELEPORT_ERROR_SOCKET_CONNECT = -5,
    TELEPORT_ERROR_SOCKET_SEND = -6,
    TELEPORT_ERROR_SOCKET_RECV = -7,
    TELEPORT_ERROR_FILE_OPEN = -8,
    TELEPORT_ERROR_FILE_READ = -9,
    TELEPORT_ERROR_FILE_WRITE = -10,
    TELEPORT_ERROR_PROTOCOL = -11,
    TELEPORT_ERROR_TIMEOUT = -12,
    TELEPORT_ERROR_CANCELLED = -13,
    TELEPORT_ERROR_REJECTED = -14,
    TELEPORT_ERROR_ALREADY_RUNNING = -15,
    TELEPORT_ERROR_NOT_RUNNING = -16,
    TELEPORT_ERROR_NETWORK_UNREACHABLE = -17,
    TELEPORT_ERROR_DEVICE_NOT_FOUND = -18,
    TELEPORT_ERROR_TRANSFER_FAILED = -19,
    TELEPORT_ERROR_INTERNAL = -100
} TeleportError;

/* ============================================================================
 * Transfer State
 * ============================================================================ */

typedef enum TeleportTransferState {
    TELEPORT_STATE_IDLE = 0,
    TELEPORT_STATE_CONNECTING,
    TELEPORT_STATE_HANDSHAKING,
    TELEPORT_STATE_TRANSFERRING,
    TELEPORT_STATE_PAUSED,
    TELEPORT_STATE_COMPLETING,
    TELEPORT_STATE_COMPLETE,
    TELEPORT_STATE_FAILED,
    TELEPORT_STATE_CANCELLED
} TeleportTransferState;

/* ============================================================================
 * Opaque Handle Types
 * ============================================================================ */

typedef struct TeleportEngine TeleportEngine;
typedef struct TeleportTransfer TeleportTransfer;

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * @brief Device information discovered on the network
 */
typedef struct TeleportDevice {
    char id[TELEPORT_UUID_SIZE];           /**< Unique session ID (UUID v4) */
    char name[TELEPORT_MAX_DEVICE_NAME];   /**< User-friendly device name */
    char os[16];                           /**< Operating system identifier */
    char ip[46];                           /**< IP address (supports IPv6 length) */
    uint16_t port;                         /**< Control channel port */
    uint32_t capabilities;                 /**< Capability flags (bitmask) */
    int64_t last_seen_ms;                  /**< Timestamp of last discovery packet */
} TeleportDevice;

/**
 * @brief File information for transfer
 */
typedef struct TeleportFileInfo {
    uint32_t id;                           /**< File ID within transfer session */
    const char* path;                      /**< Full file path */
    const char* name;                      /**< File name only */
    uint64_t size;                         /**< File size in bytes */
} TeleportFileInfo;

/**
 * @brief Transfer progress information
 */
typedef struct TeleportProgress {
    uint32_t file_id;                      /**< Current file ID */
    const char* file_name;                 /**< Current file name */
    uint64_t file_bytes_transferred;       /**< Bytes transferred for current file */
    uint64_t file_bytes_total;             /**< Total bytes for current file */
    uint64_t total_bytes_transferred;      /**< Total bytes across all files */
    uint64_t total_bytes_total;            /**< Total bytes across all files */
    uint32_t files_completed;              /**< Number of files completed */
    uint32_t files_total;                  /**< Total number of files */
    double speed_bytes_per_sec;            /**< Current transfer speed */
    int32_t eta_seconds;                   /**< Estimated time remaining (-1 if unknown) */
} TeleportProgress;

/* ============================================================================
 * Capability Flags
 * ============================================================================ */

#define TELEPORT_CAP_PARALLEL    (1 << 0)  /**< Supports parallel streams */
#define TELEPORT_CAP_RESUME      (1 << 1)  /**< Supports transfer resume */
#define TELEPORT_CAP_COMPRESS    (1 << 2)  /**< Supports compression (future) */
#define TELEPORT_CAP_ENCRYPT     (1 << 3)  /**< Supports encryption (future) */

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/**
 * @brief Callback invoked when a device is discovered or updated
 * @param device Pointer to device information (valid only during callback)
 * @param user_data User context pointer passed to start_discovery
 */
typedef void (*TeleportDeviceCallback)(const TeleportDevice* device, void* user_data);

/**
 * @brief Callback invoked when a device is no longer visible (TTL expired)
 * @param device_id The ID of the device that expired
 * @param user_data User context pointer
 */
typedef void (*TeleportDeviceLostCallback)(const char* device_id, void* user_data);

/**
 * @brief Callback invoked on transfer progress updates
 * @param progress Progress information
 * @param user_data User context pointer
 */
typedef void (*TeleportProgressCallback)(const TeleportProgress* progress, void* user_data);

/**
 * @brief Callback invoked when an incoming transfer is requested
 * @param sender The device requesting to send files
 * @param files Array of file information
 * @param file_count Number of files
 * @param user_data User context pointer
 * @return Non-zero to accept, zero to reject
 */
typedef int (*TeleportIncomingCallback)(
    const TeleportDevice* sender,
    const TeleportFileInfo* files,
    size_t file_count,
    void* user_data
);

/**
 * @brief Callback invoked when transfer completes (success or failure)
 * @param error TELEPORT_OK on success, error code otherwise
 * @param user_data User context pointer
 */
typedef void (*TeleportCompleteCallback)(TeleportError error, void* user_data);

/* ============================================================================
 * Engine Configuration
 * ============================================================================ */

typedef struct TeleportConfig {
    const char* device_name;               /**< Name to advertise (NULL = auto) */
    uint16_t control_port;                 /**< Preferred control port (0 = auto) */
    uint32_t chunk_size;                   /**< Chunk size in bytes (0 = default) */
    uint8_t parallel_streams;              /**< Number of parallel streams (0 = default) */
    uint32_t discovery_interval_ms;        /**< Discovery broadcast interval (0 = default) */
    uint32_t device_ttl_ms;                /**< Device expiration timeout (0 = default) */
    const char* download_path;             /**< Default download directory (NULL = cwd) */
} TeleportConfig;

/* ============================================================================
 * Engine Lifecycle
 * ============================================================================ */

/**
 * @brief Create and initialize the Teleport engine
 * @param config Configuration (NULL for defaults)
 * @param out_engine Output pointer to receive engine handle
 * @return TELEPORT_OK on success, error code otherwise
 */
TELEPORT_API TeleportError teleport_create(
    const TeleportConfig* config,
    TeleportEngine** out_engine
);

/**
 * @brief Destroy the Teleport engine and free all resources
 * @param engine Engine handle (safe to pass NULL)
 */
TELEPORT_API void teleport_destroy(TeleportEngine* engine);

/**
 * @brief Get the last error message for the engine
 * @param engine Engine handle
 * @return Human-readable error message (valid until next API call)
 */
TELEPORT_API const char* teleport_get_error_message(TeleportEngine* engine);

/* ============================================================================
 * Discovery
 * ============================================================================ */

/**
 * @brief Start device discovery on the local network
 * @param engine Engine handle
 * @param on_device Callback for discovered devices
 * @param on_lost Callback for expired devices (may be NULL)
 * @param user_data Context pointer passed to callbacks
 * @return TELEPORT_OK on success
 */
TELEPORT_API TeleportError teleport_start_discovery(
    TeleportEngine* engine,
    TeleportDeviceCallback on_device,
    TeleportDeviceLostCallback on_lost,
    void* user_data
);

/**
 * @brief Stop device discovery
 * @param engine Engine handle
 * @return TELEPORT_OK on success
 */
TELEPORT_API TeleportError teleport_stop_discovery(TeleportEngine* engine);

/**
 * @brief Get list of currently known devices
 * @param engine Engine handle
 * @param out_devices Output array (caller allocated)
 * @param max_devices Maximum number of devices to return
 * @param out_count Actual number of devices returned
 * @return TELEPORT_OK on success
 */
TELEPORT_API TeleportError teleport_get_devices(
    TeleportEngine* engine,
    TeleportDevice* out_devices,
    size_t max_devices,
    size_t* out_count
);

/* ============================================================================
 * Sending Files
 * ============================================================================ */

/**
 * @brief Send files to a remote device
 * @param engine Engine handle
 * @param target Target device (from discovery)
 * @param file_paths Array of file paths to send
 * @param file_count Number of files
 * @param on_progress Progress callback (may be NULL)
 * @param on_complete Completion callback
 * @param user_data Context pointer
 * @param out_transfer Output transfer handle
 * @return TELEPORT_OK on success starting transfer
 */
TELEPORT_API TeleportError teleport_send_files(
    TeleportEngine* engine,
    const TeleportDevice* target,
    const char** file_paths,
    size_t file_count,
    TeleportProgressCallback on_progress,
    TeleportCompleteCallback on_complete,
    void* user_data,
    TeleportTransfer** out_transfer
);

/* ============================================================================
 * Receiving Files
 * ============================================================================ */

/**
 * @brief Start listening for incoming file transfers
 * @param engine Engine handle
 * @param output_dir Directory to save received files
 * @param on_incoming Callback to accept/reject incoming transfers
 * @param on_progress Progress callback (may be NULL)
 * @param on_complete Completion callback
 * @param user_data Context pointer
 * @return TELEPORT_OK on success
 */
TELEPORT_API TeleportError teleport_start_receiving(
    TeleportEngine* engine,
    const char* output_dir,
    TeleportIncomingCallback on_incoming,
    TeleportProgressCallback on_progress,
    TeleportCompleteCallback on_complete,
    void* user_data
);

/**
 * @brief Stop listening for incoming transfers
 * @param engine Engine handle
 * @return TELEPORT_OK on success
 */
TELEPORT_API TeleportError teleport_stop_receiving(TeleportEngine* engine);

/* ============================================================================
 * Transfer Control
 * ============================================================================ */

/**
 * @brief Pause an active transfer
 * @param transfer Transfer handle
 * @return TELEPORT_OK on success
 */
TELEPORT_API TeleportError teleport_transfer_pause(TeleportTransfer* transfer);

/**
 * @brief Resume a paused transfer
 * @param transfer Transfer handle
 * @return TELEPORT_OK on success
 */
TELEPORT_API TeleportError teleport_transfer_resume(TeleportTransfer* transfer);

/**
 * @brief Cancel an active transfer
 * @param transfer Transfer handle
 * @return TELEPORT_OK on success
 */
TELEPORT_API TeleportError teleport_transfer_cancel(TeleportTransfer* transfer);

/**
 * @brief Get current transfer state
 * @param transfer Transfer handle
 * @return Current state
 */
TELEPORT_API TeleportTransferState teleport_transfer_get_state(TeleportTransfer* transfer);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Get this device's local IP address on the network
 * @param out_ip Output buffer for IP string
 * @param ip_size Size of output buffer (at least 46 bytes for IPv6)
 * @return TELEPORT_OK on success
 */
TELEPORT_API TeleportError teleport_get_local_ip(char* out_ip, size_t ip_size);

/**
 * @brief Get human-readable error description
 * @param error Error code
 * @return Error description string
 */
TELEPORT_API const char* teleport_error_string(TeleportError error);

/**
 * @brief Format bytes as human-readable string (e.g., "1.5 GB")
 * @param bytes Number of bytes
 * @param out_str Output buffer
 * @param str_size Size of output buffer
 */
TELEPORT_API void teleport_format_bytes(uint64_t bytes, char* out_str, size_t str_size);

/**
 * @brief Format duration as human-readable string (e.g., "2m 30s")
 * @param seconds Duration in seconds
 * @param out_str Output buffer
 * @param str_size Size of output buffer
 */
TELEPORT_API void teleport_format_duration(int32_t seconds, char* out_str, size_t str_size);

#ifdef __cplusplus
}
#endif

#endif /* TELEPORT_H */
