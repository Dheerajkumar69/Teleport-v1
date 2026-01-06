# Teleport - Cross-Platform File Transfer System

A production-grade, local-network, peer-to-peer file transfer system with a single native C++ core and thin platform-specific UI shells.

## Features

- **Zero Cloud Dependency**: Works completely offline on local network
- **High Performance**: Parallel TCP streams, chunked transfer, resume support
- **Cross-Platform Core**: Single C++ engine with platform-specific bindings
- **Automatic Discovery**: UDP broadcast device discovery

---

## Build Requirements

### ⚠️ IMPORTANT: Recommended Tool Versions

> **Keep only what you need!** Each NDK version is ~2GB. JDKs are ~400MB each.

| Platform | Tool | Required Version | Notes |
|----------|------|-----------------|-------|
| **Windows Desktop** | MSYS2 UCRT64 | Latest | At `C:\msys64` |
| **Windows Desktop** | GCC | 15.2.0+ | Via MSYS2 |
| **Windows Desktop** | CMake | 3.20+ | Via MSYS2 |
| **Windows Desktop** | Ninja | Latest | Via MSYS2 |
| **Android Mobile** | Android Studio | 2024.2+ | Bundled JDK 21 |
| **Android Mobile** | Android SDK | API 34+ | Via SDK Manager |
| **Android Mobile** | NDK | **27.0.12077973** | Via SDK Manager |
| **Android Mobile** | Node.js | 20+ | For React Native |

---

## 🖥️ Windows Desktop Build (CLI)

### ✅ Successful Build Method: MSYS2 UCRT64 + Ninja

**Prerequisites:**
- MSYS2 installed at `C:\msys64`
- UCRT64 environment with: `gcc 15.2.0`, `cmake`, `ninja`

**Install MSYS2 packages (run in MSYS2 UCRT64 terminal):**
```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-nlohmann-json
```

**Build Commands:**
```powershell
# Configure
C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c "cd '/d/CODES/actual projects/Teleport' && rm -rf build && mkdir -p build && cd build && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release"

# Build
C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c "cd '/d/CODES/actual projects/Teleport/build' && ninja -j4"
```

**Run:**
```powershell
# Discover devices
.\build\cli\teleport.exe discover

# Send a file
.\build\cli\teleport.exe send ./movie.mp4 --to <device-ip>

# Receive files
.\build\cli\teleport.exe receive --output ./downloads
```

---

## 📱 Android Mobile Build (React Native)

### ✅ Successful Build Method: Android Studio + Bundled JDK

**Prerequisites:**
| Tool | Version | Location |
|------|---------|----------|
| **Android Studio** | 2024.2+ (Ladybug) | Standard install |
| **JDK** | 21 (bundled) | `C:\Program Files\Android\Android Studio\jbr` |
| **Android SDK** | API 34-36 | `%LOCALAPPDATA%\Android\Sdk` |
| **NDK** | **27.0.12077973** | Via SDK Manager |
| **Node.js** | 20+ | [nodejs.org](https://nodejs.org) |

**Install NDK (via Android Studio):**
1. Open Android Studio → Settings → SDK Manager
2. Go to "SDK Tools" tab
3. Check "Show Package Details"
4. Under "NDK (Side by side)", check **27.0.12077973**
5. Click Apply

**Build from Android Studio:**
```powershell
# 1. Install npm dependencies
cd "d:\CODES\actual projects\Teleport\TeleportMobile"
npm install

# 2. Open Android Studio
# Open folder: d:\CODES\actual projects\Teleport\TeleportMobile\android
# Wait for Gradle sync to complete

# 3. In a terminal, start Metro bundler
npm start

# 4. In Android Studio: Run → Run 'app' (or Shift+F10)
```

**Build from Command Line:**
```powershell
# Set environment
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"

# Terminal 1: Start Metro
cd "d:\CODES\actual projects\Teleport\TeleportMobile"
npm start

# Terminal 2: Build and install
cd "d:\CODES\actual projects\Teleport\TeleportMobile"
npm run android
```

---

## 🧹 Cleanup: What You Can Delete

### ❌ Java Versions to Remove

**Keep ONLY:**
- Android Studio's bundled JDK at `C:\Program Files\Android\Android Studio\jbr` (JDK 21)

**Delete these (saves ~400MB each):**
```powershell
# Remove JDK 24 (not compatible with Gradle)
Remove-Item -Recurse -Force "C:\Program Files\Java\jdk-24"

# Remove any other JDKs in C:\Program Files\Java\
```

### ❌ NDK Versions to Remove

**Keep ONLY:**
- `27.0.12077973` (~2.2GB) - Required for this project

**Delete these (saves ~3.7GB):**
```powershell
# Run this to see sizes
Get-ChildItem "$env:LOCALAPPDATA\Android\Sdk\ndk" | Select-Object Name

# Delete old NDK versions
Remove-Item -Recurse -Force "$env:LOCALAPPDATA\Android\Sdk\ndk\25.1.8937393"
Remove-Item -Recurse -Force "$env:LOCALAPPDATA\Android\Sdk\ndk\29.0.14206865"
```

### ❌ Other Cleanup

**Visual Studio / vcpkg** (if not using for other projects):
- The MSYS2 build method doesn't need Visual Studio
- vcpkg is not required with MSYS2

---

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
│  │ Windows (pal.cpp)│  │Android (pal_android.cpp)│      │
│  └──────────────────┘  └──────────────────┘             │
└─────────────────────────────────────────────────────────┘
```

---

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

---

## Performance Targets

| Metric | Target |
|--------|--------|
| Localhost throughput | ≥800 MB/s |
| LAN throughput (1 Gbps) | ≥100 MB/s |
| Memory usage (2GB file) | <50 MB |
| CPU usage (transfer) | <20% |

---

## Troubleshooting

### Windows Build Issues

**"CMake not found"**
```bash
# In MSYS2 UCRT64 terminal
pacman -S mingw-w64-ucrt-x86_64-cmake
```

**"ninja: error: loading 'build.ninja'"**
```bash
# Re-run configure step first
```

### Android Build Issues

**"SDK location not found"**
```powershell
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
```

**"NDK not configured"**
- Install NDK 27.0.12077973 via Android Studio SDK Manager

**"Unsupported class file major version 68"**
- You're using JDK 24, which is too new
- Set `JAVA_HOME` to Android Studio's bundled JDK:
```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
```

---

## License

MIT License - See LICENSE file

## Security Notice

⚠️ **Phase 1 transfers are unencrypted.** Only use on trusted local networks. Encryption will be added in Phase 4.
