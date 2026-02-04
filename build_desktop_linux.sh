#!/bin/bash
# Build Teleport Desktop UI for Linux
# Requires: cmake, ninja, libsdl2-dev, libgl1-mesa-dev

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Check dependencies
check_dependency() {
    if ! command -v "$1" &> /dev/null; then
        echo "Error: $1 is not installed"
        echo "Install with: sudo apt install $2"
        exit 1
    fi
}

check_dependency cmake cmake
check_dependency ninja ninja-build

# Check for SDL2
if ! pkg-config --exists sdl2; then
    echo "Error: SDL2 development libraries not found"
    echo "Install with: sudo apt install libsdl2-dev"
    exit 1
fi

# Check for OpenGL
if [ ! -f /usr/include/GL/gl.h ]; then
    echo "Error: OpenGL development headers not found"
    echo "Install with: sudo apt install libgl1-mesa-dev"
    exit 1
fi

echo "=== Building Teleport Desktop for Linux ==="

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring..."
cmake .. -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DTELEPORT_BUILD_DESKTOP=ON

# Build
echo "Building..."
ninja TeleportUI

echo ""
echo "=== Build Complete ==="
echo "Executable: $(pwd)/desktop/Teleport"
echo ""
echo "Run with: ./build/desktop/Teleport"
