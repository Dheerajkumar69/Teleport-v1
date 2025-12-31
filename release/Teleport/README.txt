# Teleport - Cross-Platform File Transfer

A fast, secure, local network file transfer tool.

## Quick Start

### Desktop UI (Recommended)
Double-click **`Run_Teleport.bat`** to launch the beautiful desktop interface.

### CLI Mode
- **Discover devices:** Double-click `Discover_Devices.bat`
- **Receive files:** Double-click `Receive_Files.bat`
- **Send files:** Open Command Prompt and run:
  ```
  teleport_cli.exe send <file> <device-ip>
  ```

## Requirements
- Windows 10 or later
- Both devices must be on the same local network
- Windows Firewall may need to allow Teleport (port 42424)

## Firewall Setup
If devices don't discover each other:
1. Open Windows Defender Firewall
2. Click "Allow an app through firewall"
3. Add `Teleport.exe` and `teleport_cli.exe`
4. Enable for Private networks

## Files Included
- `Teleport.exe` - Desktop UI application
- `teleport_cli.exe` - Command-line interface
- `Run_Teleport.bat` - One-click launcher
- `Discover_Devices.bat` - Discover nearby devices
- `Receive_Files.bat` - Wait for incoming files
- `*.dll` - Required runtime libraries

## Troubleshooting
- **No devices found:** Check both devices are on same network
- **Transfer fails:** Disable firewall temporarily to test
- **App won't start:** Ensure all DLL files are present

Enjoy fast local file transfers! 🚀
