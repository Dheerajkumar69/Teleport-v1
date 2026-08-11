# Teleport Desktop UI - Cross-Platform GUI using Dear ImGui
cmake_minimum_required(VERSION 3.20)
project(TeleportDesktop VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use local ImGui copy
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

# ImGui core source files (always needed)
set(IMGUI_CORE_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

# Common desktop application sources (shared between Windows and Linux)
set(DESKTOP_COMMON_SOURCES
    src/TeleportBridge.cpp
    src/Theme.cpp
    src/Config.cpp
    src/views/DiscoverView.cpp
    src/views/SendView.cpp
    src/views/ReceiveView.cpp
    src/views/TransfersView.cpp
    src/views/SettingsView.cpp
    src/components/DeviceCard.cpp
    src/components/TransferProgress.cpp
    src/components/FileDropZone.cpp
    src/components/Animations.cpp
)

set(DESKTOP_COMMON_HEADERS
    src/TeleportBridge.h
    src/Theme.h
    src/Config.h
    src/Icons.h
    src/platform/Sound.h
    src/views/DiscoverView.h
    src/views/SendView.h
    src/views/ReceiveView.h
    src/views/TransfersView.h
    src/views/SettingsView.h
    src/components/DeviceCard.h
    src/components/TransferProgress.h
    src/components/FileDropZone.h
    src/components/Animations.h
)

# ============================================================================
# WINDOWS BUILD (DirectX 11 + Win32)
# ============================================================================
if(WIN32)
    # OpenSSL is REQUIRED on Windows — same as Linux.
    # Without it, wss:// signaling is impossible and all relay traffic
    # is sent in plaintext. Fail the build loudly rather than silently.
    find_package(OpenSSL REQUIRED)

    set(IMGUI_WIN32_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )

    set(DESKTOP_WIN_SOURCES
        src/main.cpp
        src/Application.cpp
        src/platform/Sound_win32.cpp
        src/WebSignalingClient.cpp
        src/NativeWebRTCClient.cpp
        src/RelayResumeManager.cpp
    )

    set(DESKTOP_WIN_HEADERS
        src/Application.h
    )

    add_executable(TeleportUI WIN32
        ${DESKTOP_WIN_SOURCES}
        ${DESKTOP_WIN_HEADERS}
        ${DESKTOP_COMMON_SOURCES}
        ${DESKTOP_COMMON_HEADERS}
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_WIN32_SOURCES}
        resources/app.rc
    )

    target_include_directories(TeleportUI PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/core/include
    )

    target_link_libraries(TeleportUI PRIVATE
        teleport_core
        d3d11
        dxgi
        d3dcompiler
        dwmapi
        uxtheme
        Comctl32
        Shlwapi
        datachannel
    )

    if(OpenSSL_FOUND)
        target_link_libraries(TeleportUI PRIVATE
            OpenSSL::SSL
            OpenSSL::Crypto
        )
        target_compile_definitions(TeleportUI PRIVATE USE_OPENSSL)
    else()
        # This branch should be unreachable now that REQUIRED is set above.
        # Kept for belt-and-suspenders in case someone overrides the find.
        message(FATAL_ERROR
            "OpenSSL is required for wss:// signaling support. "
            "Install OpenSSL dev libraries and re-run cmake.")
    endif()

    target_compile_definitions(TeleportUI PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )


    set_target_properties(TeleportUI PROPERTIES
        VS_DPI_AWARE "PerMonitor"
        OUTPUT_NAME "Teleport"
    )

    # Copy fonts to build output
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts)
        add_custom_command(TARGET TeleportUI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts
            $<TARGET_FILE_DIR:TeleportUI>/fonts
        )
    endif()

# ============================================================================
# LINUX BUILD (SDL2 + OpenGL3)
# ============================================================================
elseif(UNIX AND NOT APPLE)
    # Find required packages
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    find_package(OpenGL REQUIRED)
    find_package(OpenSSL REQUIRED)

    set(IMGUI_SDL_GL_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )

    set(DESKTOP_LINUX_SOURCES
        src/main_linux.cpp
        src/Application_sdl.cpp
        src/platform/FileDialog_linux.cpp
        src/platform/Sound_linux.cpp
        src/WebSignalingClient.cpp
        src/NativeWebRTCClient.cpp
        src/RelayResumeManager.cpp
    )

    set(DESKTOP_LINUX_HEADERS
        src/Application_sdl.h
        src/platform/FileDialog_linux.h
        src/WebSignalingClient.h
    )

    add_executable(TeleportUI
        ${DESKTOP_LINUX_SOURCES}
        ${DESKTOP_LINUX_HEADERS}
        ${DESKTOP_COMMON_SOURCES}
        ${DESKTOP_COMMON_HEADERS}
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_SDL_GL_SOURCES}
    )

    target_include_directories(TeleportUI PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/core/include
        ${SDL2_INCLUDE_DIRS}
    )

    target_link_libraries(TeleportUI PRIVATE
        teleport_core
        ${SDL2_LIBRARIES}
        OpenGL::GL
        OpenSSL::SSL
        OpenSSL::Crypto
        ${CMAKE_DL_LIBS}
        datachannel
    )

    target_compile_definitions(TeleportUI PRIVATE
        TELEPORT_LINUX
        USE_OPENSSL
    )

    set_target_properties(TeleportUI PROPERTIES
        OUTPUT_NAME "Teleport"
    )

    # Copy fonts to build output
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts)
        add_custom_command(TARGET TeleportUI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts
            $<TARGET_FILE_DIR:TeleportUI>/fonts
        )
    endif()

# ============================================================================
# macOS BUILD (SDL2 + OpenGL3 - same as Linux)
# ============================================================================
elseif(APPLE)
    # Find required packages
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    find_package(OpenGL REQUIRED)
    find_package(OpenSSL REQUIRED)

    set(IMGUI_SDL_GL_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )

    set(DESKTOP_MACOS_SOURCES
        src/main_macos.cpp
        src/Application_sdl.cpp
        src/platform/FileDialog_macos.cpp
        src/platform/Sound_macos.cpp
        src/WebSignalingClient.cpp
        src/NativeWebRTCClient.cpp
    )

    set(DESKTOP_MACOS_HEADERS
        src/Application_sdl.h
        src/platform/FileDialog_macos.h
        src/WebSignalingClient.h
    )

    add_executable(TeleportUI MACOSX_BUNDLE
        ${DESKTOP_MACOS_SOURCES}
        ${DESKTOP_MACOS_HEADERS}
        ${DESKTOP_COMMON_SOURCES}
        ${DESKTOP_COMMON_HEADERS}
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_SDL_GL_SOURCES}
    )

    target_include_directories(TeleportUI PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/core/include
        ${SDL2_INCLUDE_DIRS}
    )

    target_link_libraries(TeleportUI PRIVATE
        teleport_core
        ${SDL2_LIBRARIES}
        OpenGL::GL
        OpenSSL::SSL
        OpenSSL::Crypto
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        ${CMAKE_DL_LIBS}
        datachannel
    )

    target_compile_definitions(TeleportUI PRIVATE
        TELEPORT_MACOS
        USE_OPENSSL
    )

    set_target_properties(TeleportUI PROPERTIES
        OUTPUT_NAME "Teleport"
        MACOSX_BUNDLE_BUNDLE_NAME "Teleport"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.teleport.desktop"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    )

    # Copy fonts to app bundle
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts)
        add_custom_command(TARGET TeleportUI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts
            $<TARGET_FILE_DIR:TeleportUI>/../Resources/fonts
        )
    endif()
endif()
