---
description: How to build and run TeleportMobile React Native app
---

# Running TeleportMobile React Native App

## Prerequisites
- Node.js 20+ (check: `node -v`)
- Java/JDK 17+ (check: `java -version`)
- Android Studio with SDK and NDK installed
- Android Emulator running OR physical device connected via USB with debugging enabled

## Step 1: Set Environment Variables (if not already set)
```powershell
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
```

## Step 2: Start Metro Bundler (Terminal 1)
// turbo
```powershell
cd "d:\CODES\actual projects\Teleport\TeleportMobile"
npm start
```
Keep this terminal running.

## Step 3: Build and Run (Terminal 2)
Open a NEW terminal and run:
```powershell
cd "d:\CODES\actual projects\Teleport\TeleportMobile"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
npm run android
```

## Troubleshooting

### "SDK location not found"
Set ANDROID_HOME environment variable:
```powershell
[Environment]::SetEnvironmentVariable("ANDROID_HOME", "$env:LOCALAPPDATA\Android\Sdk", "User")
```
Then restart terminal.

### "No emulator running"
1. Open Android Studio
2. Go to Device Manager (or Tools → Device Manager)
3. Create a new virtual device if needed
4. Click Play ▶️ to start the emulator
5. Wait for it to fully boot before running `npm run android`

### Build fails with CMake/NDK errors
Ensure NDK is installed via Android Studio SDK Manager:
1. Open Android Studio → Settings → SDK Manager
2. Go to SDK Tools tab
3. Check "NDK (Side by side)" and install

### Gradle errors
Try cleaning and rebuilding:
```powershell
cd "d:\CODES\actual projects\Teleport\TeleportMobile\android"
.\gradlew clean
cd ..
npm run android
```
