# Teleport Android Native Build
cmake_minimum_required(VERSION 3.22.1)
project("teleport_android")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to core library (passed from Gradle)
set(TELEPORT_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core")

# Add nlohmann/json as header-only (bundled for Android)
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Core library sources
file(GLOB_RECURSE CORE_SOURCES
    "${TELEPORT_CORE_DIR}/src/*.cpp"
)

# JNI wrapper
add_library(teleport_jni SHARED
    TeleportJNI.cpp
    ${CORE_SOURCES}
)

target_include_directories(teleport_jni PRIVATE
    ${TELEPORT_CORE_DIR}/include
    ${TELEPORT_CORE_DIR}/src
)

target_link_libraries(teleport_jni
    android
    log
    nlohmann_json::nlohmann_json
)

# Compiler warnings
target_compile_options(teleport_jni PRIVATE
    -Wall -Wextra -Wpedantic
)
