cmake_minimum_required(VERSION 3.24)
project(engine LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "nv builds for the web only. Configure with emcmake: "
        "emcmake cmake -S . -B build -G Ninja")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
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

# Emits <target>.js/.wasm plus index.html (from web/index.html.in) into the target's directory,
# and installs all three into <prefix>/<target>/.
function(nv_setup_executable target)
    set_target_properties(${target} PROPERTIES SUFFIX ".js")
    set(NV_WEB_SCRIPT "${target}.js")
    configure_file("${PROJECT_SOURCE_DIR}/web/index.html.in"
        "${CMAKE_CURRENT_BINARY_DIR}/index.html" @ONLY)
    install(FILES
        "$<TARGET_FILE_DIR:${target}>/${target}.js"
        "$<TARGET_FILE_DIR:${target}>/${target}.wasm"
        "${CMAKE_CURRENT_BINARY_DIR}/index.html"
        DESTINATION ${target})
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

add_subdirectory(engine)
add_subdirectory(examples/triangle)
add_subdirectory(examples/scene)
