add_library(photon_engine STATIC
    scene.cpp
    renderer.cpp
    denoiser.cpp
    embree_accel.cpp
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

if(PHOTON_OIDN_FOUND)
    target_compile_definitions(photon_engine PUBLIC PHOTON_ENABLE_OIDN)
    if(TARGET OpenImageDenoise::OpenImageDenoise)
        target_link_libraries(photon_engine PUBLIC OpenImageDenoise::OpenImageDenoise)
    else()
        target_link_libraries(photon_engine PUBLIC OpenImageDenoise)
    endif()
endif()

# Intel Embree 4 (Apache-2.0): bulunursa üretim ışın kesişimi onunla yapılır.
if(embree_FOUND)
    target_compile_definitions(photon_engine PRIVATE PHOTON_HAS_EMBREE)
    target_link_libraries(photon_engine PRIVATE embree)
    message(STATUS "Embree: found ${embree_VERSION} — production ray queries")
else()
    message(STATUS "Embree: not found — using the built-in BVH")
endif()

target_link_libraries(photon_engine PRIVATE photon_build_flags)
