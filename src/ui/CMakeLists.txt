# photon_ui — arayüz yardımcıları: tema/fontlar, ortak widget'lar, orbit kamera,
# yerel dosya diyaloğu. ImGui'ye bağlıdır; motor kodu buna bağlı değildir.
add_library(photon_ui STATIC
    orbit_camera.cpp
    theme.cpp
    widgets.cpp
    file_dialog.cpp
)

target_link_libraries(photon_ui PUBLIC
    photon_core
    imgui::imgui
)

target_include_directories(photon_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_ui PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(photon_ui PUBLIC comdlg32 shell32)
endif()

target_link_libraries(photon_ui PRIVATE photon_build_flags)
