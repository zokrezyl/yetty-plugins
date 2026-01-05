#=============================================================================
# yetty-plugins - Plugin library for yetty WebGPU Terminal Emulator
#=============================================================================
#
# BUILD:
#   cmake -B build && cmake --build build
#
# Plugins are built as shared libraries and output to build/plugins/
#
#=============================================================================

cmake_minimum_required(VERSION 3.20)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version for dependencies")

project(yetty-plugins VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include CPM.cmake
include(build-tools/cmake/CPM.cmake)

#-----------------------------------------------------------------------------
# Fetch yetty from GitHub (headers + core sources)
#-----------------------------------------------------------------------------
CPMAddPackage(
    NAME yetty
    GITHUB_REPOSITORY zokrezyl/yetty
    GIT_TAG main
    DOWNLOAD_ONLY YES
)

if(NOT yetty_ADDED)
    message(FATAL_ERROR "Failed to fetch yetty from GitHub")
endif()

message(STATUS "yetty fetched to: ${yetty_SOURCE_DIR}")

#-----------------------------------------------------------------------------
# WebGPU setup - use wgpu-native
#-----------------------------------------------------------------------------
include(build-tools/cmake/WgpuNative.cmake)

#-----------------------------------------------------------------------------
# Dependencies (same as yetty main project)
#-----------------------------------------------------------------------------

# GLM for math
CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 1.0.1
)

# stb for image loading
CPMAddPackage(
    NAME stb
    GITHUB_REPOSITORY nothings/stb
    GIT_TAG master
    DOWNLOAD_ONLY YES
)

if(stb_ADDED)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
endif()

# yaml-cpp for config files
CPMAddPackage(
    NAME yaml-cpp
    GITHUB_REPOSITORY jbeder/yaml-cpp
    GIT_TAG 0.8.0
    OPTIONS "YAML_CPP_BUILD_TESTS OFF" "YAML_CPP_BUILD_TOOLS OFF"
)

# spdlog for logging
CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    GIT_TAG v1.16.0
    OPTIONS
        "CMAKE_POSITION_INDEPENDENT_CODE ON"
)

