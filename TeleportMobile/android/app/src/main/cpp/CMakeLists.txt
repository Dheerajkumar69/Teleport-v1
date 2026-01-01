# React Native Teleport Native Module Build
cmake_minimum_required(VERSION 3.22.1)
project("teleport_rn")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to Teleport core
set(TELEPORT_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../core")

# Add nlohmann/json
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Core library sources (cross-platform only)
set(CORE_SOURCES
    ${TELEPORT_CORE_DIR}/src/control/protocol.cpp
    ${TELEPORT_CORE_DIR}/src/control/control_server.cpp
    ${TELEPORT_CORE_DIR}/src/control/control_client.cpp
    ${TELEPORT_CORE_DIR}/src/discovery/discovery.cpp
    ${TELEPORT_CORE_DIR}/src/discovery/device_list.cpp
    ${TELEPORT_CORE_DIR}/src/discovery/udp_broadcaster.cpp
    ${TELEPORT_CORE_DIR}/src/transfer/chunk_reader.cpp
    ${TELEPORT_CORE_DIR}/src/transfer/chunk_writer.cpp
    ${TELEPORT_CORE_DIR}/src/transfer/resume_manager.cpp
    ${TELEPORT_CORE_DIR}/src/transfer/stream_pool.cpp
    ${TELEPORT_CORE_DIR}/src/transfer/transfer_manager.cpp
    ${TELEPORT_CORE_DIR}/src/platform/pal.cpp
    ${TELEPORT_CORE_DIR}/src/utils/logger.cpp
    ${TELEPORT_CORE_DIR}/src/utils/uuid.cpp
    ${TELEPORT_CORE_DIR}/src/utils/sanitize.cpp
    ${TELEPORT_CORE_DIR}/src/teleport.cpp
)

# JNI library
add_library(teleport_rn SHARED
    teleport_rn.cpp
    ${CORE_SOURCES}
)

target_include_directories(teleport_rn PRIVATE
    ${TELEPORT_CORE_DIR}/include
    ${TELEPORT_CORE_DIR}/src
)

target_compile_definitions(teleport_rn PRIVATE
    TELEPORT_PLATFORM_ANDROID=1
)

target_link_libraries(teleport_rn
    android
    log
    nlohmann_json::nlohmann_json
)

target_compile_options(teleport_rn PRIVATE
    -Wall -Wextra
)
