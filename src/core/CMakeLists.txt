# core/CMakeLists.txt — photon_core statik kütüphanesi: matematik, renk, görüntü/film,
# iş parçacığı havuzu ve bellek yardımcıları. Diğer tüm modüller buna bağlıdır.
# Başlıkların çoğu yalnızca-başlık (header-only); burada sadece .cpp'ler listelenir.
# PhotonEngine Core Library
# Contains math, image processing, threading, and memory utilities.

add_library(photon_core STATIC)

target_sources(photon_core PRIVATE
    math/vec.cpp
    math/transform.cpp
    color/transfer.cpp
    image/film.cpp
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
# stb ve tinyexr'in uygulaması image_io.cpp içinde derlenir; SYSTEM bayrağı onların
# uyarılarını bastırır, PRIVATE ise bu başlıkların diğer modüllere sızmasını engeller.
target_include_directories(photon_core SYSTEM
    PRIVATE
        ${stb_SOURCE_DIR}
        ${tinyexr_SOURCE_DIR}
)

target_link_libraries(photon_core PRIVATE photon_build_flags)
