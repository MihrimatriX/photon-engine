# Örnekleyiciler: bağımsız (beyaz gürültü), tabakalı ve Owen-karıştırmalı Sobol.
add_library(photon_samplers STATIC
    independent_sampler.cpp
    stratified_sampler.cpp
    sobol_sampler.cpp
)

target_link_libraries(photon_samplers PUBLIC photon_core)
target_include_directories(photon_samplers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_samplers PUBLIC cxx_std_20)

target_link_libraries(photon_samplers PRIVATE photon_build_flags)
