include(FetchContent)

# ---------------------------------------------------------------------------
# GLFW (windowing / input)
# ---------------------------------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glfw)

# ---------------------------------------------------------------------------
# wgpu-native (prebuilt WebGPU implementation exposing the standard webgpu.h)
# ---------------------------------------------------------------------------
set(WGPU_NATIVE_VERSION "v29.0.1.1" CACHE STRING "wgpu-native release tag")

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _arch)
if(_arch MATCHES "^(arm64|aarch64)$")
    set(_wgpu_arch "aarch64")
elseif(_arch MATCHES "^(x86_64|amd64)$")
    set(_wgpu_arch "x86_64")
else()
    message(FATAL_ERROR "Unsupported CPU architecture for wgpu-native: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(WIN32)
    set(_wgpu_os "windows")
    set(_wgpu_suffix "-msvc")
elseif(APPLE)
    set(_wgpu_os "macos")
    set(_wgpu_suffix "")
elseif(UNIX)
    set(_wgpu_os "linux")
    set(_wgpu_suffix "")
else()
    message(FATAL_ERROR "Unsupported OS for wgpu-native")
endif()

set(_wgpu_url "https://github.com/gfx-rs/wgpu-native/releases/download/${WGPU_NATIVE_VERSION}/wgpu-${_wgpu_os}-${_wgpu_arch}${_wgpu_suffix}-release.zip")
message(STATUS "wgpu-native: ${_wgpu_url}")

FetchContent_Declare(wgpu_native
    URL ${_wgpu_url}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(wgpu_native)

add_library(wgpu_native SHARED IMPORTED GLOBAL)
set_target_properties(wgpu_native PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${wgpu_native_SOURCE_DIR}/include")

if(WIN32)
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "${wgpu_native_SOURCE_DIR}/lib/wgpu_native.dll"
        IMPORTED_IMPLIB   "${wgpu_native_SOURCE_DIR}/lib/wgpu_native.dll.lib")
elseif(APPLE)
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "${wgpu_native_SOURCE_DIR}/lib/libwgpu_native.dylib"
        IMPORTED_NO_SONAME TRUE)
else()
    # The prebuilt .so has no SONAME; link it by name so the binary finds it via RPATH.
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "${wgpu_native_SOURCE_DIR}/lib/libwgpu_native.so"
        IMPORTED_NO_SONAME TRUE)
endif()

# Copies the wgpu-native shared library next to an executable and sets up RPATH.
function(engine_setup_executable target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:wgpu_native> $<TARGET_FILE_DIR:${target}>)
    if(APPLE)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "@executable_path")
    elseif(UNIX)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN")
    endif()
endfunction()
