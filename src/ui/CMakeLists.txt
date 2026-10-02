add_library(photon_ui STATIC
    orbit_camera.cpp
    theme.cpp
    file_dialog.cpp
)

if(TARGET imgui_app)
    target_link_libraries(imgui_app PUBLIC OpenGL::GL)
endif()

target_link_libraries(photon_ui PUBLIC
    photon_preview
    imgui::imgui
)

target_include_directories(photon_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_ui PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(photon_ui PUBLIC comdlg32)
endif()

target_link_libraries(photon_ui PRIVATE photon_build_flags)
