/**
 * @file teleport.cpp
 * @brief Public C API implementation
 */

#include "teleport/teleport.h"
#include "teleport/errors.h"
#include "discovery/discovery.hpp"
#include "control/control_server.hpp"
#include "control/control_client.hpp"
#include "transfer/transfer_manager.hpp"
#include "platform/pal.hpp"
#include "utils/logger.hpp"

#include <memory>
#include <cstring>

// TeleportEngine and TeleportTransfer are defined OUTSIDE namespace to match C header
// The C header declares: typedef struct TeleportEngine TeleportEngine;

/**
 * @brief Engine state structure (matches C API declaration)
 */
struct TeleportEngine {
    teleport::Config config;
    std::unique_ptr<teleport::DiscoveryManager> discovery;
    std::unique_ptr<teleport::ControlServer> server;
    std::unique_ptr<teleport::ControlClient> client;
    std::unique_ptr<teleport::TransferManager> transfer_manager;
    teleport::pal::PlatformGuard platform_guard;
    std::string last_error;
    
    // Callbacks
    TeleportDeviceCallback on_device = nullptr;
    TeleportDeviceLostCallback on_device_lost = nullptr;
    void* callback_data = nullptr;
};

/**
 * @brief Transfer state structure (matches C API declaration)
 */
struct TeleportTransfer {
    teleport::ControlClient* client = nullptr;
    teleport::TransferState state = teleport::TransferState::Idle;
};

namespace teleport {
// Helper to copy device to C struct
void copy_device_to_c(const Device& src, TeleportDevice* dst) {
    strncpy(dst->id, src.id.c_str(), TELEPORT_UUID_SIZE - 1);
    dst->id[TELEPORT_UUID_SIZE - 1] = '\0';
    
    strncpy(dst->name, src.name.c_str(), TELEPORT_MAX_DEVICE_NAME - 1);
    dst->name[TELEPORT_MAX_DEVICE_NAME - 1] = '\0';
    
    auto os_str = os_to_string(src.os);
    strncpy(dst->os, os_str.c_str(), sizeof(dst->os) - 1);
    dst->os[sizeof(dst->os) - 1] = '\0';
    
    strncpy(dst->ip, src.address.ip.c_str(), sizeof(dst->ip) - 1);
    dst->ip[sizeof(dst->ip) - 1] = '\0';
    
    dst->port = src.address.port;
    dst->capabilities = static_cast<uint32_t>(src.capabilities);
    dst->last_seen_ms = src.last_seen_ms;
}

// Helper to copy C struct to device
Device copy_device_from_c(const TeleportDevice* src) {
    Device dst;
    dst.id = src->id;
    dst.name = src->name;
    dst.os = os_from_string(src->os);
    dst.address.ip = src->ip;
    dst.address.port = src->port;
    dst.capabilities = static_cast<Capability>(src->capabilities);
    dst.last_seen_ms = src->last_seen_ms;
    return dst;
}

} // namespace teleport

using namespace teleport;

/* ============================================================================
 * Engine Lifecycle
 * ============================================================================ */

