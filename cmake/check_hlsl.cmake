# Compiles a Slang source to HLSL, as D3D12 will need it, and keeps the text beside the other outputs; fails when it does
# not compile. slangc writes a source with several entry points to its standard output, which add_custom_command cannot
# redirect: cmake -DSLANGC=<slangc> -DSOURCE=<.slang> -DINCLUDE=<dir> -DOUTPUT=<file> -P check_hlsl.cmake
execute_process(
    COMMAND "${SLANGC}" "${SOURCE}" -I "${INCLUDE}" -target hlsl -matrix-layout-column-major -warnings-as-errors all
    OUTPUT_FILE "${OUTPUT}"
    RESULT_VARIABLE result)
if(result)
    message(FATAL_ERROR "${SOURCE} does not compile to HLSL (${result})")
endif()
