# src/lights/CMakeLists.txt — photon_lights: ışık kaynakları (nokta, yönlü, alan/mesh, ortam/HDRI).
# Integrator doğrudan aydınlatma (next-event estimation) için bunların sampleLi/pdfLi'sini çağırır.
add_library(photon_lights STATIC
    point_light.cpp
    directional_light.cpp
    area_light.cpp
    environment_light.cpp
)

target_link_libraries(photon_lights PUBLIC photon_core photon_geometry)
target_include_directories(photon_lights PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_lights PUBLIC cxx_std_20)

target_link_libraries(photon_lights PRIVATE photon_build_flags)
