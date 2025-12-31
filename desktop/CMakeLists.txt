# Teleport Desktop UI - Modern Windows GUI using Dear ImGui
cmake_minimum_required(VERSION 3.20)
project(TeleportDesktop VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use local ImGui copy (faster than FetchContent)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

# ImGui source files
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# Desktop application sources
set(DESKTOP_SOURCES
    src/main.cpp
    src/Application.cpp
    src/TeleportBridge.cpp
    src/Theme.cpp
    src/views/DiscoverView.cpp
    src/views/SendView.cpp
    src/views/ReceiveView.cpp
    src/views/TransfersView.cpp
    src/components/DeviceCard.cpp
    src/components/TransferProgress.cpp
    src/components/FileDropZone.cpp
    src/components/Animations.cpp
)

set(DESKTOP_HEADERS
    src/Application.h
    src/TeleportBridge.h
    src/Theme.h
    src/views/DiscoverView.h
    src/views/SendView.h
    src/views/ReceiveView.h
    src/views/TransfersView.h
    src/components/DeviceCard.h
    src/components/TransferProgress.h
    src/components/FileDropZone.h
    src/components/Animations.h
)

# Create executable
add_executable(TeleportUI WIN32
    ${DESKTOP_SOURCES}
    ${DESKTOP_HEADERS}
    ${IMGUI_SOURCES}
    resources/app.rc
)

target_include_directories(TeleportUI PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/core/include
)

# Link libraries
target_link_libraries(TeleportUI PRIVATE
    teleport_core
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    uxtheme
    Comctl32
    Shlwapi
)

# Windows-specific settings
target_compile_definitions(TeleportUI PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Enable high DPI awareness
set_target_properties(TeleportUI PROPERTIES
    VS_DPI_AWARE "PerMonitor"
    OUTPUT_NAME "Teleport"
)

# Copy fonts to build output (if fonts exist)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts)
    add_custom_command(TARGET TeleportUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts
        $<TARGET_FILE_DIR:TeleportUI>/fonts
    )
endif()
