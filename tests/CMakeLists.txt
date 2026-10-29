# Tests that need no browser. Emscripten builds them for Node, which ctest runs them with; on Windows they are plain executables.
#   nv_add_test(<name> <sources...>): the executable, run by ctest. On the web `-sENVIRONMENT=node` links it for Node.
function(nv_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR})
    if(NV_WEB)
        target_link_options(${name} PRIVATE -sENVIRONMENT=node)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# A test that reads files from the repository: Node reads them from the disk with NODERAWFS, a native executable just opens them.
function(nv_add_file_test name)
    nv_add_test(${name} ${ARGN})
    if(NV_WEB)
        target_link_options(${name} PRIVATE -sNODERAWFS=1)
    endif()
endfunction()

nv_add_test(chunk_test chunk_test.c ${PROJECT_SOURCE_DIR}/engine/chunk.c)

# The log itself writes to the browser console through EM_JS, which this test replaces to count and compare what arrives.
if(NV_WEB)
    nv_add_test(log_test log_test.c ${PROJECT_SOURCE_DIR}/engine/log.c)
endif()

# Only engine/gpu.h's inline helpers: the WebGPU header comes from the port, nothing is linked from it.
nv_add_test(gpu_format_test gpu_format_test.c)
if(NV_WEB)
    target_compile_options(gpu_format_test PRIVATE --use-port=emdawnwebgpu)
endif()

# The Korean UI strings: every wrapped English text has a row, and rows keep their printf conversions. The test is a Node script:
# a build with no Node skips it (the web build's CI runs it).
if(NV_WEB)
    add_test(NAME strings_test COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} ${PROJECT_SOURCE_DIR}/tests/strings_test.mjs sandbox)
else()
    find_program(NV_NODE node)
    if(NV_NODE)
        add_test(NAME strings_test COMMAND ${NV_NODE} ${PROJECT_SOURCE_DIR}/tests/strings_test.mjs sandbox)
    else()
        message(WARNING "No Node: strings_test and shader_layout_test are skipped (the web build's CI runs them)")
    endif()
endif()

# nv_utf8_fit and nv_utf8_trim: text cut to fit stays valid UTF-8.
nv_add_test(utf8_test utf8_test.c)

# The effects system's CPU side: splitting bursts into workgroup jobs, the ring buffers' slots.
nv_add_test(vfx_test vfx_test.c ${PROJECT_SOURCE_DIR}/engine/vfx_cpu.c)

# The small helpers in engine/base.h and engine/math.h: FNV-1a, clamps, PCG32, the pixel ratio.
nv_add_test(base_test base_test.c)

# The mesh primitives in engine/mesh.h: no GPU, so nothing from the WebGPU port is needed.
nv_add_test(mesh_test mesh_test.c ${PROJECT_SOURCE_DIR}/engine/mesh.c)

# The scene's resolution and the rays through its image (engine/renderer_cpu.c): no GPU is used, but renderer.h includes the
# WebGPU header, which the port provides (as for gpu_format_test); nothing is linked from it.
nv_add_test(resolution_test resolution_test.c ${PROJECT_SOURCE_DIR}/engine/renderer_cpu.c ${PROJECT_SOURCE_DIR}/engine/scene.c)
if(NV_WEB)
    target_compile_options(resolution_test PRIVATE --use-port=emdawnwebgpu)
endif()

# The orbit camera in engine/camera.h: no GPU, so nothing from the WebGPU port is needed.
nv_add_test(camera_test camera_test.c ${PROJECT_SOURCE_DIR}/engine/camera.c)

# The line-based text reading in engine/text.c: lines, tokens, numbers, names and error reports.
nv_add_test(text_test text_test.c ${PROJECT_SOURCE_DIR}/engine/text.c ${PROJECT_SOURCE_DIR}/engine/log.c)

# The scene file reader (sandbox/scene_file.c): no GPU. It reads the sandbox's own .nvscene files.
nv_add_file_test(scene_file_test scene_file_test.c ${PROJECT_SOURCE_DIR}/sandbox/scene_file.c ${PROJECT_SOURCE_DIR}/engine/text.c
    ${PROJECT_SOURCE_DIR}/engine/log.c)
target_compile_definitions(scene_file_test PRIVATE SCENE_DATA_DIR="${PROJECT_SOURCE_DIR}/sandbox/data")

# Reading a whole file into an arena (engine/file.c). It reads this test's own source.
nv_add_file_test(file_test file_test.c ${PROJECT_SOURCE_DIR}/engine/file.c)
target_compile_definitions(file_test PRIVATE FILE_TEST_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/file_test.c")

# The shaders' reflection (slangc's, written when they are built) against the layouts the C code relies on. On Windows the
# reflection is of the HLSL (cmake/make_dxil.cmake), whose numbers are D3D12's registers rather than WebGPU's bindings.
if(NV_WEB)
    add_test(NAME shader_layout_test COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} ${PROJECT_SOURCE_DIR}/tests/shader_layout_test.mjs ${PROJECT_BINARY_DIR}/shaders)
endif()

# Files on the user's computer on Windows (engine/local_file_win32.c). A Debug build takes the path the dialogs would give from
# NV_LOCAL_FILE, which the test sets; a Release build would open a dialog, so ctest runs the test in Debug only (-C Debug).
if(NV_WINDOWS)
    add_executable(local_file_test local_file_test.c ${PROJECT_SOURCE_DIR}/engine/local_file_win32.c ${PROJECT_SOURCE_DIR}/engine/file.c)
    target_include_directories(local_file_test PRIVATE ${PROJECT_SOURCE_DIR})
    add_test(NAME local_file_test COMMAND local_file_test CONFIGURATIONS Debug)
    target_compile_definitions(local_file_test PRIVATE LOCAL_FILE_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(local_file_test PRIVATE comdlg32)
endif()
