cmake_minimum_required(VERSION 3.30) # ozz-animation 0.17 needs 3.30
project(engine LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "nv builds for the web only. Configure with emcmake: "
        "emcmake cmake -S . -B build -G Ninja")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17) # ozz-animation and our wrapper around it
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra)

# WebGPU comes from the browser through Emscripten's emdawnwebgpu port.
add_library(webgpu INTERFACE)
target_compile_options(webgpu INTERFACE --use-port=emdawnwebgpu)
target_link_options(webgpu INTERFACE
    --use-port=emdawnwebgpu
    -sDEFAULT_TO_CXX   # the port's implementation is C++
    -sASYNCIFY         # lets wgpuInstanceWaitAny yield to the browser during setup
    -sASYNCIFY_IGNORE_INDIRECT # setup reaches WaitAny only through direct calls
    -sALLOW_MEMORY_GROWTH
    -sENVIRONMENT=web)

# Emits <target>.js, .wasm and .manifest.json plus index.html (from web/index.html.in) into the
# target's directory, and installs them into <prefix>/<target>/ as the "web" install component.
#   nv_setup_executable(<target> [ROOT] [ASSETS <dir>])
# ROOT installs into <prefix>/ instead, so the page is the package's index.html.
# ASSETS preloads <dir> as /assets, readable with fopen; it ships as <target>.data.
# Debug builds keep their DWARF debug info in <target>.debug.wasm next to the page. The page does
# not download it; browser developer tools fetch it for source-level debugging.
function(nv_setup_executable target)
    cmake_parse_arguments(ARG "ROOT" "ASSETS" "" ${ARGN})
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
    # The page reads file sizes from the manifest to show download progress.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DDIR=$<TARGET_FILE_DIR:${target}> -DTARGET=${target}
                -P "${PROJECT_SOURCE_DIR}/web/manifest.cmake"
        VERBATIM)

    if(ARG_ASSETS)
        # CONFIGURE_DEPENDS: an asset added or removed re-runs the glob, so an old build directory does not stop on a
        # file that is gone.
        file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS "${ARG_ASSETS}/*")
        # A Git LFS pointer instead of the real file means the clone skipped LFS.
        foreach(asset ${asset_files})
            file(READ "${asset}" head LIMIT 40)
            if(head MATCHES "^version https://git-lfs")
                message(FATAL_ERROR "${asset} is a Git LFS pointer. Run: git lfs install && git lfs pull")
            endif()
        endforeach()
        target_link_options(${target} PRIVATE "--preload-file=${ARG_ASSETS}@/assets")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${asset_files})
        list(APPEND files "$<TARGET_FILE_DIR:${target}>/${target}.data")
    endif()

    set(destination ${target})
    if(ARG_ROOT)
        set(destination .)
    endif()
    install(FILES ${files} DESTINATION ${destination} COMPONENT web)
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
target_compile_options(cimgui PRIVATE -w)

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
target_compile_options(cgltf PRIVATE -w)

# ozz-animation: runtime and the offline builders we use to build skeletons and clips at load time.
set(ozz_build_tools    OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx      OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf     OFF CACHE BOOL "" FORCE)
set(ozz_build_data     OFF CACHE BOOL "" FORCE)
set(ozz_build_samples  OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos   OFF CACHE BOOL "" FORCE)
set(ozz_build_tests    OFF CACHE BOOL "" FORCE)
set(ozz_build_postfix  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ozz
    GIT_REPOSITORY https://github.com/guillaumeblanc/ozz-animation.git
    GIT_TAG        0.17.0
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL # only the libraries nv links get built
    SYSTEM)          # its headers' warnings stay out of our builds
FetchContent_MakeAvailable(ozz)
foreach(ozz_target ozz_base ozz_animation ozz_animation_offline)
    target_compile_options(${ozz_target} PRIVATE -w)
endforeach()

enable_testing()
add_subdirectory(engine)
add_subdirectory(app)
add_subdirectory(tests)
