add_library(photon_engine STATIC
    scene.cpp
    renderer.cpp
    denoiser.cpp
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

target_link_libraries(photon_engine PRIVATE photon_build_flags)
