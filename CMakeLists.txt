cmake_minimum_required(VERSION 3.30) # ozz-animation 0.17 needs 3.30
project(engine LANGUAGES C CXX)

# Two builds from one source (docs/specs/d3d12_port.md): the web build (Emscripten, WebGPU) and the Windows x64 build (MSVC, Direct3D 12).
if(EMSCRIPTEN)
    set(NV_WEB TRUE)
elseif(WIN32 AND MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    set(NV_WINDOWS TRUE)
else()
    message(FATAL_ERROR "nv builds for the web (emcmake cmake -S . -B build -G Ninja) and for Windows x64 with MSVC "
        "(cmake -S . -B build-win -G Ninja in a Visual Studio x64 Native Tools Command Prompt).")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17) # ozz-animation and our wrapper around it
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NV_WEB)
    add_compile_options(-Wall -Wextra)
endif()
# The warnings and the source encoding of our own code on MSVC are set below, after the external libraries: those come with
# their own options (-w / /w) and must not get ours.

if(NV_WEB)
    # WebGPU comes from the browser through Emscripten's emdawnwebgpu port.
    add_library(webgpu INTERFACE)
    target_compile_options(webgpu INTERFACE --use-port=emdawnwebgpu)
    target_link_options(webgpu INTERFACE
        --use-port=emdawnwebgpu
        -sDEFAULT_TO_CXX   # the port's implementation is C++
        -sASYNCIFY         # lets wgpuInstanceWaitAny yield to the browser during setup
        -sALLOW_MEMORY_GROWTH
        -sENVIRONMENT=web)
endif()
# What switches off warnings of an external library: -w for clang, /w for MSVC.
if(NV_WEB)
    set(NV_NO_WARNINGS -w)
else()
    set(NV_NO_WARNINGS /w)
endif()

