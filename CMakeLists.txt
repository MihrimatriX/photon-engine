cmake_minimum_required(VERSION 3.20)
project(PhotonEngine VERSION 0.1.0 LANGUAGES CXX)

# ──────────────────────────────────────────────
# C++ Standard
# ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ──────────────────────────────────────────────
# Export compile commands for IDE/tool support
# ──────────────────────────────────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ──────────────────────────────────────────────
# Build Options
# ──────────────────────────────────────────────
option(PHOTON_BUILD_TESTS  "Build unit tests"                    ON)
option(PHOTON_BUILD_CLI    "Build photon_render command-line renderer" ON)
option(PHOTON_ENABLE_SIMD  "Enable AVX2/FMA SIMD instructions"  ON)
option(PHOTON_ENABLE_ASAN "AddressSanitizer on photon_* targets" OFF)
# ponytail: MSVC /WX stays off until a clean /W4 build is seen on the owner's
# machine (this tree is only verified with GCC/Clang). Flip the default then.
if(MSVC)
    option(PHOTON_WARNINGS_AS_ERRORS "Treat warnings in photon_* targets as errors" OFF)
else()
    option(PHOTON_WARNINGS_AS_ERRORS "Treat warnings in photon_* targets as errors" ON)
endif()
# OIDN (Intel Open Image Denoise, Apache-2.0). Not a vcpkg port: the official
# prebuilt package goes into third_party/oidn (scripts/fetch_deps.ps1).
# ON = use it when found; OFF = never link it.
option(PHOTON_ENABLE_OIDN "Use Intel Open Image Denoise when found" ON)
set(PHOTON_OIDN_FOUND FALSE)
set(PHOTON_OIDN_DLL_DIR "")
if(PHOTON_ENABLE_OIDN)
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/third_party/oidn")
    find_package(OpenImageDenoise CONFIG QUIET)
    if(OpenImageDenoise_FOUND)
        set(PHOTON_OIDN_FOUND TRUE)
        if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/oidn/bin")
            set(PHOTON_OIDN_DLL_DIR "${CMAKE_SOURCE_DIR}/third_party/oidn/bin")
        endif()
        message(STATUS "OIDN: found ${OpenImageDenoise_VERSION} — denoise enabled")
    else()
        message(STATUS "OIDN: not found — run scripts/fetch_deps.ps1 (soft blur fallback only)")
    endif()
endif()

# Intel Embree 4 (Apache-2.0): resmi derlenmiş paket third_party/embree'de
# (scripts/fetch_deps.ps1). Bulunursa üretim ışın kesişimi onunla yapılır.
option(PHOTON_ENABLE_EMBREE "Use Intel Embree when found" ON)
set(PHOTON_EMBREE_DLL_DIR "")
if(PHOTON_ENABLE_EMBREE)
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/third_party/embree")
    find_package(embree 4 CONFIG QUIET)
    if(embree_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/embree/bin")
        set(PHOTON_EMBREE_DLL_DIR "${CMAKE_SOURCE_DIR}/third_party/embree/bin")
    endif()
endif()

# Çalışma zamanı DLL'lerini exe'nin yanına kopyalar (Windows'ta rpath yok).
# Önce Embree, sonra OIDN: ikisi de tbb12.dll ister, OIDN'ninki daha yeni (2023.1)
# ve oneTBB ikili arayüzü geriye uyumlu olduğu için onunki kalır.
function(photon_copy_oidn_dlls target)
    if(PHOTON_EMBREE_DLL_DIR)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${PHOTON_EMBREE_DLL_DIR}" $<TARGET_FILE_DIR:${target}>
            COMMENT "Copying Embree DLLs for ${target}")
    endif()
    if(PHOTON_OIDN_DLL_DIR)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${PHOTON_OIDN_DLL_DIR}" $<TARGET_FILE_DIR:${target}>
            COMMENT "Copying OIDN DLLs for ${target}")
    endif()
endfunction()

# ──────────────────────────────────────────────
# Compiler Warnings & Flags
# ──────────────────────────────────────────────
# Process-wide definitions only. Warnings and float-model flags live on
# photon_build_flags so third-party code keeps its own settings.
if(MSVC)
    add_compile_options(/MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# Linked PRIVATE by every photon_* target.
add_library(photon_build_flags INTERFACE)
if(MSVC)
    target_compile_options(photon_build_flags INTERFACE /W4 /utf-8 /fp:fast)
    if(PHOTON_WARNINGS_AS_ERRORS)
        target_compile_options(photon_build_flags INTERFACE /WX)
    endif()
else()
    # Fast math, but keep NaN/Inf semantics: the sample path must be able to
    # reject non-finite radiance (see core/math/float_bits.h).
    target_compile_options(photon_build_flags INTERFACE
        -Wall -Wextra -Wpedantic -ffast-math -fno-finite-math-only)
    if(PHOTON_WARNINGS_AS_ERRORS)
        target_compile_options(photon_build_flags INTERFACE -Werror)
    endif()
endif()

# ──────────────────────────────────────────────
# SIMD Configuration
# ──────────────────────────────────────────────
if(PHOTON_ENABLE_SIMD)
    if(MSVC)
        add_compile_options(/arch:AVX2)
    else()
        add_compile_options(-mavx2 -mfma)
    endif()
endif()

# ──────────────────────────────────────────────
# FetchContent – External Dependencies
# ──────────────────────────────────────────────
include(FetchContent)

# stb – single-file image I/O headers
# Pinned 2026-10-01 from branch master.
# SOURCE_SUBDIR points at a folder with no CMakeLists.txt: the headers are
# fetched but the projects' own library/sample targets are never configured.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
    SOURCE_SUBDIR  photon-headers-only
)

