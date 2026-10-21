# src/io/CMakeLists.txt — photon_io: model dosyası yükleyicileri (OBJ, glTF).
# Dış dosya biçimlerini motorun TriangleMesh + Material türlerine çevirir. tinyobjloader ve
# cgltf tek başlıklı (single-header) kütüphanelerdir; uygulamaları yalnız bu hedefte derlenir.
add_library(photon_io STATIC
    obj_loader.cpp
    gltf_loader.cpp
)

target_link_libraries(photon_io PUBLIC
    photon_core
    photon_geometry
    photon_materials
)

# Link private tinyobjloader since we use it in obj_loader.cpp
# FetchContent of tinyobjloader might not define a library, so we include its source dir.
target_include_directories(photon_io SYSTEM PRIVATE ${tinyobjloader_SOURCE_DIR} ${cgltf_SOURCE_DIR})
target_include_directories(photon_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_io PUBLIC cxx_std_20)

target_link_libraries(photon_io PRIVATE photon_build_flags)
