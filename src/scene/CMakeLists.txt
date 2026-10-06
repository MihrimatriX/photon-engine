# Düzenlenebilir sahne katmanı: sahne ağacı, malzeme kütüphanesi, belge (ışık/ortam),
# model içe aktarma ve proje dosyaları. Masaüstü uygulaması kapalıyken de testler kullanır.
add_library(photon_scene STATIC
    scene_graph.cpp
    material_library.cpp
    document.cpp
    model_import.cpp
    project_io.cpp
    primitives.cpp
)

target_link_libraries(photon_scene PUBLIC
    photon_engine
    photon_io
    nlohmann_json::nlohmann_json
)

target_include_directories(photon_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_scene PUBLIC cxx_std_20)

target_link_libraries(photon_scene PRIVATE photon_build_flags)
