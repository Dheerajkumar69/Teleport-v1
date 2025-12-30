/**
 * @file send.cpp
 * @brief Send command implementation
 */

#include "send.hpp"
#include "teleport/teleport.h"
#include "ui/console.hpp"
#include "ui/progress.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <algorithm>
#include <cctype>

namespace teleport {
namespace cli {

static std::atomic<bool> g_transfer_done{false};
static std::atomic<TeleportError> g_transfer_error{TELEPORT_OK};
static ProgressBar g_progress;

void on_send_progress(const TeleportProgress* progress, void*) {
    g_progress.update(progress);
}

void on_send_complete(TeleportError error, void*) {
    g_transfer_error.store(error);
    g_transfer_done.store(true);
}

int send_command(const std::vector<std::string>& args) {
    enable_colors();
    
    // Parse arguments
    std::vector<std::string> file_paths;
    std::string target;
    uint16_t port = 0;
    
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--to" && i + 1 < args.size()) {
            target = args[++i];
        } else if (args[i] == "--port" && i + 1 < args.size()) {
            port = static_cast<uint16_t>(std::stoi(args[++i]));
        } else if (args[i][0] != '-') {
            file_paths.push_back(args[i]);
        }
    }
    
    if (file_paths.empty()) {
        print_error("No files specified");
        std::cout << "Usage: teleport send <files...> --to <device>" << std::endl;
        return 1;
    }
    
    if (target.empty()) {
        print_error("No target device specified");
        std::cout << "Usage: teleport send <files...> --to <device>" << std::endl;
        return 1;
    }
    
    // Create engine
    TeleportEngine* engine = nullptr;
    TeleportConfig config = {};
    
    auto err = teleport_create(&config, &engine);
    if (err != TELEPORT_OK) {
        print_error("Failed to create engine: " + std::string(teleport_error_string(err)));
        return 1;
    }
    
    // Resolve target
    TeleportDevice target_device = {};
    bool target_found = false;
    
    // Check if target is a number (device index from discovery)
    bool is_numeric = !target.empty() && std::all_of(target.begin(), target.end(), ::isdigit);
    
    if (is_numeric) {
        int device_index = 0;
        try {
            device_index = std::stoi(target) - 1;  // 1-indexed to 0-indexed
        } catch (const std::exception&) {
            print_error("Invalid device number: " + target);
            teleport_destroy(engine);
            return 1;
        }
        
        if (device_index < 0) {
            print_error("Device number must be >= 1");
            teleport_destroy(engine);
            return 1;
        }
        
        print_info("Discovering devices...");
        
        err = teleport_start_discovery(engine, nullptr, nullptr, nullptr);
        if (err != TELEPORT_OK) {
            print_error("Discovery failed");
            teleport_destroy(engine);
            return 1;
        }
        
        // Wait for discovery
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        TeleportDevice devices[64];
        size_t count = 0;
        teleport_get_devices(engine, devices, 64, &count);
        teleport_stop_discovery(engine);
        
        if (static_cast<size_t>(device_index) < count) {
            target_device = devices[device_index];
            target_found = true;
        } else {
            print_error("Device " + target + " not found. Available devices: " + std::to_string(count));
            teleport_destroy(engine);
            return 1;
        }
    } else {
        // Target is an IP address - validate it
        if (target.size() < 7 || target.size() > 15) {  // x.x.x.x min, xxx.xxx.xxx.xxx max
            print_error("Invalid IP address format: " + target);
            teleport_destroy(engine);
            return 1;
        }
        
        strncpy(target_device.ip, target.c_str(), sizeof(target_device.ip) - 1);
        target_device.ip[sizeof(target_device.ip) - 1] = '\0';
        target_device.port = port > 0 ? port : TELEPORT_CONTROL_PORT_MIN;
        strncpy(target_device.name, target.c_str(), sizeof(target_device.name) - 1);
        target_device.name[sizeof(target_device.name) - 1] = '\0';
        target_found = true;
    }
    
    if (!target_found) {
        print_error("Could not resolve target device");
        teleport_destroy(engine);
        return 1;
    }
    
    // Print transfer info
    print_header("Sending files to " + std::string(target_device.name));
    std::cout << std::endl;
    
    for (const auto& path : file_paths) {
        std::cout << "  • " << path << std::endl;
    }
    std::cout << std::endl;
    
    // Convert file paths for C API
    std::vector<const char*> c_paths;
    for (const auto& path : file_paths) {
        c_paths.push_back(path.c_str());
    }
    
    // Start transfer
    g_transfer_done.store(false);
    g_transfer_error.store(TELEPORT_OK);
    
    TeleportTransfer* transfer = nullptr;
    err = teleport_send_files(
        engine,
        &target_device,
        c_paths.data(),
        c_paths.size(),
        on_send_progress,
        on_send_complete,
        nullptr,
        &transfer
    );
    
    if (err != TELEPORT_OK) {
        print_error("Failed to start transfer: " + std::string(teleport_error_string(err)));
        teleport_destroy(engine);
        return 1;
    }
    
    // Wait for transfer to complete
    while (!g_transfer_done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << std::endl;
    
    TeleportError final_error = g_transfer_error.load();
    if (final_error == TELEPORT_OK) {
        g_progress.complete();
    } else {
        g_progress.failed(teleport_error_string(final_error));
    }
    
    teleport_destroy(engine);
    return final_error == TELEPORT_OK ? 0 : 1;
}

} // namespace cli
} // namespace teleport
