add_library(photon_preview STATIC
    viewport_texture.cpp
    gl_preview.cpp
)

target_link_libraries(photon_preview PUBLIC
    photon_scene
    OpenGL::GL
)

target_include_directories(photon_preview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_preview PUBLIC cxx_std_20)
