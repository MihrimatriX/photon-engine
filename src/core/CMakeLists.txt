# PhotonEngine Core Library
# Contains math, image processing, threading, and memory utilities.

add_library(photon_core STATIC)

target_sources(photon_core PRIVATE
    math/vec.cpp
    math/transform.cpp
    image/image.cpp
    image/image_io.cpp
    image/tone_mapping.cpp
    threading/thread_pool.cpp
    memory/arena_allocator.cpp
)

# Public include directory — parent src/ folder so includes look like:
#   #include "core/math/vec.h"
target_include_directories(photon_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Require C++20
target_compile_features(photon_core PUBLIC cxx_std_20)

# Private include dirs for third-party header-only libraries
target_include_directories(photon_core
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/stb
        ${CMAKE_SOURCE_DIR}/external/tinyexr
)