# Emits <target>.js, .wasm and .manifest.json plus index.html (from web/index.html.in) into the
# target's directory, and installs them into <prefix>/<target>/ as an install component ("web" unless COMPONENT says
# otherwise).
#   nv_setup_executable(<target> [ROOT] [COMPONENT <name>] [ASSETS <dir>] [PRELOAD <dir>@<path> ...])
# ROOT installs into <prefix>/ instead, so the page is the package's index.html.
# COMPONENT names the install component: executables that are deployed apart (docs/specs/battle.md) take one each.
# PRELOAD packs each <dir> at <path> in the executable's file system, readable with fopen; they ship as <target>.data.
# ASSETS <dir> is PRELOAD <dir>@/assets.
# Debug builds keep their DWARF debug info in <target>.debug.wasm next to the page. The page does
# not download it; browser developer tools fetch it for source-level debugging.
#
# On Windows (MSVC) it makes the executable <target>.exe instead, with the application manifest (Per-Monitor V2 DPI, UTF-8 code
# page) built in, and copies next to it what it reads: each PRELOAD directory to <path> under the executable's folder, which
# nv_file_read maps a leading "/" to (ROOT and COMPONENT only matter to the web package), and the D3D12 runtime of the pinned
# Agility SDK to D3D12\ (the debug layer too in Debug).
function(nv_setup_executable target)
    cmake_parse_arguments(ARG "ROOT" "ASSETS;COMPONENT" "PRELOAD" ${ARGN})
    if(NOT ARG_COMPONENT)
        set(ARG_COMPONENT web)
    endif()
    set(files)
    if(NV_WEB)
        set_target_properties(${target} PROPERTIES SUFFIX ".js")
        set(NV_WEB_TARGET "${target}")
        configure_file("${PROJECT_SOURCE_DIR}/web/index.html.in"
            "${CMAKE_CURRENT_BINARY_DIR}/index.html" @ONLY)
        set(files
            "$<TARGET_FILE_DIR:${target}>/${target}.js"
            "$<TARGET_FILE_DIR:${target}>/${target}.wasm"
            "$<TARGET_FILE_DIR:${target}>/${target}.manifest.json"
            "${CMAKE_CURRENT_BINARY_DIR}/index.html")
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_link_options(${target} PRIVATE
                "-gseparate-dwarf=$<TARGET_FILE_DIR:${target}>/${target}.debug.wasm"
                "-sSEPARATE_DWARF_URL=${target}.debug.wasm")
            list(APPEND files "$<TARGET_FILE_DIR:${target}>/${target}.debug.wasm")
        endif()
        # The page reads the build's version and the file sizes from the manifest (docs/specs/page_loading.md).
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -DDIR=$<TARGET_FILE_DIR:${target}> -DTARGET=${target}
                    -P "${PROJECT_SOURCE_DIR}/web/manifest.cmake"
            VERBATIM)
    else()
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/engine/nv.manifest")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/D3D12"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NV_AGILITY_BIN}/D3D12Core.dll" "$<TARGET_FILE_DIR:${target}>/D3D12"
            VERBATIM)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            # The debug layer has to be the runtime's own version.
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NV_AGILITY_BIN}/d3d12SDKLayers.dll" "$<TARGET_FILE_DIR:${target}>/D3D12"
                VERBATIM)
        endif()
    endif()

    set(preloads ${ARG_PRELOAD})
    if(ARG_ASSETS)
        list(APPEND preloads "${ARG_ASSETS}@/assets")
    endif()
    if(preloads)
        set(packed_files)
        foreach(preload ${preloads})
            string(REGEX REPLACE "@.*$" "" preload_dir "${preload}")
            # CONFIGURE_DEPENDS: a file added or removed re-runs the glob, so an old build directory does not stop on a
            # file that is gone.
            file(GLOB_RECURSE found CONFIGURE_DEPENDS "${preload_dir}/*")
            # A Git LFS pointer instead of the real file means the clone skipped LFS.
            foreach(asset ${found})
                file(READ "${asset}" head LIMIT 40)
                if(head MATCHES "^version https://git-lfs")
                    message(FATAL_ERROR "${asset} is a Git LFS pointer. Run: git lfs install && git lfs pull")
                endif()
            endforeach()
            list(APPEND packed_files ${found})
            if(NV_WEB)
                target_link_options(${target} PRIVATE "--preload-file=${preload}")
            else()
                string(REGEX REPLACE "^.*@" "" preload_path "${preload}")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${preload_dir}" "$<TARGET_FILE_DIR:${target}>${preload_path}"
                    VERBATIM)
            endif()
        endforeach()
        if(NV_WEB)
            set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${packed_files})
            list(APPEND files "$<TARGET_FILE_DIR:${target}>/${target}.data")
        endif()
    endif()

    if(NV_WEB)
        set(destination ${target})
        if(ARG_ROOT)
            set(destination .)
        endif()
        install(FILES ${files} DESTINATION ${destination} COMPONENT ${ARG_COMPONENT})
    endif()
endfunction()

# Names the commit a target's build comes from: its short hash and subject line, as nv_version.h in the target's binary
# directory (cmake/version.cmake rewrites it on every build, so it is never the commit of an older configure), and the build
# type as NV_BUILD_NAME. Shown on build labels and in benchmark reports.
#   nv_add_version(<target>)
function(nv_add_version target)
    set(header "${CMAKE_CURRENT_BINARY_DIR}/nv_version.h")
    add_custom_target(${target}_version
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUTPUT=${header}
                -P ${PROJECT_SOURCE_DIR}/cmake/version.cmake
        BYPRODUCTS ${header}
        COMMENT "Recording the commit")
    add_dependencies(${target} ${target}_version)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(${target} PRIVATE NV_BUILD_NAME="${CMAKE_BUILD_TYPE}")
endfunction()

# Dear ImGui through cimgui, its C API. Only the sources are fetched; the library is defined
# here so cimgui's own install rules stay out of the web package.
include(FetchContent)
FetchContent_Declare(cimgui
    GIT_REPOSITORY https://github.com/cimgui/cimgui.git
    GIT_TAG        125f397e0982fe9466eb0a162de547be5efec849 # Dear ImGui 1.92.9b
    GIT_SUBMODULES imgui
    SOURCE_SUBDIR  no-cmake)
FetchContent_MakeAvailable(cimgui)

