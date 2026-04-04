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

# Build options
option(TELEPORT_BUILD_DESKTOP "Build desktop UI application" ON)

include(FetchContent)

# Fetch libdatachannel for Desktop (provides nlohmann_json internally)
if(TELEPORT_BUILD_DESKTOP)
    set(NO_WEBSOCKET ON CACHE INTERNAL "Disable websocket in libdatachannel")
    set(NO_MEDIA ON CACHE INTERNAL "Disable media in libdatachannel")
    set(USE_GNUTLS OFF CACHE INTERNAL "Disable GNUTLS")
    set(USE_NICE OFF CACHE INTERNAL "Disable libnice")
    set(SCTP_DEBUG OFF CACHE INTERNAL "Disable SCTP debug")
    set(USE_MBEDTLS OFF CACHE INTERNAL "Use OpenSSL instead")

    FetchContent_Declare(
        libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG        v0.21.2
    )
    FetchContent_MakeAvailable(libdatachannel)
endif()

# Fetch nlohmann/json if libdatachannel hasn't provided it
if(NOT TARGET nlohmann_json)
    FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(json)
endif()

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

# Desktop UI (Windows and Linux)
if(TELEPORT_BUILD_DESKTOP)
    if(WIN32 OR UNIX)
        add_subdirectory(desktop)
    endif()
endif()

# Install targets
install(TARGETS teleport_core teleport_cli
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
