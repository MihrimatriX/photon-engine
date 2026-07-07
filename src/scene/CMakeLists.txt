add_library(photon_scene STATIC
    scene_graph.cpp
    material_library.cpp
    project_io.cpp
)

target_link_libraries(photon_scene PUBLIC
    photon_engine
    photon_io
)

target_include_directories(photon_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_scene PUBLIC cxx_std_20)
