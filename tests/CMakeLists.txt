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
    test_sobol.cpp
    test_integrator.cpp
    test_scene_edit.cpp
    test_primitives.cpp
    test_media.cpp
    test_metal.cpp
    test_lights.cpp
    test_imager.cpp
    test_bsdf_verification.cpp
    test_pixel_filter.cpp
    test_robustness.cpp
    test_light_sampler.cpp
    test_model_formats.cpp
    test_render_passes.cpp
    test_render_session.cpp
    test_camera_animation.cpp
    test_camera_light_tools.cpp
    test_realtime_scene.cpp
    test_dcc_import.cpp
    test_package.cpp
    test_live_link.cpp
    test_dcc_convert.cpp
    ${CMAKE_SOURCE_DIR}/src/app/live_link.cpp
)

target_link_libraries(photon_tests PRIVATE
    gtest_main
    photon_engine
    photon_scene
)
if(WIN32)
    target_link_libraries(photon_tests PRIVATE ws2_32) # canlı bağlantı soketleri
endif()

# Set include directory so tests can find core/math/vec.h etc.
target_include_directories(photon_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Gerçek DCC dışa aktarımları (Blender 2.79/2.93, 3ds Max 2009): ufbx'in test verisinden, sabit
# sürüm ve SHA-256 ile; yalnız testler derlenirken indirilir.
set(PHOTON_FBX_TESTDATA "${CMAKE_BINARY_DIR}/testdata/fbx")
foreach(entry
        "blender_279_default_7400_binary.fbx|db93866edd663ccc2e68469db6ec65c040e77786b0b128bfc522a7a860d2035c"
        "blender_293_instancing_7400_binary.fbx|825abc205df1c0b9cb118128e3b45b4c5c1d8e6bf5a62a6034b1e0ebb78939a7"
        "blender_suzanne_multimaterial_7400_binary.fbx|503bdf6683b7473bbd40e2220c9f7ab91b03598f06bf39692379afe020b1a3c6"
        "max2009_cube_texture_6100_ascii.fbx|a0503fd77e56d7c09d94fbc7dbc1ae2a0265b0c142232f31cf7b742a98d4bb9d"
        "blender_279_nested_meshes_7400_binary.fbx|7adab361044b68170aaf3d460137ef004483ffd08c76ea73172cc449cb6348a9")
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 fname)
    list(GET parts 1 fhash)
    file(DOWNLOAD "https://raw.githubusercontent.com/ufbx/ufbx/${PHOTON_UFBX_VERSION}/data/${fname}"
         "${PHOTON_FBX_TESTDATA}/${fname}" EXPECTED_HASH SHA256=${fhash} TLS_VERIFY ON)
endforeach()
target_compile_definitions(photon_tests PRIVATE PHOTON_FBX_TESTDATA="${PHOTON_FBX_TESTDATA}")
if(PHOTON_ALEMBIC_FOUND)
    # Test, Alembic arşivini kendisi yazar (OPolyMesh) ve içe aktarıcıyla geri okur.
    if(TARGET Alembic::Alembic)
        target_link_libraries(photon_tests PRIVATE Alembic::Alembic)
    else()
        target_link_libraries(photon_tests PRIVATE Alembic)
    endif()
endif()
target_compile_definitions(photon_tests PRIVATE PHOTON_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
                                               PHOTON_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")

photon_copy_oidn_dlls(photon_tests)

add_test(NAME photon_tests COMMAND photon_tests)
