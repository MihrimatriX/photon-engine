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
)

target_link_libraries(photon_tests PRIVATE
    gtest_main
    photon_engine
)

# Set include directory so tests can find core/math/vec.h etc.
target_include_directories(photon_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME photon_tests COMMAND photon_tests)