add_library(cimgui STATIC
    ${cimgui_SOURCE_DIR}/cimgui.cpp
    ${cimgui_SOURCE_DIR}/imgui/imgui.cpp
    ${cimgui_SOURCE_DIR}/imgui/imgui_demo.cpp
    ${cimgui_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${cimgui_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${cimgui_SOURCE_DIR}/imgui/imgui_widgets.cpp)
target_include_directories(cimgui SYSTEM PUBLIC ${cimgui_SOURCE_DIR})
target_include_directories(cimgui PRIVATE ${cimgui_SOURCE_DIR}/imgui)
target_compile_definitions(cimgui
    PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    INTERFACE CIMGUI_DEFINE_ENUMS_AND_STRUCTS) # C users get plain structs; cimgui.cpp must not
target_compile_options(cimgui PRIVATE ${NV_NO_WARNINGS})

# ImGuizmo (transform gizmos) through cimguizmo, its C API. Built into the cimgui library so it
# shares ImGui's context.
FetchContent_Declare(cimguizmo
    GIT_REPOSITORY https://github.com/cimgui/cimguizmo.git
    GIT_TAG        eaf7d7b0bcb5f62374305a21f69af55fca4b1ee7 # ImGuizmo dc25afb
    GIT_SUBMODULES ImGuizmo
    SOURCE_SUBDIR  no-cmake)
FetchContent_MakeAvailable(cimguizmo)
target_sources(cimgui PRIVATE
    ${cimguizmo_SOURCE_DIR}/cimguizmo.cpp
    ${cimguizmo_SOURCE_DIR}/ImGuizmo/src/ImGuizmo.cpp)
target_include_directories(cimgui SYSTEM PUBLIC ${cimguizmo_SOURCE_DIR})

# cgltf: single-header glTF parser. The implementation gets its own translation unit so its
# warnings stay off.
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.15
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake)
FetchContent_MakeAvailable(cgltf)
file(WRITE "${CMAKE_BINARY_DIR}/cgltf_impl.c" "#define CGLTF_IMPLEMENTATION\n#include <cgltf.h>\n")
add_library(cgltf STATIC "${CMAKE_BINARY_DIR}/cgltf_impl.c")
target_include_directories(cgltf SYSTEM PUBLIC ${cgltf_SOURCE_DIR})
target_compile_options(cgltf PRIVATE ${NV_NO_WARNINGS})

# ozz-animation: runtime and the offline builders we use to build skeletons and clips at load time.
set(ozz_build_tools    OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx      OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf     OFF CACHE BOOL "" FORCE)
set(ozz_build_data     OFF CACHE BOOL "" FORCE)
set(ozz_build_samples  OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos   OFF CACHE BOOL "" FORCE)
set(ozz_build_tests    OFF CACHE BOOL "" FORCE)
set(ozz_build_postfix  OFF CACHE BOOL "" FORCE)
set(ozz_build_msvc_rt_dll ON CACHE BOOL "" FORCE) # the same C runtime as ours (MSVC): the dynamic one
FetchContent_Declare(ozz
    GIT_REPOSITORY https://github.com/guillaumeblanc/ozz-animation.git
    GIT_TAG        0.17.0
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL # only the libraries nv links get built
    SYSTEM)          # its headers' warnings stay out of our builds
FetchContent_MakeAvailable(ozz)
foreach(ozz_target ozz_base ozz_animation ozz_animation_offline)
    target_compile_options(${ozz_target} PRIVATE ${NV_NO_WARNINGS})
endforeach()

# Slang and Tint for the shaders (docs/specs/slang_shader_port.md), and nv_add_shader. On Windows also DXC and the Agility SDK.
include(cmake/shaders.cmake)

# Our code on MSVC (docs/specs/d3d12_port.md, "Build"): /std:c17 and /std:c++17 come from CMAKE_C_STANDARD above. /utf-8 reads
# the sources as UTF-8 (the Korean strings), /Zc:preprocessor is the standard preprocessor, /W4 has to show no warning.
# What is set only for the warnings that standard C17 draws on MSVC alone says why:
#   _CRT_SECURE_NO_WARNINGS: C4996 asks for the *_s functions for fopen, strncpy and the like, which are not standard C.
# UNICODE and _UNICODE make resource names such as IDC_ARROW wide, as the W functions the platform code calls take them.
if(NV_WINDOWS)
    add_compile_options(/W4 /utf-8 /Zc:preprocessor)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
endif()

enable_testing()
add_subdirectory(engine)
add_subdirectory(sandbox)
add_subdirectory(autobattler)
add_subdirectory(tests)
