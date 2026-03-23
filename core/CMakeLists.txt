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
    )
elseif(UNIX AND NOT APPLE)
    target_sources(teleport_core PRIVATE
        src/platform/pal_linux.cpp
        src/platform/hotspot_linux.cpp
        src/platform/wifi_direct_linux.cpp
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
