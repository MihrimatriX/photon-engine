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
option(PHOTON_BUILD_CLI    "Build photon_render CLI (dev only)"  OFF)
option(PHOTON_BUILD_GPU    "Build GPU (OptiX/Vulkan) backend"    OFF)
option(PHOTON_ENABLE_SIMD  "Enable AVX2/FMA SIMD instructions"  ON)
option(PHOTON_ENABLE_ASAN "AddressSanitizer on photon_* targets" OFF)
# OIDN: auto-enable when the SDK is findable (vcpkg / CMAKE_PREFIX_PATH).
# Force with -DPHOTON_ENABLE_OIDN=ON; disable with -DPHOTON_ENABLE_OIDN=OFF.
option(PHOTON_ENABLE_OIDN "Enable Intel Open Image Denoise (AUTO if found)" OFF)
set(PHOTON_OIDN_FOUND FALSE)
find_package(OpenImageDenoise CONFIG QUIET)
if(OpenImageDenoise_FOUND)
    set(PHOTON_OIDN_FOUND TRUE)
    message(STATUS "OIDN: Open Image Denoise found — denoise enabled")
elseif(PHOTON_ENABLE_OIDN)
    message(WARNING "PHOTON_ENABLE_OIDN=ON but OpenImageDenoise not found — soft blur fallback only.\n"
                    "  Install: vcpkg install openimagedenoise, then reconfigure with\n"
                    "  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake")
endif()

# ──────────────────────────────────────────────
# Compiler Warnings & Flags
# ──────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX- /MP /fp:fast)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -ffast-math)
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
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
)

# tinyobjloader – Wavefront OBJ loader
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG        v2.0.0rc13
)

# tinyexr – EXR image loader/writer
FetchContent_Declare(
    tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG        v1.0.8
)

FetchContent_MakeAvailable(stb tinyobjloader tinyexr)

# cgltf – single-header glTF 2.0 loader
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.13
)
FetchContent_MakeAvailable(cgltf)

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

# GPU backend (optional)
if(PHOTON_BUILD_GPU)
    add_subdirectory(src/gpu)
endif()

# Desktop UI application (GLFW + ImGui)
option(PHOTON_BUILD_APP "Build photon_app desktop UI" ON)
if(PHOTON_BUILD_APP)
    find_package(glfw3 CONFIG QUIET)
    find_package(imgui CONFIG QUIET)
    find_package(glm CONFIG QUIET)

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
        target_include_directories(imgui_app PUBLIC
            ${imgui_fc_SOURCE_DIR}
            ${imgui_fc_SOURCE_DIR}/backends
        )
        target_compile_definitions(imgui_app PUBLIC IMGUI_HAS_DOCK)
        target_link_libraries(imgui_app PUBLIC glfw)
        add_library(imgui::imgui ALIAS imgui_app)
    endif()

    if(NOT glm_FOUND)
        FetchContent_Declare(
            glm
            GIT_REPOSITORY https://github.com/g-truc/glm.git
            GIT_TAG        1.0.1
        )
        FetchContent_MakeAvailable(glm)
    endif()

    find_package(OpenGL REQUIRED)
    add_subdirectory(src/preview)
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
    target_link_libraries(photon_render PRIVATE photon_engine)
endif()

# ponytail: ASan instruments photon_* only. vcpkg and FetchContent stay
# uninstrumented. Upgrade: an ASan vcpkg triplet if those libs must be checked.
if(PHOTON_ENABLE_ASAN AND MSVC)
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
            # /RTC1 (Debug default) is incompatible with /fsanitize=address.
            target_compile_options("${_t}" PRIVATE /fsanitize=address /RTC1-)
            if(_kind STREQUAL "EXECUTABLE" OR _kind STREQUAL "SHARED_LIBRARY")
                target_link_options("${_t}" PRIVATE /INCREMENTAL:NO)
            endif()
        endforeach()
        get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(_sub IN LISTS _subs)
            _photon_apply_asan("${_sub}")
        endforeach()
    endfunction()
    _photon_apply_asan("${CMAKE_SOURCE_DIR}")
endif()
