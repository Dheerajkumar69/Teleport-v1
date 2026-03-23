#!/bin/bash
# desktop.sh — Build (if needed) and run the Teleport desktop app on Linux
#
# Usage:
#   ./desktop.sh             # rebuild then launch
#   ./desktop.sh --no-build  # skip rebuild, just launch

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BINARY="$SCRIPT_DIR/build/desktop/Teleport"
SKIP_BUILD=0

# Parse script-only flags — do NOT pass these to the binary
for arg in "$@"; do
    case "$arg" in
        --no-build) SKIP_BUILD=1 ;;
    esac
done

# ── Dependency checks ─────────────────────────────────────────────────────────
check_dep() {
    if ! command -v "$1" &>/dev/null; then
        echo "❌  Missing: $1  →  sudo apt install $2"
        exit 1
    fi
}

if [ "$SKIP_BUILD" -eq 0 ]; then
    check_dep cmake cmake
    check_dep ninja ninja-build

    if ! pkg-config --exists sdl2; then
        echo "❌  SDL2 not found  →  sudo apt install libsdl2-dev"
        exit 1
    fi

    if [ ! -f /usr/include/GL/gl.h ]; then
        echo "❌  OpenGL headers not found  →  sudo apt install libgl1-mesa-dev"
        exit 1
    fi

    # ── Build ─────────────────────────────────────────────────────────────────
    echo "🔨  Building Teleport Desktop (Release)…"
    mkdir -p build
    cd build

    # Suppress CMP0135 / FetchContent timestamp warning with -Wno-dev
    cmake .. -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTELEPORT_BUILD_DESKTOP=ON \
        -Wno-dev \
        --log-level=WARNING

    ninja TeleportUI

    cd "$SCRIPT_DIR"
    echo "✅  Build complete → $BINARY"
fi

# ── Pre-launch checks ─────────────────────────────────────────────────────────
if [ ! -x "$BINARY" ]; then
    echo "❌  Binary not found: $BINARY"
    echo "    Run without --no-build to compile first."
    exit 1
fi

# BUG FIX: Check for a display server before launching the GUI app.
# SDL2 will abort immediately if there is no DISPLAY or WAYLAND_DISPLAY.
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "❌  No display server detected (DISPLAY and WAYLAND_DISPLAY are unset)."
    echo "    Run this script from a graphical desktop session."
    exit 1
fi

# ── Launch ────────────────────────────────────────────────────────────────────
echo "🚀  Launching Teleport Desktop…"
echo "    Binary : $BINARY"
echo "    Display: ${DISPLAY:-${WAYLAND_DISPLAY}}"
echo ""

# BUG FIX: Use plain launch instead of exec "$BINARY" "$@".
# exec "$BINARY" "$@" was forwarding script-only flags (--no-build) directly
# to the binary, which does not understand them and may crash or behave
# unexpectedly. Launch without any extra arguments.
"$BINARY"
