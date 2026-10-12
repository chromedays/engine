# Shader tools and nv_add_shader (docs/specs/slang_shader_port.md). Both tools are prebuilt release binaries, pinned by
# version and SHA-256, run only while building and linked into nothing.
#   slangc: Slang's compiler, writes the WGSL (and checks the source compiles to HLSL).
#   tint:   Dawn's WGSL compiler, the one Chrome and Edge use, checks the WGSL slangc wrote (slangc does not).
# NV_SLANGC and NV_TINT point at binaries of your own instead of downloading.
include(FetchContent)

set(NV_SLANG_VERSION 2026.19)
set(NV_DAWN_TAG v20261009.190412)
set(NV_DAWN_COMMIT c943ea32b614ff53e4fe410fc8f4968b6b8fe46d)

# The host that builds, not the one the code is for.
string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" nv_host_os)
string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" nv_host_cpu)
if(nv_host_cpu MATCHES "^(x86_64|amd64)$")
    set(nv_host_cpu x86_64)
elseif(nv_host_cpu MATCHES "^(aarch64|arm64)$")
    set(nv_host_cpu aarch64)
endif()
set(nv_host "${nv_host_os}-${nv_host_cpu}")

# Where the packages are unpacked: a directory that outlives a build directory, so the CI caches it (build.yml) and the Release
# and Debug builds share one download.
set(NV_TOOLS_DIR "${CMAKE_BINARY_DIR}/tools" CACHE PATH "Where the Slang and Dawn packages are unpacked")

set(NV_SLANGC "" CACHE FILEPATH "A slangc to use instead of downloading Slang ${NV_SLANG_VERSION}")
set(NV_TINT "" CACHE FILEPATH "A tint to use instead of the one in Dawn ${NV_DAWN_TAG}")

if(NOT NV_SLANGC)
    set(slang_hash_linux-x86_64 bd6cfc47b7353b2cffa36a866b1584749f76f4b2dbebf6dda26bc3fb40dc3c0e)
    set(slang_hash_linux-aarch64 eeefa4e52ca322cf3c037ee0d273b0ee68f4161fa422eda1bb704126073b69b4)
    set(slang_hash_windows-x86_64 fa7629c9ce59c87c821614ae4b52e52f785d4c659a5a06a89fa9ec86d615eeee)
    set(slang_hash_windows-aarch64 e47c327262a56e99a9ae07c31c3bab5ea5a531ffd3b75b5d5746b24b63a6e751)
    set(slang_hash_darwin-aarch64 fa46822c92d81404a7951de3746093985c1e6f520f4b533e2aa3bea576f0f2f0)
    set(slang_name_darwin-aarch64 macos-aarch64)
    if(NOT DEFINED slang_hash_${nv_host})
        message(FATAL_ERROR "No Slang ${NV_SLANG_VERSION} package is pinned for ${nv_host}. Set NV_SLANGC to a slangc.")
    endif()
    set(slang_name ${nv_host})
    if(DEFINED slang_name_${nv_host})
        set(slang_name ${slang_name_${nv_host}})
    endif()
    set(slang_root "${NV_TOOLS_DIR}/slang-${NV_SLANG_VERSION}-${slang_name}")
    set(nv_slangc "${slang_root}/bin/slangc${CMAKE_HOST_EXECUTABLE_SUFFIX}")
    if(NOT EXISTS "${nv_slangc}")
        FetchContent_Declare(slang
            URL "https://github.com/shader-slang/slang/releases/download/v${NV_SLANG_VERSION}/slang-${NV_SLANG_VERSION}-${slang_name}.tar.gz"
            URL_HASH SHA256=${slang_hash_${nv_host}}
            SOURCE_DIR "${slang_root}"
            SOURCE_SUBDIR no-cmake)
        FetchContent_MakeAvailable(slang)
    endif()
else()
    set(nv_slangc "${NV_SLANGC}")
    execute_process(COMMAND "${nv_slangc}" -v OUTPUT_VARIABLE slang_found ERROR_VARIABLE slang_found OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT slang_found MATCHES "${NV_SLANG_VERSION}")
        message(WARNING "NV_SLANGC is Slang '${slang_found}', not the pinned ${NV_SLANG_VERSION}")
    endif()
endif()

# Dawn has no packages for the arm64 hosts: the check is skipped there and the browser run catches what it would have.
set(nv_tint "")
if(NV_TINT)
    set(nv_tint "${NV_TINT}")
