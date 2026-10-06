# photon_app — masaüstü düzenleyici (GLFW + Dear ImGui + ImGuizmo).
add_executable(photon_app WIN32
    main_app.cpp
    application.cpp
    render_controller.cpp
    thumbnails.cpp
    scene_ops.cpp
    final_render.cpp
    ui_common.cpp
    ui_layout.cpp
    ui_library.cpp
    ui_viewport.cpp
    ui_scene.cpp
    ui_properties.cpp
    ui_render.cpp
)

target_link_libraries(photon_app PRIVATE
    photon_ui
    photon_scene
    photon_engine
    imgui::imgui
    imguizmo::imguizmo
    OpenGL::GL
    photon_build_flags
)

if(TARGET glfw)
    target_link_libraries(photon_app PRIVATE glfw)
else()
    target_link_libraries(photon_app PRIVATE glfw3::glfw)
endif()

if(MSVC)
    # WIN32 alt sistemi (konsol penceresi yok) ama giriş noktası yine main().
    target_link_options(photon_app PRIVATE /ENTRY:mainCRTStartup)
endif()

target_include_directories(photon_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_app PRIVATE cxx_std_20)

# Varlıklar (malzemeler, HDRI, fontlar) exe'nin yanına kopyalanır.
add_custom_command(TARGET photon_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:photon_app>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:photon_app>/assets
    COMMENT "Copying assets to output directory"
)
photon_copy_oidn_dlls(photon_app)

# Hakkında penceresindeki sürüm; tek kaynak kök CMakeLists'teki project(VERSION).
target_compile_definitions(photon_app PRIVATE PHOTON_VERSION="${PROJECT_VERSION}")
