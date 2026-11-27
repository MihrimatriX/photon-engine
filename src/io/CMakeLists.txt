# src/io/CMakeLists.txt — photon_io: model dosyası yükleyicileri (OBJ, glTF, FBX, Alembic, USD).
# Dış dosya biçimlerini motorun TriangleMesh + Material türlerine çevirir. tinyobjloader ve
# cgltf tek başlıklı (single-header) kütüphanelerdir; uygulamaları yalnız bu hedefte derlenir.
add_library(photon_io STATIC
    obj_loader.cpp
    gltf_loader.cpp
    imported_model.cpp
    fbx_loader.cpp
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

target_link_libraries(photon_io PRIVATE photon_build_flags photon_ufbx)

if(PHOTON_ALEMBIC_FOUND)
    target_sources(photon_io PRIVATE alembic_loader.cpp)
    if(TARGET Alembic::Alembic)
        target_link_libraries(photon_io PRIVATE Alembic::Alembic)
    else()
        target_link_libraries(photon_io PRIVATE Alembic)
    endif()
    target_compile_definitions(photon_io PUBLIC PHOTON_HAS_ALEMBIC=1)
endif()

if(PHOTON_USD_FOUND)
    target_sources(photon_io PRIVATE usd_loader.cpp)
    target_link_libraries(photon_io PRIVATE usd_m)
    target_compile_definitions(photon_io PUBLIC PHOTON_HAS_USD=1)
    if(MSVC)
        # USD başlıkları /W4'te çok sayıda uyarı verir; dosya düzeyinde kapatılır (başlıklar SYSTEM).
        set_source_files_properties(usd_loader.cpp PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4305;/wd4267;/wd4100;/wd4127")
    else()
        # TfHashSet eski <ext/hash_set> başlığını içerir (GCC "#warning" verir).
        set_source_files_properties(usd_loader.cpp PROPERTIES COMPILE_OPTIONS "-Wno-deprecated")
    endif()
endif()
