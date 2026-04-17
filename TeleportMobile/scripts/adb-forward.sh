#!/usr/bin/env bash
# ============================================================
# adb-forward.sh — USB debug setup for Teleport WebRTC dev
#
# Run this ONCE before starting Metro and the Android app.
# It lets the Android device reach your laptop's local services
# through the USB cable (no WiFi needed).
# ============================================================

set -euo pipefail

echo "🔌 Setting up ADB port forwarding for Teleport WebRTC dev..."

# Check adb is available
if ! command -v adb &>/dev/null; then
  echo "❌  adb not found. Install Android SDK platform-tools first."
  exit 1
fi

# Check a device is connected
DEVICES=$(adb devices | grep -v "List of" | grep "device$" | wc -l)
if [ "$DEVICES" -eq 0 ]; then
  echo "❌  No Android device connected. Connect your phone via USB and enable USB Debugging."
  exit 1
fi

echo "✅  Found $DEVICES device(s)"

# adb reverse: phone → laptop  (so phone's localhost:PORT → laptop's localhost:PORT)
# This is the correct direction for our use case.

adb reverse tcp:3000 tcp:3000   # Teleport signaling server
adb reverse tcp:8081 tcp:8081   # Metro bundler

echo ""
echo "✅  Port forwarding active:"
echo "     Phone localhost:3000  →  Laptop localhost:3000  (Signaling Server)"
echo "     Phone localhost:8081  →  Laptop localhost:8081  (Metro Bundler)"
echo ""
echo "📋  Next steps:"
echo "  1. Start signaling: cd ../webversion/server && node signaling.js"
echo "  2. Start Metro:     cd .. && npx react-native start"
echo "  3. Run app:         npx react-native run-android"
echo "  4. Open web app:    http://localhost:3000 (or your web dev server)"
echo ""
echo "💡  In the Android app, toggle 'USB Dev Mode' in settings to use local signaling."
echo "    (This sets USE_USB_FORWARD=true in SignalingClient.ts)"
