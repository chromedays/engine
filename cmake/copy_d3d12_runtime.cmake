# Run after a Windows executable is linked (nv_setup_executable): puts the Agility SDK's D3D12 runtime in D3D12\ beside it.
#   cmake -DCONFIG=<Debug|Release|...> -DBIN=<the SDK's bin\x64 folder> -DDEST=<the executable's D3D12 folder> -P copy_d3d12_runtime.cmake
# A script rather than command-line arguments because the debug layer is copied in Debug only, and a generator expression cannot
# leave a whole command out of a build that has all the configurations in one project (the Visual Studio generator).
file(MAKE_DIRECTORY "${DEST}")
file(COPY_FILE "${BIN}/D3D12Core.dll" "${DEST}/D3D12Core.dll" ONLY_IF_DIFFERENT)
if(CONFIG STREQUAL "Debug")
    # The debug layer has to be the runtime's own version.
    file(COPY_FILE "${BIN}/d3d12SDKLayers.dll" "${DEST}/d3d12SDKLayers.dll" ONLY_IF_DIFFERENT)
endif()
