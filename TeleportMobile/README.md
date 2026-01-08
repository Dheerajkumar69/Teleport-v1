# TeleportMobile

React Native mobile app for high-speed P2P file transfer.

## Prerequisites

- Node.js 20+
- JDK 21 (from Android Studio: `C:\Program Files\Android\Android Studio\jbr`)
- Android SDK (via Android Studio SDK Manager)
- NDK 27.0.12077973

## Building from Command Line

**No Android Studio IDE required!** Just need the SDK/NDK tools installed.

### 1. Set environment variables (PowerShell)

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
```

### 2. Install dependencies

```powershell
npm install
```

### 3. Build debug APK

```powershell
cd android
.\gradlew assembleDebug --no-daemon
```

APK will be at: `android/app/build/outputs/apk/debug/app-debug.apk`

### 4. Install on connected device

```powershell
adb install android\app\build\outputs\apk\debug\app-debug.apk
```

## Running the App (Development)

### Start Metro bundler

```powershell
npm start
```

### Run on Android device/emulator (in another terminal)

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
npm run android
```

## Testing with a Physical Device

1. Enable **Developer Options** (Settings > About > tap Build Number 7x)
2. Enable **USB Debugging** in Developer Options
3. Connect phone via USB
4. Authorize computer when prompted on phone
5. Run `npm run android`

## Architecture

```
TeleportMobile/
├── App.tsx                    # Main UI (Discover/Send/Receive tabs)
├── src/
│   └── TeleportService.ts     # TypeScript API for native module
├── android/
│   └── app/src/main/
│       ├── java/.../          # Kotlin native module
│       │   ├── TeleportModule.kt
│       │   └── TeleportPackage.kt
│       └── cpp/               # C++ JNI bridge
│           ├── CMakeLists.txt
│           └── teleport_rn.cpp
└── core/ (parent dir)         # Shared C++ Teleport engine
```

## Features

- 🔍 **Discover**: Find other Teleport devices on the network
- 📁 **Send**: Select and send files to discovered devices  
- 📥 **Receive**: Accept incoming file transfers

## License

MIT
