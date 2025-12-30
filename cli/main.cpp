/**
 * @file main.cpp
 * @brief Teleport CLI main entry point
 */

#include "teleport/teleport.h"
#include "commands/discover.hpp"
#include "commands/send.hpp"
#include "commands/receive.hpp"
#include "ui/console.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <cstring>

void print_usage(const char* program) {
    std::cout << R"(
╔════════════════════════════════════════════════════════════════╗
║                     TELEPORT v1.0.0                            ║
║          Enterprise File Transfer System (Phase 1)             ║
╚════════════════════════════════════════════════════════════════╝

Usage: )" << program << R"( <command> [options]

Commands:
  discover              Discover devices on the network
  send <files...>       Send files to a device
  receive               Listen for incoming transfers
  version               Show version information

Discovery Options:
  --timeout <seconds>   Discovery timeout (default: 10)

Send Options:
  --to <device>         Target device number or IP address
  --port <port>         Target port (default: auto-discover)

Receive Options:
  --output <dir>        Output directory (default: current)
  --auto-accept         Accept all incoming transfers

Examples:
  )" << program << R"( discover
  )" << program << R"( send movie.mp4 --to 1
  )" << program << R"( send *.zip --to 192.168.1.100
  )" << program << R"( receive --output ./downloads

)" << std::endl;
}

void print_version() {
    std::cout << "Teleport v" << TELEPORT_VERSION_MAJOR << "." 
              << TELEPORT_VERSION_MINOR << "." << TELEPORT_VERSION_PATCH << std::endl;
    std::cout << "Protocol version: " << TELEPORT_PROTOCOL_VERSION << std::endl;
    std::cout << "Platform: Windows" << std::endl;
    
    char ip[64];
    teleport_get_local_ip(ip, sizeof(ip));
    std::cout << "Local IP: " << ip << std::endl;
}

int main(int argc, char* argv[]) {
    // Parse command
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "version" || command == "--version" || command == "-v") {
        print_version();
        return 0;
    }
    
    if (command == "help" || command == "--help" || command == "-h") {
        print_usage(argv[0]);
        return 0;
    }
    
    // Collect arguments
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    
    // Execute command
    int result = 0;
    
    if (command == "discover") {
        result = teleport::cli::discover_command(args);
    } else if (command == "send") {
        result = teleport::cli::send_command(args);
    } else if (command == "receive") {
        result = teleport::cli::receive_command(args);
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        print_usage(argv[0]);
        result = 1;
    }
    
    return result;
}
