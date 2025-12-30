# Teleport - Enterprise Cross-Platform File Transfer System

A production-grade, local-network, peer-to-peer file transfer system with a single native C++ core and thin platform-specific UI shells.

## Features

- **Zero Cloud Dependency**: Works completely offline on local network
- **High Performance**: Parallel TCP streams, chunked transfer, resume support
- **Cross-Platform Core**: Single C++ engine with platform-specific bindings
- **Automatic Discovery**: UDP broadcast device discovery

## Phase 1 Status: Windows ↔ Windows (CLI)

Currently implementing Phase 1 with Windows CLI interface.

## Requirements

### Windows Build

- Visual Studio 2022 with C++ Desktop Development workload
- CMake 3.20+
- vcpkg (for nlohmann-json)

## Build Instructions

```powershell
# Install vcpkg if you haven't
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install

# Install dependencies
.\vcpkg install nlohmann-json:x64-windows gtest:x64-windows

# Clone and build Teleport
cd path\to\Teleport

# Configure (adjust vcpkg path)
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release

# Run tests
cd build && ctest -C Release --output-on-failure
```

## Usage

```powershell
# Discover devices on the network
.\teleport.exe discover

# Send a file
.\teleport.exe send ./movie.mp4 --to <device-ip>

# Start receiver (listen mode)
.\teleport.exe receive --output ./downloads
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Platform Shells                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐      │
│  │ Windows CLI │  │ Android UI  │  │  macOS UI   │      │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘      │
└─────────┼────────────────┼────────────────┼─────────────┘
          │                │                │
          ▼                ▼                ▼
┌─────────────────────────────────────────────────────────┐
│              Core Engine (C++17)                         │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │ Discovery│ │ Control  │ │ Transfer │ │ Security │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘   │
└─────────────────────────────────────────────────────────┘
          │
          ▼
┌─────────────────────────────────────────────────────────┐
│        Platform Abstraction Layer (PAL)                  │
│  ┌──────────────────┐  ┌──────────────────┐             │
│  │  Socket Wrapper  │  │  File I/O Wrapper│             │
│  └──────────────────┘  └──────────────────┘             │
└─────────────────────────────────────────────────────────┘
```

## Protocol Overview

### Discovery (UDP Port 45454)
Devices broadcast JSON packets every 1 second:
```json
{
  "v": 1,
  "id": "uuid",
  "name": "Device Name",
  "os": "Windows",
  "ip": "192.168.1.100",
  "port": 45455,
  "caps": ["parallel", "resume"]
}
```

### Control Channel (TCP)
Length-prefixed JSON messages for handshake, file list, accept/reject, pause/resume.

### Data Channel (TCP)
Binary chunked transfer with 16-byte header + data payload. Supports parallel streams.

## Performance Targets

| Metric | Target |
|--------|--------|
| Localhost throughput | ≥800 MB/s |
| LAN throughput (1 Gbps) | ≥100 MB/s |
| Memory usage (2GB file) | <50 MB |
| CPU usage (transfer) | <20% |

## License

MIT License - See LICENSE file

## Security Notice

⚠️ **Phase 1 transfers are unencrypted.** Only use on trusted local networks. Encryption will be added in Phase 4.
