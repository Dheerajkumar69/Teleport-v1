/**
 * @file receive.cpp
 * @brief Receive command implementation
 */

#include "receive.hpp"
#include "teleport/teleport.h"
#include "ui/console.hpp"
#include "ui/progress.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

namespace teleport {
namespace cli {

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_transfer_done{false};
static std::atomic<TeleportError> g_transfer_error{TELEPORT_OK};
static bool g_auto_accept = false;
static ProgressBar g_recv_progress;

void recv_signal_handler(int) {
    g_running.store(false);
}

int on_incoming_transfer(
    const TeleportDevice* sender,
    const TeleportFileInfo* files,
    size_t file_count,
    void*
) {
    std::cout << std::endl;
    print_header("Incoming transfer from " + std::string(sender->name));
    std::cout << "IP: " << sender->ip << std::endl;
    std::cout << std::endl;
    
    uint64_t total_size = 0;
    for (size_t i = 0; i < file_count; ++i) {
        char size_str[32];
        teleport_format_bytes(files[i].size, size_str, sizeof(size_str));
        std::cout << "  • " << files[i].name << " (" << size_str << ")" << std::endl;
        total_size += files[i].size;
    }
    
    char total_str[32];
    teleport_format_bytes(total_size, total_str, sizeof(total_str));
    std::cout << std::endl;
    std::cout << "Total: " << file_count << " file(s), " << total_str << std::endl;
    std::cout << std::endl;
    
    if (g_auto_accept) {
        print_info("Auto-accepting transfer");
        return 1;
    }
    
    return prompt_yes_no("Accept transfer?", true) ? 1 : 0;
}

void on_recv_progress(const TeleportProgress* progress, void*) {
    g_recv_progress.update(progress);
}

void on_recv_complete(TeleportError error, void*) {
    g_transfer_error.store(error);
    g_transfer_done.store(true);
}

int receive_command(const std::vector<std::string>& args) {
    enable_colors();
    
    // Parse arguments
    std::string output_dir = ".";
    g_auto_accept = false;
    
    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--output" || args[i] == "-o") && i + 1 < args.size()) {
            output_dir = args[++i];
        } else if (args[i] == "--auto-accept" || args[i] == "-y") {
            g_auto_accept = true;
        }
    }
    
    // Set up signal handler
    std::signal(SIGINT, recv_signal_handler);
    g_running.store(true);
    
    // Create engine
    TeleportEngine* engine = nullptr;
    TeleportConfig config = {};
    
    auto err = teleport_create(&config, &engine);
    if (err != TELEPORT_OK) {
        print_error("Failed to create engine: " + std::string(teleport_error_string(err)));
        return 1;
    }
    
    // Show local IP
    char local_ip[64];
    teleport_get_local_ip(local_ip, sizeof(local_ip));
    
    print_header("Teleport Receiver");
    std::cout << std::endl;
    print_info("Local IP: " + std::string(local_ip));
    print_info("Output directory: " + output_dir);
    print_info("Press Ctrl+C to stop");
    std::cout << std::endl;
    
    // Start discovery (so others can find us)
    err = teleport_start_discovery(engine, nullptr, nullptr, nullptr);
    if (err != TELEPORT_OK) {
        print_warning("Could not start discovery broadcasting");
    }
    
    // Start receiving
    err = teleport_start_receiving(
        engine,
        output_dir.c_str(),
        on_incoming_transfer,
        on_recv_progress,
        on_recv_complete,
        nullptr
    );
    
    if (err != TELEPORT_OK) {
        print_error("Failed to start receiver: " + std::string(teleport_error_string(err)));
        teleport_destroy(engine);
        return 1;
    }
    
    print_success("Listening for incoming transfers...");
    std::cout << std::endl;
    
    // Wait for transfers or Ctrl+C
    while (g_running.load()) {
        if (g_transfer_done.load()) {
            std::cout << std::endl;
            
            TeleportError final_error = g_transfer_error.load();
            if (final_error == TELEPORT_OK) {
                g_recv_progress.complete();
            } else {
                g_recv_progress.failed(teleport_error_string(final_error));
            }
            
            // Reset for next transfer
            g_transfer_done.store(false);
            g_transfer_error.store(TELEPORT_OK);
            
            std::cout << std::endl;
            print_success("Listening for more transfers...");
            std::cout << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << std::endl;
    print_info("Stopping receiver...");
    
    teleport_stop_receiving(engine);
    teleport_stop_discovery(engine);
    teleport_destroy(engine);
    
    return 0;
}

} // namespace cli
} // namespace teleport
