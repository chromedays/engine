# odiff for the visual regression test (docs/specs/visual_regression.md): a prebuilt release binary, pinned by version
# and SHA-256 like slangc and tint (cmake/shaders.cmake), run only by tools/visual/compare.mjs and linked into nothing.
# The release assets are single binaries, not archives, so this is a plain download, not a FetchContent package.
#   NV_ODIFF: an odiff to use instead of downloading odiff ${NV_ODIFF_VERSION}
# Sets `nv_odiff` and also copies the binary to <build>/tools/odiff, the stable path the justfile's recipes use.

set(NV_ODIFF_VERSION 4.5.0)
set(NV_ODIFF "" CACHE FILEPATH "An odiff to use instead of downloading odiff ${NV_ODIFF_VERSION}")

if(NV_ODIFF)
    set(nv_odiff "${NV_ODIFF}")
else()
    set(odiff_asset_linux-x86_64 odiff-linux-x64)
    set(odiff_hash_linux-x86_64 2eb5a7f44df925d93b451898de8bf213f0aac270a9781187746097ed7cd45324)
    set(odiff_asset_linux-aarch64 odiff-linux-arm64)
    set(odiff_hash_linux-aarch64 245ad45851da200cfdd361374618fca22781919faaca255aed1c93b883beb476)
    if(NOT DEFINED odiff_hash_${nv_host})
        message(FATAL_ERROR "No odiff ${NV_ODIFF_VERSION} binary is pinned for ${nv_host}. Set NV_ODIFF to an odiff.")
    endif()
    set(odiff_root "${NV_TOOLS_DIR}/odiff-${NV_ODIFF_VERSION}")
    set(nv_odiff "${odiff_root}/odiff")
    if(NOT EXISTS "${nv_odiff}")
        file(MAKE_DIRECTORY "${odiff_root}")
        file(DOWNLOAD
            "https://github.com/dmtrKovalenko/odiff/releases/download/v${NV_ODIFF_VERSION}/${odiff_asset_${nv_host}}"
            "${nv_odiff}"
            EXPECTED_HASH SHA256=${odiff_hash_${nv_host}}
            TLS_VERIFY ON
            STATUS odiff_download_status)
        list(GET odiff_download_status 0 odiff_download_code)
        if(NOT odiff_download_code EQUAL 0)
            list(GET odiff_download_status 1 odiff_download_message)
            file(REMOVE "${nv_odiff}")
            message(FATAL_ERROR "Downloading odiff ${NV_ODIFF_VERSION} failed: ${odiff_download_message}")
        endif()
        file(CHMOD "${nv_odiff}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
endif()

configure_file("${nv_odiff}" "${CMAKE_BINARY_DIR}/tools/odiff" COPYONLY)
file(CHMOD "${CMAKE_BINARY_DIR}/tools/odiff" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
