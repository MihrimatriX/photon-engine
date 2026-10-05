# Birim testleri (GoogleTest). Yeni test dosyaları buraya eklenir.
add_executable(photon_tests
    test_vec.cpp
    test_ray.cpp
    test_aabb.cpp
    test_color.cpp
    test_sampling.cpp
    test_sphere.cpp
    test_triangle.cpp
    test_bvh.cpp
    test_disney.cpp
    test_area_light.cpp
    test_environment.cpp
    test_dielectric.cpp
    test_mesh_uv.cpp
    test_product.cpp
    test_stratified.cpp
    test_mesh_light.cpp
    test_import.cpp
    test_ortho.cpp
    test_loader_limits.cpp
    test_snell.cpp
)

target_link_libraries(photon_tests PRIVATE
    gtest_main
    photon_engine
    photon_scene
)

# Set include directory so tests can find core/math/vec.h etc.
target_include_directories(photon_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME photon_tests COMMAND photon_tests)
