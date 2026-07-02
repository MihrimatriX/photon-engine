add_library(photon_integrators STATIC
    path_tracer.cpp
)

target_link_libraries(photon_integrators PUBLIC
    photon_core
    photon_geometry
    photon_materials
    photon_lights
    photon_camera
    photon_samplers
)

target_include_directories(photon_integrators PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_integrators PUBLIC cxx_std_20)