extern "C" {

TELEPORT_API TeleportError teleport_create(
    const TeleportConfig* config,
    TeleportEngine** out_engine
) {
    if (!out_engine) {
        return TELEPORT_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        auto engine = new TeleportEngine();
        
        // Initialize platform
        if (!engine->platform_guard.ok()) {
            delete engine;
            return TELEPORT_ERROR_INTERNAL;
        }
        
        // Set up configuration
        if (config) {
            if (config->device_name) {
                engine->config.device_name = config->device_name;
            } else {
                engine->config.device_name = pal::get_device_name();
            }
            engine->config.control_port = config->control_port;
            engine->config.chunk_size = config->chunk_size > 0 
                ? config->chunk_size : TELEPORT_CHUNK_SIZE;
            engine->config.parallel_streams = config->parallel_streams > 0 
                ? config->parallel_streams : TELEPORT_PARALLEL_STREAMS;
            engine->config.discovery_interval_ms = config->discovery_interval_ms > 0 
                ? config->discovery_interval_ms : TELEPORT_DISCOVERY_INTERVAL;
            engine->config.device_ttl_ms = config->device_ttl_ms > 0 
                ? config->device_ttl_ms : TELEPORT_DEVICE_TTL;
            engine->config.download_path = config->download_path 
                ? config->download_path : ".";
        } else {
            engine->config = Config::with_defaults();
        }
        
        // Create components
        engine->discovery = std::make_unique<DiscoveryManager>(engine->config);
        engine->server = std::make_unique<ControlServer>(engine->config);
        engine->client = std::make_unique<ControlClient>(engine->config);
        engine->transfer_manager = std::make_unique<TransferManager>(engine->config);
        
        *out_engine = engine;
        LOG_INFO("Teleport engine created");
        
        return TELEPORT_OK;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create engine: ", e.what());
        return TELEPORT_ERROR_INTERNAL;
    }
}

TELEPORT_API void teleport_destroy(TeleportEngine* engine) {
    if (!engine) return;
    
    LOG_INFO("Destroying Teleport engine...");
    
    // First, cancel any active transfers with timeout wait
    if (engine->client) {
        auto state = engine->client->state();
        if (state != TransferState::Idle && state != TransferState::Complete) {
            LOG_INFO("Waiting for active transfer to complete...");
            engine->client->cancel();
            
            // Wait up to 5 seconds for transfer to stop
            constexpr int MAX_WAIT_MS = 5000;
            constexpr int POLL_INTERVAL_MS = 100;
            int waited = 0;
            
            while (engine->client->state() != TransferState::Idle && waited < MAX_WAIT_MS) {
                pal::sleep_ms(POLL_INTERVAL_MS);
                waited += POLL_INTERVAL_MS;
            }
            
            if (waited >= MAX_WAIT_MS) {
                LOG_WARN("Transfer did not complete within timeout, forcing shutdown");
            }
        }
    }
    
    // Stop receiving server (waits for any active connections)
    if (engine->server) {
        engine->server->stop();
    }
    
    // Stop discovery
    if (engine->discovery) {
        engine->discovery->stop();
    }
    
    // Now safe to delete
    delete engine;
    LOG_INFO("Teleport engine destroyed");
}

TELEPORT_API const char* teleport_get_error_message(TeleportEngine* engine) {
    if (!engine) return "Invalid engine";
    return engine->last_error.c_str();
}

/* ============================================================================
 * Discovery
 * ============================================================================ */

TELEPORT_API TeleportError teleport_start_discovery(
    TeleportEngine* engine,
    TeleportDeviceCallback on_device,
    TeleportDeviceLostCallback on_lost,
    void* user_data
) {
    if (!engine) return TELEPORT_ERROR_INVALID_ARGUMENT;
    
    engine->on_device = on_device;
    engine->on_device_lost = on_lost;
    engine->callback_data = user_data;
    
    auto result = engine->discovery->start(
        [engine](const Device& device) {
            if (engine->on_device) {
                TeleportDevice c_device;
                copy_device_to_c(device, &c_device);
                engine->on_device(&c_device, engine->callback_data);
            }
        },
        [engine](const std::string& device_id) {
            if (engine->on_device_lost) {
                engine->on_device_lost(device_id.c_str(), engine->callback_data);
            }
        }
    );
    
    if (!result) {
        engine->last_error = result.error().message;
        return static_cast<TeleportError>(result.error().code);
    }
    
    return TELEPORT_OK;
}

TELEPORT_API TeleportError teleport_stop_discovery(TeleportEngine* engine) {
    if (!engine) return TELEPORT_ERROR_INVALID_ARGUMENT;
    engine->discovery->stop();
    return TELEPORT_OK;
}

TELEPORT_API TeleportError teleport_get_devices(
    TeleportEngine* engine,
    TeleportDevice* out_devices,
    size_t max_devices,
    size_t* out_count
) {
    if (!engine || !out_devices || !out_count) {
        return TELEPORT_ERROR_INVALID_ARGUMENT;
    }
    
    auto devices = engine->discovery->devices().all();
    size_t count = std::min(devices.size(), max_devices);
    
    for (size_t i = 0; i < count; ++i) {
        copy_device_to_c(devices[i], &out_devices[i]);
    }
    
    *out_count = count;
    return TELEPORT_OK;
}

/* ============================================================================
 * Sending Files
 * ============================================================================ */

TELEPORT_API TeleportError teleport_send_files(
    TeleportEngine* engine,
    const TeleportDevice* target,
    const char** file_paths,
    size_t file_count,
    TeleportProgressCallback on_progress,
    TeleportCompleteCallback on_complete,
    void* user_data,
    TeleportTransfer** out_transfer
) {
    if (!engine || !target || !file_paths || file_count == 0) {
        return TELEPORT_ERROR_INVALID_ARGUMENT;
    }
    
    Device target_device = copy_device_from_c(target);
    
    std::vector<std::string> paths;
    for (size_t i = 0; i < file_count; ++i) {
        paths.push_back(file_paths[i]);
    }
    
    auto result = engine->client->send_files(
        target_device,
        paths,
        [on_progress, user_data](const TransferStats& stats) {
            if (on_progress) {
                TeleportProgress progress;
                progress.file_id = 0;
                progress.file_name = "";
                progress.file_bytes_transferred = 0;
                progress.file_bytes_total = 0;
                progress.total_bytes_transferred = stats.bytes_transferred;
                progress.total_bytes_total = stats.bytes_total;
                progress.files_completed = stats.files_completed;
                progress.files_total = stats.files_total;
                progress.speed_bytes_per_sec = stats.speed_bps;
                progress.eta_seconds = stats.eta_seconds;
                on_progress(&progress, user_data);
            }
        },
        [on_complete, user_data](TeleportError error) {
            if (on_complete) {
                on_complete(error, user_data);
            }
        }
    );
    
    if (!result) {
        engine->last_error = result.error().message;
        return static_cast<TeleportError>(result.error().code);
    }
    
    if (out_transfer) {
        auto transfer = new TeleportTransfer();
        transfer->client = engine->client.get();
        *out_transfer = transfer;
    }
    
    return TELEPORT_OK;
}

/* ============================================================================
 * Receiving Files
 * ============================================================================ */

TELEPORT_API TeleportError teleport_start_receiving(
    TeleportEngine* engine,
    const char* output_dir,
    TeleportIncomingCallback on_incoming,
    TeleportProgressCallback on_progress,
    TeleportCompleteCallback on_complete,
    void* user_data
) {
    if (!engine || !output_dir) {
        return TELEPORT_ERROR_INVALID_ARGUMENT;
    }
    
    engine->server->set_output_dir(output_dir);
    
    auto result = engine->server->start(
        [on_incoming, user_data](const IncomingTransfer& transfer) -> bool {
            if (!on_incoming) return false;
            
            TeleportDevice sender;
            copy_device_to_c(transfer.sender, &sender);
            
            std::vector<TeleportFileInfo> files;
            for (const auto& f : transfer.files) {
                TeleportFileInfo info;
                info.id = f.id;
                info.path = f.path.c_str();
                info.name = f.name.c_str();
                info.size = f.size;
                files.push_back(info);
            }
            
            return on_incoming(&sender, files.data(), files.size(), user_data) != 0;
        },
        [on_progress, user_data](const TransferStats& stats) {
            if (on_progress) {
                TeleportProgress progress;
                progress.file_id = 0;
                progress.file_name = "";
                progress.file_bytes_transferred = 0;
                progress.file_bytes_total = 0;
                progress.total_bytes_transferred = stats.bytes_transferred;
                progress.total_bytes_total = stats.bytes_total;
                progress.files_completed = stats.files_completed;
                progress.files_total = stats.files_total;
                progress.speed_bytes_per_sec = stats.speed_bps;
                progress.eta_seconds = stats.eta_seconds;
                on_progress(&progress, user_data);
            }
        },
        [on_complete, user_data](TeleportError error) {
            if (on_complete) {
                on_complete(error, user_data);
            }
        }
    );
    
    if (!result) {
        engine->last_error = result.error().message;
        return static_cast<TeleportError>(result.error().code);
    }
    
    return TELEPORT_OK;
}

TELEPORT_API TeleportError teleport_stop_receiving(TeleportEngine* engine) {
    if (!engine) return TELEPORT_ERROR_INVALID_ARGUMENT;
    engine->server->stop();
    return TELEPORT_OK;
}

/* ============================================================================
 * Transfer Control
 * ============================================================================ */

TELEPORT_API TeleportError teleport_transfer_pause(TeleportTransfer* transfer) {
    if (!transfer || !transfer->client) return TELEPORT_ERROR_INVALID_ARGUMENT;
    auto result = transfer->client->pause();
    return result ? TELEPORT_OK : static_cast<TeleportError>(result.error().code);
}

TELEPORT_API TeleportError teleport_transfer_resume(TeleportTransfer* transfer) {
    if (!transfer || !transfer->client) return TELEPORT_ERROR_INVALID_ARGUMENT;
    auto result = transfer->client->resume();
    return result ? TELEPORT_OK : static_cast<TeleportError>(result.error().code);
}

TELEPORT_API TeleportError teleport_transfer_cancel(TeleportTransfer* transfer) {
    if (!transfer || !transfer->client) return TELEPORT_ERROR_INVALID_ARGUMENT;
    auto result = transfer->client->cancel();
    return result ? TELEPORT_OK : static_cast<TeleportError>(result.error().code);
}

TELEPORT_API TeleportTransferState teleport_transfer_get_state(TeleportTransfer* transfer) {
    if (!transfer || !transfer->client) return TELEPORT_STATE_IDLE;
    return static_cast<TeleportTransferState>(transfer->client->state());
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

TELEPORT_API TeleportError teleport_get_local_ip(char* out_ip, size_t ip_size) {
    if (!out_ip || ip_size < 16) return TELEPORT_ERROR_INVALID_ARGUMENT;
    
    std::string ip = pal::get_primary_local_ip();
    strncpy(out_ip, ip.c_str(), ip_size - 1);
    out_ip[ip_size - 1] = '\0';
    
    return TELEPORT_OK;
}

TELEPORT_API const char* teleport_error_string(TeleportError error) {
    return error_to_string(error);
}

TELEPORT_API void teleport_format_bytes(uint64_t bytes, char* out_str, size_t str_size) {
    if (!out_str || str_size == 0) return;
    
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit_idx < 4) {
        size /= 1024.0;
        unit_idx++;
    }
    
    if (unit_idx == 0) {
        snprintf(out_str, str_size, "%.0f %s", size, units[unit_idx]);
    } else {
        snprintf(out_str, str_size, "%.2f %s", size, units[unit_idx]);
    }
}

TELEPORT_API void teleport_format_duration(int32_t seconds, char* out_str, size_t str_size) {
    if (!out_str || str_size == 0) return;
    
    if (seconds < 0) {
        snprintf(out_str, str_size, "--");
        return;
    }
    
    if (seconds < 60) {
        snprintf(out_str, str_size, "%ds", seconds);
    } else if (seconds < 3600) {
        snprintf(out_str, str_size, "%dm %ds", seconds / 60, seconds % 60);
    } else {
        snprintf(out_str, str_size, "%dh %dm", seconds / 3600, (seconds % 3600) / 60);
    }
}

} // extern "C"
