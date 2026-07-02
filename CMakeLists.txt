cmake_minimum_required(VERSION 3.20)
project(PhotonEngine VERSION 0.1.0 LANGUAGES CXX)

# ──────────────────────────────────────────────
# C++ Standard
# ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ──────────────────────────────────────────────
# Export compile commands for IDE/tool support
# ──────────────────────────────────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ──────────────────────────────────────────────
# Build Options
# ──────────────────────────────────────────────
option(PHOTON_BUILD_TESTS  "Build unit tests"                    ON)
option(PHOTON_BUILD_GPU    "Build GPU (OptiX/Vulkan) backend"    OFF)
option(PHOTON_ENABLE_SIMD  "Enable AVX2/FMA SIMD instructions"  ON)
option(PHOTON_ENABLE_OIDN  "Enable Intel Open Image Denoise"    OFF)

# ──────────────────────────────────────────────
# Compiler Warnings & Flags
# ──────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX- /MP /fp:fast)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -ffast-math)
endif()

# ──────────────────────────────────────────────
# SIMD Configuration
# ──────────────────────────────────────────────
if(PHOTON_ENABLE_SIMD)
    if(MSVC)
        add_compile_options(/arch:AVX2)
    else()
        add_compile_options(-mavx2 -mfma)
    endif()
endif()

# ──────────────────────────────────────────────
# FetchContent – External Dependencies
# ──────────────────────────────────────────────
include(FetchContent)

# stb – single-file image I/O headers
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)

# tinyobjloader – Wavefront OBJ loader
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG        v2.0.0rc13
)

# tinyexr – EXR image loader/writer
FetchContent_Declare(
    tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG        v1.0.8
)

FetchContent_MakeAvailable(stb tinyobjloader tinyexr)

# Google Test (conditional)
if(PHOTON_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    # Prevent gtest from overriding parent project's compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# ──────────────────────────────────────────────
# Subdirectories – Engine Modules
# ──────────────────────────────────────────────
add_subdirectory(src/core)
add_subdirectory(src/geometry)
add_subdirectory(src/materials)
add_subdirectory(src/lights)
add_subdirectory(src/camera)
add_subdirectory(src/integrators)
add_subdirectory(src/samplers)
add_subdirectory(src/io)
add_subdirectory(src/engine)

# GPU backend (optional)
if(PHOTON_BUILD_GPU)
    add_subdirectory(src/gpu)
endif()

# Tests (optional)
if(PHOTON_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ──────────────────────────────────────────────
# Main Executable
# ──────────────────────────────────────────────
add_executable(photon_render src/main.cpp)
target_link_libraries(photon_render PRIVATE photon_engine)
