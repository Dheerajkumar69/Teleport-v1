# CLI executable
add_executable(teleport_cli
    main.cpp
    commands/discover.cpp
    commands/send.cpp
    commands/receive.cpp
    ui/progress.cpp
    ui/console.cpp
)

# Rename output to just "teleport"
set_target_properties(teleport_cli PROPERTIES
    OUTPUT_NAME teleport
)

target_include_directories(teleport_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(teleport_cli PRIVATE
    teleport_core
)
