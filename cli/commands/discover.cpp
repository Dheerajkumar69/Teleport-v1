/**
 * @file discover.cpp
 * @brief Discovery command implementation
 */

#include "discover.hpp"
#include "teleport/teleport.h"
#include "ui/console.hpp"

#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>
#include <vector>
#include <mutex>
#include <cstdlib>

namespace teleport {
namespace cli {

// Signal handler for Ctrl+C
static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running.store(false);
}

/**
 * @brief Context structure for discovery callbacks (properly typed)
 */
struct DiscoveryContext {
    std::vector<TeleportDevice> devices;
    std::mutex mutex;
    int next_index = 1;
};

static void on_device_found(const TeleportDevice* device, void* ctx) {
    if (!device || !ctx) return;
    
    auto* context = static_cast<DiscoveryContext*>(ctx);
    std::lock_guard<std::mutex> lock(context->mutex);
    
    // Check if already in list (update existing)
    for (auto& entry : context->devices) {
        if (std::string(entry.id) == device->id) {
            entry = *device;
            return;
        }
    }
    
    // New device - add to list and print
    context->devices.push_back(*device);
    int index = context->next_index++;
    
    print_device(
        index,
        device->name,
        device->ip,
        device->os
    );
}

static void on_device_lost(const char* device_id, void* ctx) {
    if (!device_id || !ctx) return;
    
    auto* context = static_cast<DiscoveryContext*>(ctx);
    std::lock_guard<std::mutex> lock(context->mutex);
    
    // Remove from list (optional: just log for now)
    // Could implement visual indicator of device going offline
}

int discover_command(const std::vector<std::string>& args) {
    enable_colors();
    
    // Parse arguments with validation
    int timeout_sec = 10;
    
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--timeout" && i + 1 < args.size()) {
            try {
                timeout_sec = std::stoi(args[i + 1]);
                if (timeout_sec <= 0 || timeout_sec > 300) {
                    print_warning("Timeout must be between 1-300 seconds, using 10");
                    timeout_sec = 10;
                }
            } catch (const std::exception&) {
                print_warning("Invalid timeout value, using 10 seconds");
                timeout_sec = 10;
            }
            ++i;
        }
    }
    
    print_header("Discovering devices on local network...");
    print_info("Press Ctrl+C to stop (timeout: " + std::to_string(timeout_sec) + "s)");
    std::cout << std::endl;
    
    // Set up signal handler
    std::signal(SIGINT, signal_handler);
    g_running.store(true);
    
    // Create engine
    TeleportEngine* engine = nullptr;
    TeleportConfig config = {};
    
    auto err = teleport_create(&config, &engine);
    if (err != TELEPORT_OK) {
        print_error("Failed to create engine: " + std::string(teleport_error_string(err)));
        return 1;
    }
    
    // Properly typed context for callbacks
    DiscoveryContext context;
    
    // Start discovery with properly typed callbacks
    err = teleport_start_discovery(
        engine,
        on_device_found,
        on_device_lost,
        &context
    );
    
    if (err != TELEPORT_OK) {
        print_error("Failed to start discovery: " + std::string(teleport_error_string(err)));
        teleport_destroy(engine);
        return 1;
    }
    
    // Wait for timeout or Ctrl+C
    auto start = std::chrono::steady_clock::now();
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start
        ).count();
        
        if (elapsed >= timeout_sec) {
            break;
        }
    }
    
    // Stop discovery
    teleport_stop_discovery(engine);
    
    std::cout << std::endl;
    
    // Print summary using engine's device list (authoritative)
    TeleportDevice found_devices[64];
    size_t count = 0;
    teleport_get_devices(engine, found_devices, 64, &count);
    
    if (count == 0) {
        print_warning("No devices found");
    } else {
        print_success("Found " + std::to_string(count) + " device(s)");
        std::cout << std::endl;
        
        std::cout << color::bold << "Available devices:" << color::reset << std::endl;
        for (size_t i = 0; i < count; ++i) {
            print_device(
                static_cast<int>(i + 1),
                found_devices[i].name,
                found_devices[i].ip,
                found_devices[i].os
            );
        }
        
        std::cout << std::endl;
        print_info("Use 'teleport send <file> --to <number>' to send files");
    }
    
    teleport_destroy(engine);
    return 0;
}

} // namespace cli
} // namespace teleport