# tinyobjloader – Wavefront OBJ loader
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG        v2.0.0rc13
    SOURCE_SUBDIR  photon-headers-only
)

# tinyexr – EXR image loader/writer
FetchContent_Declare(
    tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG        v1.0.8
    SOURCE_SUBDIR  photon-headers-only
)

FetchContent_MakeAvailable(stb tinyobjloader tinyexr)

# cgltf – single-header glTF 2.0 loader
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.13
    SOURCE_SUBDIR  photon-headers-only
)
FetchContent_MakeAvailable(cgltf)

# nlohmann/json – project files, material and studio presets (MIT)
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Google Test (conditional)
if(PHOTON_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    # Prevent gtest from overriding parent project's compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# ──────────────────────────────────────────────
# Subdirectories – Engine Modules
# ──────────────────────────────────────────────
add_subdirectory(src/core)
add_subdirectory(src/geometry)
add_subdirectory(src/materials)
add_subdirectory(src/lights)
add_subdirectory(src/camera)
add_subdirectory(src/integrators)
add_subdirectory(src/samplers)
add_subdirectory(src/io)
add_subdirectory(src/engine)

# Scene graph is CPU-only. Tests use it with the desktop app off.
add_subdirectory(src/scene)

# Desktop UI application (GLFW + ImGui)
option(PHOTON_BUILD_APP "Build photon_app desktop UI" ON)
if(PHOTON_BUILD_APP)
    find_package(glfw3 CONFIG QUIET)
    find_package(imgui CONFIG QUIET)

    if(NOT glfw3_FOUND)
        FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG        3.4
        )
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(glfw)
    endif()

    if(NOT imgui_FOUND)
        FetchContent_Declare(
            imgui_fc
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            # Pinned 2026-10-01 from branch docking.
            GIT_TAG        64944b4520b30772de8dbf0b37d0311746477a32
        )
        FetchContent_MakeAvailable(imgui_fc)
        add_library(imgui_app STATIC
            ${imgui_fc_SOURCE_DIR}/imgui.cpp
            ${imgui_fc_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_fc_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_fc_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_fc_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_fc_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        )
        target_include_directories(imgui_app SYSTEM PUBLIC
            ${imgui_fc_SOURCE_DIR}
            ${imgui_fc_SOURCE_DIR}/backends
        )
        target_link_libraries(imgui_app PUBLIC glfw)
        add_library(imgui::imgui ALIAS imgui_app)
    endif()

    # ImGuizmo – translate/rotate/scale gizmo (MIT)
    find_package(imguizmo CONFIG QUIET)
    if(NOT imguizmo_FOUND)
        FetchContent_Declare(
            imguizmo_fc
            GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
            GIT_TAG        1.91.3
            SOURCE_SUBDIR  photon-headers-only
        )
        FetchContent_MakeAvailable(imguizmo_fc)
        add_library(imguizmo_app STATIC ${imguizmo_fc_SOURCE_DIR}/ImGuizmo.cpp)
        target_include_directories(imguizmo_app SYSTEM PUBLIC ${imguizmo_fc_SOURCE_DIR})
        target_link_libraries(imguizmo_app PUBLIC imgui::imgui)
        add_library(imguizmo::imguizmo ALIAS imguizmo_app)
    endif()

    find_package(OpenGL REQUIRED)
    add_subdirectory(src/ui)
    add_subdirectory(src/app)
endif()

# Tests (optional)
if(PHOTON_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ──────────────────────────────────────────────
# CLI Executable (dev only, OFF by default)
# ──────────────────────────────────────────────
if(PHOTON_BUILD_CLI)
    add_executable(photon_render src/main.cpp)
    target_link_libraries(photon_render PRIVATE photon_scene photon_engine photon_build_flags)
    photon_copy_oidn_dlls(photon_render)
endif()

# ponytail: ASan instruments photon_* only. vcpkg and FetchContent stay
# uninstrumented. Upgrade: an ASan vcpkg triplet if those libs must be checked.
if(PHOTON_ENABLE_ASAN)
    function(_photon_apply_asan dir)
        get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_t IN LISTS _targets)
            if(NOT _t MATCHES "^photon_")
                continue()
            endif()
            get_target_property(_kind "${_t}" TYPE)
            if(_kind STREQUAL "INTERFACE_LIBRARY" OR _kind STREQUAL "UTILITY")
                continue()
            endif()
            if(MSVC)
                # /RTC1 (Debug default) is incompatible with /fsanitize=address.
                target_compile_options("${_t}" PRIVATE /fsanitize=address /RTC1-)
                if(_kind STREQUAL "EXECUTABLE" OR _kind STREQUAL "SHARED_LIBRARY")
                    target_link_options("${_t}" PRIVATE /INCREMENTAL:NO)
                endif()
            else()
                target_compile_options("${_t}" PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                if(_kind STREQUAL "EXECUTABLE" OR _kind STREQUAL "SHARED_LIBRARY")
                    target_link_options("${_t}" PRIVATE -fsanitize=address,undefined)
                endif()
            endif()
        endforeach()
        get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(_sub IN LISTS _subs)
            _photon_apply_asan("${_sub}")
        endforeach()
    endfunction()
    _photon_apply_asan("${CMAKE_SOURCE_DIR}")
endif()
