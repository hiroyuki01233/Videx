cmake_minimum_required(VERSION 3.28)

project(
    Videx
    VERSION 0.1.0
    DESCRIPTION "Open-source AI-native non-linear video editor"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(GNUInstallDirs)
include(VidexSanitizers)
include(VidexWarnings)

option(VIDEX_BUILD_APP "Build the Qt desktop application" ON)
option(VIDEX_BUILD_MEDIA_WORKER "Build the isolated FFmpeg media worker" OFF)
option(VIDEX_REQUIRE_QT "Fail configuration when Qt is unavailable" OFF)
option(VIDEX_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)
option(VIDEX_ENABLE_SANITIZERS "Enable supported runtime sanitizers" OFF)
option(VIDEX_ENABLE_CLANG_TIDY "Run clang-tidy while compiling" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(VIDEX_ENABLE_CLANG_TIDY)
    find_program(VIDEX_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${VIDEX_CLANG_TIDY_EXECUTABLE}")
endif()

add_subdirectory(src/core)

if(VIDEX_BUILD_MEDIA_WORKER)
    find_package(FFMPEG REQUIRED)
    add_subdirectory(src/media)
    add_subdirectory(workers/media-worker)
endif()

if(VIDEX_BUILD_APP)
    find_package(Qt6 6.7 QUIET COMPONENTS Multimedia Widgets)
    if(Qt6_FOUND)
        add_subdirectory(src/render)
        add_subdirectory(src/app)
    elseif(VIDEX_REQUIRE_QT)
        message(FATAL_ERROR "Qt 6.7 or newer with Widgets is required")
    else()
        message(WARNING "Qt 6.7 or newer was not found; the Videx desktop app is skipped")
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
