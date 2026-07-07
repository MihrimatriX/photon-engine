add_executable(photon_app
    main_app.cpp
    application.cpp
)

target_link_libraries(photon_app PRIVATE
    photon_ui
    photon_scene
    photon_engine
    imgui::imgui
    OpenGL::GL
)

if(TARGET glfw)
    target_link_libraries(photon_app PRIVATE glfw)
else()
    target_link_libraries(photon_app PRIVATE glfw3::glfw)
endif()

target_include_directories(photon_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(photon_app PRIVATE cxx_std_20)

# Copy assets next to executable
add_custom_command(TARGET photon_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:photon_app>/assets
    COMMENT "Copying assets to output directory"
)
