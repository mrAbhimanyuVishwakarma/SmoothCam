cmake_minimum_required(VERSION 3.28)

project(SmoothCam
    VERSION 1.2.0
    DESCRIPTION "Smooth 60 fps virtual webcam for Windows 11"
    HOMEPAGE_URL "https://github.com/smoothcam/SmoothCam"
    LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "SmoothCam builds with MSVC on Windows only.")
endif()

option(SMOOTHCAM_BUILD_TESTS "Build unit tests and the bench tool" ON)
option(SMOOTHCAM_WITH_NVOF "Build the NVIDIA Optical Flow path" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: nothing to redistribute, keeps the installer tiny.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Dependencies)
include(CompileShaders)

# Common compile settings for every SmoothCam target.
add_library(smoothcam_options INTERFACE)
target_compile_definitions(smoothcam_options INTERFACE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A00000C)
target_compile_options(smoothcam_options INTERFACE
    /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc /guard:cf /sdl
    $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Gy /Gw>)
target_link_options(smoothcam_options INTERFACE
    /guard:cf /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /CETCOMPAT
    $<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF /OPT:ICF /DEBUG>)

add_subdirectory(src/core)
add_subdirectory(src/engine)
add_subdirectory(src/vcam)
add_subdirectory(src/app)

if(SMOOTHCAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
