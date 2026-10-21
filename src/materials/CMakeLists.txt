# src/materials/CMakeLists.txt — photon_materials: BSDF'ler (Lambertian, ayna, cam, Disney).
add_library(photon_materials STATIC
    lambertian.cpp
    mirror.cpp
    dielectric.cpp
    disney.cpp
)

target_link_libraries(photon_materials PUBLIC photon_core photon_geometry)
target_include_directories(photon_materials PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_materials PUBLIC cxx_std_20)

target_link_libraries(photon_materials PRIVATE photon_build_flags)