# LZ4 for fast font cache compression
CPMAddPackage(
    NAME lz4
    GITHUB_REPOSITORY lz4/lz4
    GIT_TAG v1.10.0
    SOURCE_SUBDIR build/cmake
    OPTIONS
        "LZ4_BUILD_CLI OFF"
        "LZ4_BUILD_LEGACY_LZ4C OFF"
        "BUILD_SHARED_LIBS OFF"
        "BUILD_STATIC_LIBS ON"
)
if(TARGET lz4_static)
    set_target_properties(lz4_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# FreeType for font loading
CPMAddPackage(
    NAME freetype
    GITHUB_REPOSITORY freetype/freetype
    GIT_TAG VER-2-13-2
    OPTIONS
        "FT_DISABLE_ZLIB OFF"
        "FT_DISABLE_BZIP2 ON"
        "FT_DISABLE_PNG OFF"
        "FT_DISABLE_HARFBUZZ ON"
        "FT_DISABLE_BROTLI ON"
)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
set_target_properties(freetype PROPERTIES POSITION_INDEPENDENT_CODE ON)

# msdfgen with FreeType support
CPMAddPackage(
    NAME msdfgen
    GITHUB_REPOSITORY Chlumsky/msdfgen
    GIT_TAG v1.12
    OPTIONS
        "MSDFGEN_BUILD_STANDALONE OFF"
        "MSDFGEN_CORE_ONLY OFF"
        "MSDFGEN_USE_SKIA OFF"
        "MSDFGEN_USE_VCPKG OFF"
        "MSDFGEN_DISABLE_SVG ON"
        "MSDFGEN_DISABLE_PNG ON"
        "MSDFGEN_INSTALL OFF"
)
if(TARGET msdfgen-core)
    set_target_properties(msdfgen-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
if(TARGET msdfgen-ext)
    set_target_properties(msdfgen-ext PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# MuPDF for PDF rendering
CPMAddPackage(
    NAME mupdf
    URL https://mupdf.com/downloads/archive/mupdf-1.25.4-source.tar.gz
    DOWNLOAD_ONLY YES
)
if(mupdf_ADDED)
    include(${CMAKE_CURRENT_SOURCE_DIR}/build-tools/cmake/mupdf/CMakeLists.txt)
endif()

# GLFW and glfw3webgpu adapter
if(APPLE)
    set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
endif()

CPMAddPackage(
    NAME glfw
    GITHUB_REPOSITORY glfw/glfw
    GIT_TAG 3.4
    OPTIONS
        "GLFW_BUILD_DOCS OFF"
        "GLFW_BUILD_TESTS OFF"
        "GLFW_BUILD_EXAMPLES OFF"
        "GLFW_INSTALL OFF"
        "GLFW_BUILD_WAYLAND OFF"
)

CPMAddPackage(
    NAME glfw3webgpu
    GITHUB_REPOSITORY eliemichel/glfw3webgpu
    GIT_TAG main
)

if(UNIX AND NOT APPLE)
    target_compile_definitions(glfw3webgpu PRIVATE _GLFW_X11)
endif()

# Taywee/args for command line parsing
CPMAddPackage(
    NAME args
    GITHUB_REPOSITORY Taywee/args
    GIT_TAG 6.4.6
)

# FFmpeg for video plugin (Unix only - requires ./configure + make)
if(UNIX)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/build-tools/cmake/ffmpeg ffmpeg_build)
endif()

# Python for python plugin (Unix only - requires ./configure + make)
if(UNIX)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/build-tools/cmake/python python_build)
endif()

#-----------------------------------------------------------------------------
# Build yetty_core shared library from fetched sources
#-----------------------------------------------------------------------------
set(YETTY_CORE_SOURCES
    ${yetty_SOURCE_DIR}/src/yetty/webgpu-context.cpp
    ${yetty_SOURCE_DIR}/src/yetty/font.cpp
    ${yetty_SOURCE_DIR}/src/yetty/font-manager.cpp
    ${yetty_SOURCE_DIR}/src/yetty/rich-text.cpp
    ${yetty_SOURCE_DIR}/src/yetty/emoji-atlas.cpp
)

add_library(yetty_core SHARED ${YETTY_CORE_SOURCES})

target_include_directories(yetty_core PUBLIC
    ${yetty_SOURCE_DIR}/src
    ${yetty_SOURCE_DIR}/include
)

target_compile_definitions(yetty_core PRIVATE
    YETTY_WEB=0
    YETTY_ANDROID=0
)

# Platform-specific font discovery
if(WIN32)
    target_compile_definitions(yetty_core PRIVATE YETTY_USE_DIRECTWRITE=1)
    target_link_libraries(yetty_core PUBLIC dwrite)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
    target_include_directories(yetty_core PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
    target_compile_definitions(yetty_core PRIVATE YETTY_USE_FONTCONFIG=1)
endif()

target_link_libraries(yetty_core PUBLIC
    webgpu
    glfw
    glfw3webgpu
    glm::glm
    stb
    msdfgen::msdfgen-core
    msdfgen::msdfgen-ext
    freetype
    spdlog::spdlog
    lz4_static
    $<$<NOT:$<BOOL:${WIN32}>>:${FONTCONFIG_LIBRARIES}>
)

set_target_properties(yetty_core PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    POSITION_INDEPENDENT_CODE ON
)

#-----------------------------------------------------------------------------
# Build plugins
#-----------------------------------------------------------------------------
add_subdirectory(src/yetty/plugins)

#-----------------------------------------------------------------------------
# Build plugin tester application
#-----------------------------------------------------------------------------
add_executable(yetty-plugin-tester src/tester/main.cpp)

target_include_directories(yetty-plugin-tester PRIVATE
    ${yetty_SOURCE_DIR}/include
    ${yetty_SOURCE_DIR}/src
)

target_link_libraries(yetty-plugin-tester PRIVATE
    yetty_core
    webgpu
    glfw
    glfw3webgpu
    spdlog::spdlog
    args
    ${CMAKE_DL_LIBS}  # For dlopen/dlsym
)

set_target_properties(yetty-plugin-tester PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
