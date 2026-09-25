cmake_minimum_required(VERSION 3.20)
project(FarCry6HeadTracking VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "FarCry6HeadTracking must be built as x64. Configure with -A x64.")
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# The camera adapter and co-op gate use core's MinHook wrapper.
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# The game loads its head tracking support by this exact file name, so that is what
# the mod has to be called. install.cmd keeps the shipped one alongside as
# tobii_gameintegration_x64.dll.backup and puts it back on uninstall.
add_library(FarCry6HeadTracking SHARED
    src/camera_adapter.cpp
    src/headlight.cpp
    src/reticle.cpp
    src/build_profile.cpp
    src/ubisoft_offsets.cpp
    src/steam_offsets.cpp
    src/dllmain.cpp
    src/config.cpp
    src/legacy_config/legacy_config.cpp
    src/mod.cpp
    src/hotkeys.cpp
    src/tracking_runtime.cpp
    src/frame_pump.cpp
    src/tobii_shim.cpp
    src/tracker_info.cpp
    src/coop_gate.cpp
    src/window_centering.cpp
)

set_target_properties(FarCry6HeadTracking PROPERTIES
    OUTPUT_NAME "tobii_gameintegration_x64"
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_include_directories(FarCry6HeadTracking PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(FarCry6HeadTracking PRIVATE cameraunlock cameraunlock_hooks minhook)

if(MSVC)
    target_compile_options(FarCry6HeadTracking PRIVATE /W4 /WX /permissive- /wd4324)
endif()

if(WIN32)
    target_link_libraries(FarCry6HeadTracking PRIVATE ws2_32)
endif()

# Native unit tests for the pure logic between the pipeline and the game: the INI
# sanitizers, the sign/unit conversion in pose_bridge.h and the tracker description
# the game is answered with, plus the runtime reclaiming a tracker port another
# program was holding. Off by default so a normal build never puts a test binary
# in bin/.
option(FARCRY6_BUILD_TESTS "Build FarCry6HeadTracking unit tests" OFF)
if(FARCRY6_BUILD_TESTS)
    enable_testing()
    add_executable(farcry6_tests
        tests/farcry6_tests.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
        src/tracking_runtime.cpp
        src/tracker_info.cpp
        src/window_centering.cpp
    )
    target_include_directories(farcry6_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp/include
    )
    target_compile_definitions(farcry6_tests PRIVATE
        FARCRY6_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/FarCry6HeadTracking.ini")
    target_link_libraries(farcry6_tests PRIVATE cameraunlock cameraunlock_hooks minhook)
    if(WIN32)
        # user32/gdi32 are what the tracker description reads the monitor through.
        target_link_libraries(farcry6_tests PRIVATE ws2_32 user32 gdi32)
    endif()
    set_target_properties(farcry6_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(MSVC)
        target_compile_options(farcry6_tests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME farcry6_tests COMMAND farcry6_tests)

    # The config differential test: v0.1.0's reader (the oracle), the frozen reader in
    # src/legacy_config and the migration to the canonical file, over every published
    # build's first-run output and core's mutation corpus.
    add_executable(farcry6_config_differential
        tests/config_differential/differential_tests.cpp
        tests/config_differential/oracle/oracle_config.cpp
        src/legacy_config/legacy_config.cpp
        src/config.cpp
    )
    target_include_directories(farcry6_config_differential PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential
        ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp/include
    )
    target_compile_definitions(farcry6_config_differential PRIVATE
        FARCRY6_DIFFERENTIAL_INPUTS="${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/inputs"
        FARCRY6_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/FarCry6HeadTracking.ini")
    target_link_libraries(farcry6_config_differential PRIVATE cameraunlock)
    set_target_properties(farcry6_config_differential PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(MSVC)
        target_compile_options(farcry6_config_differential PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME farcry6_config_differential COMMAND farcry6_config_differential)
endif()
