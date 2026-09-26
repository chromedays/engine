cmake_minimum_required(VERSION 3.24)
project(engine LANGUAGES C)

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

add_subdirectory(engine)
add_subdirectory(examples/triangle)
add_subdirectory(examples/scene)
