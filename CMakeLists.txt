cmake_minimum_required(VERSION 3.20)
project(Teleport VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Platform detection
if(WIN32)
    set(TELEPORT_PLATFORM "windows")
    add_definitions(-DTELEPORT_WINDOWS)
    add_definitions(-D_WIN32_WINNT=0x0601)  # Windows 7+
    add_definitions(-DNOMINMAX)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
elseif(APPLE)
    set(TELEPORT_PLATFORM "darwin")
    add_definitions(-DTELEPORT_DARWIN)
elseif(UNIX)
    set(TELEPORT_PLATFORM "linux")
    add_definitions(-DTELEPORT_LINUX)
endif()

message(STATUS "Building Teleport for platform: ${TELEPORT_PLATFORM}")

# Fetch nlohmann/json if not found
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Alias for compatibility
if(NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()

# Find GTest (optional)
find_package(GTest CONFIG QUIET)

# Enable testing
enable_testing()

# Subdirectories
add_subdirectory(core)
add_subdirectory(cli)

# Install targets
install(TARGETS teleport_core teleport_cli
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
