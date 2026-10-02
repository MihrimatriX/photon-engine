add_library(photon_camera STATIC
    perspective_camera.cpp
    thin_lens_camera.cpp
    orthographic_camera.cpp
)

target_link_libraries(photon_camera PUBLIC photon_core)
target_include_directories(photon_camera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_camera PUBLIC cxx_std_20)

target_link_libraries(photon_camera PRIVATE photon_build_flags)
