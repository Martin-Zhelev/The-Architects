cmake_minimum_required(VERSION 3.16)
project(Labyrinth VERSION 2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(LABYRINTH_BUILD_APP "Build the 3D app (downloads raylib 5.5 if it is not installed)" ON)

if(MINGW)
    # Windows builds made with MinGW need no extra DLLs.
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

set(LABYRINTH_WARNINGS
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Wshadow>)

# ---------------------------------------------------------------------------
# Game rules: pure logic, no cin/cout, no graphics. Shared by everything.
# ---------------------------------------------------------------------------
add_library(labyrinth_logic STATIC
    src/maze.cpp
    src/game.cpp
    src/number_trail.cpp
    src/validation.cpp
    src/campaign.cpp
    src/profile.cpp
    src/savegame.cpp
    src/custom_map.cpp
)
target_include_directories(labyrinth_logic PUBLIC src)
target_compile_options(labyrinth_logic PRIVATE ${LABYRINTH_WARNINGS})

# Automated tests
add_executable(labyrinth_tests tests/test_main.cpp)
target_link_libraries(labyrinth_tests PRIVATE labyrinth_logic)
target_compile_options(labyrinth_tests PRIVATE ${LABYRINTH_WARNINGS})
enable_testing()
add_test(NAME logic_tests COMMAND labyrinth_tests)

# The classic terminal edition (version 1)
add_executable(labyrinth_console src/input.cpp src/ui.cpp src/main.cpp)
target_link_libraries(labyrinth_console PRIVATE labyrinth_logic)
target_compile_options(labyrinth_console PRIVATE ${LABYRINTH_WARNINGS})

# ---------------------------------------------------------------------------
# The 3D app
# ---------------------------------------------------------------------------
if(LABYRINTH_BUILD_APP)
    find_package(raylib 5.5 QUIET)
    if(NOT raylib_FOUND)
        include(FetchContent)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
        set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE)
        FetchContent_MakeAvailable(raylib)
    endif()

    add_executable(Labyrinth
        src/app/main.cpp
        src/app/theme.cpp
        src/app/render3d.cpp
        src/app/postfx.cpp
        src/app/particles.cpp
        src/app/audio.cpp
        src/app/widgets.cpp
        src/app/screen_menus.cpp
        src/app/screen_modes.cpp
        src/app/screen_game.cpp
        src/app/screen_trail.cpp
        src/app/screen_maps.cpp
    )
    target_link_libraries(Labyrinth PRIVATE labyrinth_logic raylib)
    target_compile_options(Labyrinth PRIVATE ${LABYRINTH_WARNINGS})

    if(WIN32)
        # The .exe icon (same art as the window icon, see tools/make_icon.py).
        enable_language(RC)
        target_sources(Labyrinth PRIVATE src/app/labyrinth.rc)
        # A real Windows app: no console window behind the game.
        if(MSVC)
            target_link_options(Labyrinth PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
        else()
            target_link_options(Labyrinth PRIVATE -mwindows)
        endif()
    endif()
    if(APPLE)
        target_link_libraries(Labyrinth PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
    endif()
    set_target_properties(Labyrinth PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Labyrinth)
endif()
