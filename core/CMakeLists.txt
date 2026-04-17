# ============================================================================
# Core library CMakeLists.txt
# Can be built standalone (cmake -S core -B build) or as a subdirectory of
# the root project.  When built standalone it fetches its own nlohmann_json.
# ============================================================================

# Only set these if we are the top-level project (not included via add_subdirectory)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(TeleportCore VERSION 1.0.0 LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    # Platform detection (mirrors root CMakeLists.txt)
    if(WIN32)
        add_definitions(-DTELEPORT_WINDOWS)
        add_definitions(-D_WIN32_WINNT=0x0601)
        add_definitions(-DNOMINMAX)
        add_definitions(-DWIN32_LEAN_AND_MEAN)
    elseif(UNIX AND NOT APPLE)
        add_definitions(-DTELEPORT_LINUX)
    elseif(APPLE)
        add_definitions(-DTELEPORT_DARWIN)
    endif()

    enable_testing()
endif()

# ---- Dependency: nlohmann/json ----
# When built from the root project, nlohmann_json::nlohmann_json is already
# defined via FetchContent in the root CMakeLists.  Only fetch it here when
# building core standalone.
if(NOT TARGET nlohmann_json::nlohmann_json)
    include(FetchContent)
    FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(json)
    if(NOT TARGET nlohmann_json::nlohmann_json)
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    endif()
endif()

# Find GTest (only needed when building standalone; root project already finds it)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    find_package(GTest CONFIG QUIET)
endif()

# Core library

add_library(teleport_core STATIC
    # Public API
    src/teleport.cpp
    
    # Discovery
    src/discovery/discovery.cpp
    src/discovery/udp_broadcaster.cpp
    src/discovery/device_list.cpp
    
    # Control
    src/control/control_server.cpp
    src/control/control_client.cpp
    src/control/protocol.cpp
    
    # Transfer
    src/transfer/transfer_manager.cpp
    src/transfer/chunk_reader.cpp
    src/transfer/chunk_writer.cpp
    src/transfer/stream_pool.cpp
    src/transfer/parallel_transfer.cpp  # Cross-platform parallel TCP transfer
    
    # Pairing
    src/pairing/qr_pairing.cpp
    src/pairing/third_party/qrcodegen.cpp
    
    # Security
    src/security/token.cpp
    
    # Utils
    src/utils/uuid.cpp
    src/utils/logger.cpp
    src/utils/sanitize.cpp
)

# Platform-specific source files
if(WIN32)
    target_sources(teleport_core PRIVATE
        src/platform/pal.cpp
        src/platform/hotspot_win.cpp
        src/platform/wifi_direct_win.cpp
        src/transfer/zero_copy_win.cpp
    )
elseif(UNIX AND NOT APPLE)
    target_sources(teleport_core PRIVATE
        src/platform/pal_linux.cpp
        src/platform/hotspot_linux.cpp
        src/platform/wifi_direct_linux.cpp
        src/transfer/zero_copy_linux.cpp
    )
endif()

target_include_directories(teleport_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(teleport_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pairing
)

# Define TELEPORT_STATIC for static library builds (no dllimport/dllexport)
target_compile_definitions(teleport_core PUBLIC TELEPORT_STATIC)

target_link_libraries(teleport_core PUBLIC
    nlohmann_json::nlohmann_json
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(teleport_core PUBLIC
        ws2_32      # Winsock
        iphlpapi    # IP Helper API (for network interfaces)
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(teleport_core PRIVATE /W4 /WX-)
else()
    target_compile_options(teleport_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tests
if(GTest_FOUND)
    add_executable(teleport_tests
        tests/test_discovery.cpp
        tests/test_protocol.cpp
        tests/test_transfer.cpp
        tests/test_uuid.cpp
        tests/test_sanitize.cpp
        tests/test_qr_pairing.cpp
    )
    
    target_link_libraries(teleport_tests PRIVATE
        teleport_core
        GTest::gtest
        GTest::gtest_main
    )
    
    include(GoogleTest)
    gtest_discover_tests(teleport_tests)
endif()

# Always-on transfer safety self-test (runs even when GTest is unavailable)
add_executable(teleport_transfer_selftest
    tests/test_transfer_bulletproof.cpp
)

target_link_libraries(teleport_transfer_selftest PRIVATE
    teleport_core
)

add_test(NAME teleport_transfer_selftest COMMAND teleport_transfer_selftest)