else()
    set(dawn_hash_linux-x86_64 8530b52e5ef5abeb788ab5c3552b261882d9b4d400d7c85c9dc45255b74d48ea)
    set(dawn_name_linux-x86_64 ubuntu-latest)
    set(dawn_hash_darwin-aarch64 6d1584b308a332a547a08d694bbec78615bbde686de8a7ed24f2c7d2e8eb1114)
    set(dawn_name_darwin-aarch64 macos-latest)
    set(dawn_hash_darwin-x86_64 4ebbc32d8e0eaf8093ac17e050e15533aacede30d47049589ae475bfe0e0dcb4)
    set(dawn_name_darwin-x86_64 macos-15-intel)
    set(dawn_hash_windows-x86_64 ba97fa93b5e1c265b8a4411698169b40e7696e7e8d574482c94efce6e3ff4228)
    set(dawn_name_windows-x86_64 windows-latest)
    if(DEFINED dawn_hash_${nv_host})
        set(dawn_root "${NV_TOOLS_DIR}/dawn-${NV_DAWN_TAG}-${dawn_name_${nv_host}}")
        set(nv_tint "${dawn_root}/bin/tint${CMAKE_HOST_EXECUTABLE_SUFFIX}")
        if(NOT EXISTS "${nv_tint}")
            FetchContent_Declare(dawn
                URL "https://github.com/google/dawn/releases/download/${NV_DAWN_TAG}/Dawn-${NV_DAWN_COMMIT}-${dawn_name_${nv_host}}-Release.tar.gz"
                URL_HASH SHA256=${dawn_hash_${nv_host}}
                SOURCE_DIR "${dawn_root}"
                SOURCE_SUBDIR no-cmake)
            FetchContent_MakeAvailable(dawn)
        endif()
    else()
        message(WARNING "No Dawn package for ${nv_host}: the generated WGSL is not checked with tint at build time")
    endif()
endif()

set(NV_SHADER_DIR "${PROJECT_SOURCE_DIR}/engine/shaders")

# Builds <name> from engine/shaders/<source>: the WGSL (<build>/shaders/<name>.wgsl, kept: the browser's and tint's errors
# name its lines), its check, the reflection (<name>.json, read by tests/shader_layout_test) and the C header
# <build>/shaders/<name>_shader.h holding the WGSL as `const char <name>_wgsl[]` and the shader as engine/gpu.h takes it,
# `const NvGpuShaderCode <name>_shader`. Add the returned header to a target's sources:
#   nv_add_shader(<name> <source.slang> <output header variable>)
function(nv_add_shader name source out_header)
    set(dir "${CMAKE_BINARY_DIR}/shaders")
    set(wgsl "${dir}/${name}.wgsl")
    set(header "${dir}/${name}_shader.h")
    set(checked "${dir}/${name}.tint.wgsl")
    set(commands
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
        # 1. Slang to WGSL, and the reflection of the layouts.
        COMMAND "${nv_slangc}" "${NV_SHADER_DIR}/${source}" -I "${NV_SHADER_DIR}" -target wgsl -matrix-layout-column-major
                -warnings-as-errors all -depfile "${wgsl}.d" -reflection-json "${dir}/${name}.json" -o "${wgsl}"
        # The same source has to compile to HLSL too (the output is not used).
        COMMAND ${CMAKE_COMMAND} -DSLANGC=${nv_slangc} -DSOURCE=${NV_SHADER_DIR}/${source} -DINCLUDE=${NV_SHADER_DIR}
                -DOUTPUT=${dir}/${name}.hlsl.txt -P "${PROJECT_SOURCE_DIR}/cmake/check_hlsl.cmake")
    if(nv_tint)
        # 2. tint reads the WGSL as the browser will; its output only marks that the check passed.
        list(APPEND commands COMMAND "${nv_tint}" --format=wgsl -o "${checked}" "${wgsl}")
    endif()
    # 3. The header, from the checked WGSL only.
    list(APPEND commands COMMAND ${CMAKE_COMMAND} -DINPUT=${wgsl} -DOUTPUT=${header} -DNAME=${name}_wgsl -DSHADER=${name}_shader -DFIELD=wgsl
                -P "${PROJECT_SOURCE_DIR}/cmake/embed_text.cmake")
    add_custom_command(OUTPUT "${wgsl}" "${header}" "${dir}/${name}.json"
        ${commands}
        DEPENDS "${NV_SHADER_DIR}/${source}" "${PROJECT_SOURCE_DIR}/cmake/embed_text.cmake" "${PROJECT_SOURCE_DIR}/cmake/check_hlsl.cmake"
        DEPFILE "${wgsl}.d"
        COMMENT "Shader ${name}"
        VERBATIM)
    set(${out_header} "${header}" PARENT_SCOPE)
endfunction()
