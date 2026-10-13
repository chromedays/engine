# Builds one shader for the Windows build (docs/specs/d3d12_port.md, "Shader bindings"): the DXIL of each of its entry points
# (shader model 6.6, signed by DXC) and the C header that holds them as `static const NvGpuShaderCode <NAME>_shader` (engine/gpu.h).
#   cmake -DSLANGC=<slangc> -DDXC=<dxc.exe> -DSOURCE=<.slang> -DINCLUDE=<dir> -DDIR=<output dir> -DNAME=<name> -P make_dxil.cmake
# Writes <DIR>/<NAME>.json (the reflection), <DIR>/<NAME>.hlsl.txt (the HLSL, kept for reading: slangc writes a source with
# several entry points to its standard output, one program after another), <DIR>/<NAME>.<entry>.dxil, <DIR>/<NAME>.d (the
# files the source imports, for Ninja) and <DIR>/<NAME>_shader.h.
get_filename_component(dxc_dir "${DXC}" DIRECTORY)
set(header "${DIR}/${NAME}_shader.h")
set(common -I "${INCLUDE}" -DNV_D3D12 -matrix-layout-column-major -warnings-as-errors all)

# 1. The reflection, which also lists the entry points; the HLSL has to compile as well.
execute_process(
    COMMAND "${SLANGC}" "${SOURCE}" ${common} -target hlsl -reflection-json "${DIR}/${NAME}.json"
    OUTPUT_FILE "${DIR}/${NAME}.hlsl.txt"
    RESULT_VARIABLE result)
if(result)
    message(FATAL_ERROR "${SOURCE} does not compile to HLSL (${result})")
endif()
file(READ "${DIR}/${NAME}.json" reflection)
string(JSON entry_count LENGTH "${reflection}" entryPoints)
if(entry_count EQUAL 0)
    message(FATAL_ERROR "${SOURCE} has no entry points")
endif()

# 2. DXIL for each entry point. The first also writes what the source imports.
math(EXPR last "${entry_count} - 1")
set(arrays "")
set(entries "")
foreach(i RANGE ${last})
    string(JSON entry GET "${reflection}" entryPoints ${i} name)
    set(dxil "${DIR}/${NAME}.${entry}.dxil")
    set(depfile_option "")
    if(i EQUAL 0)
        set(depfile_option -depfile "${DIR}/${NAME}.slang.d")
    endif()
    execute_process(
        COMMAND "${SLANGC}" "${SOURCE}" ${common} -target dxil -profile sm_6_6 -dxc-path "${dxc_dir}" -entry ${entry}
                ${depfile_option} -o "${dxil}"
        RESULT_VARIABLE result)
    if(result)
        message(FATAL_ERROR "${SOURCE}: entry point ${entry} does not compile to DXIL (${result})")
    endif()

    # 3. The bytes as a C array, sixteen to a line.
    file(READ "${dxil}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
    string(REPEAT "0x[0-9a-f][0-9a-f]," 16 sixteen)
    string(REGEX REPLACE "(${sixteen})" "\\1\n    " hex "${hex}")
    string(APPEND arrays "static const unsigned char ${NAME}_${entry}_dxil[] = {\n    ${hex}\n};\n")
    string(APPEND entries "    {\"${entry}\", ${NAME}_${entry}_dxil, sizeof(${NAME}_${entry}_dxil)},\n")
endforeach()

file(WRITE "${header}.tmp"
    "// Generated from ${SOURCE} by cmake/make_dxil.cmake. Do not edit.\n#pragma once\n#include <engine/gpu.h>\n"
    "${arrays}static const NvGpuShaderEntry ${NAME}_entries[] = {\n${entries}};\n"
    "static const NvGpuShaderCode ${NAME}_shader = {.entries = ${NAME}_entries, .entry_count = ${entry_count}};\n")
file(COPY_FILE "${header}.tmp" "${header}" ONLY_IF_DIFFERENT)
file(REMOVE "${header}.tmp")

# 4. slangc's depfile is in Makefile syntax with escaped drive colons and backslashes; Ninja wants plain paths.
file(READ "${DIR}/${NAME}.slang.d" slang_depfile)
string(REGEX REPLACE "^.*\\.dxil: *" "" slang_depfile "${slang_depfile}") # drop `<output>.dxil: `
string(REPLACE "\\:" ":" slang_depfile "${slang_depfile}")
string(REPLACE "\\\\" "/" slang_depfile "${slang_depfile}")
string(STRIP "${slang_depfile}" slang_depfile)
file(WRITE "${DIR}/${NAME}.d" "${header}: ${slang_depfile}\n")
