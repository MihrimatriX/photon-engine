# src/geometry/CMakeLists.txt — photon_geometry: şekiller (küre, üçgen, mesh), kesişim kaydı
# (SurfaceInteraction) ve BVH hızlandırma yapısı.
add_library(photon_geometry STATIC
    sphere.cpp
    triangle.cpp
    mesh.cpp
    bvh.cpp
)

target_link_libraries(photon_geometry PUBLIC photon_core)
target_include_directories(photon_geometry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_geometry PUBLIC cxx_std_20)

target_link_libraries(photon_geometry PRIVATE photon_build_flags)
