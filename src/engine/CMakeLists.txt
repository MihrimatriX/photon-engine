add_library(photon_engine STATIC
    scene.cpp
    renderer.cpp
)

target_link_libraries(photon_engine PUBLIC
    photon_core
    photon_geometry
    photon_materials
    photon_lights
    photon_camera
    photon_samplers
    photon_integrators
    photon_io
)

target_include_directories(photon_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_engine PUBLIC cxx_std_20)
