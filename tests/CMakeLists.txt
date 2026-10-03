# Tests that need no browser. Emscripten builds them for Node, which ctest runs them with.
add_executable(chunk_test chunk_test.c ${PROJECT_SOURCE_DIR}/engine/chunk.c)
target_include_directories(chunk_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_options(chunk_test PRIVATE -sENVIRONMENT=node)
add_test(NAME chunk_test COMMAND chunk_test)

add_executable(log_test log_test.c ${PROJECT_SOURCE_DIR}/engine/log.c)
target_include_directories(log_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_options(log_test PRIVATE -sENVIRONMENT=node)
add_test(NAME log_test COMMAND log_test)

# Only engine/gpu.h's inline helpers: the WebGPU header comes from the port, nothing is linked from it.
add_executable(gpu_format_test gpu_format_test.c)
target_include_directories(gpu_format_test PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_options(gpu_format_test PRIVATE --use-port=emdawnwebgpu)
target_link_options(gpu_format_test PRIVATE -sENVIRONMENT=node)
add_test(NAME gpu_format_test COMMAND gpu_format_test)

# The Korean UI strings: every wrapped English text has a row, and rows keep their printf conversions.
add_test(NAME strings_test COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} ${PROJECT_SOURCE_DIR}/tests/strings_test.mjs app autobattler)

# nv_utf8_fit and nv_utf8_trim: text cut to fit stays valid UTF-8.
add_executable(utf8_test utf8_test.c)
target_include_directories(utf8_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_options(utf8_test PRIVATE -sENVIRONMENT=node)
add_test(NAME utf8_test COMMAND utf8_test)

# The effects system's CPU side: splitting bursts into workgroup jobs, the ring buffers' slots.
add_executable(vfx_test vfx_test.c ${PROJECT_SOURCE_DIR}/engine/vfx_cpu.c)
target_include_directories(vfx_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_options(vfx_test PRIVATE -sENVIRONMENT=node)
add_test(NAME vfx_test COMMAND vfx_test)

# The small helpers in engine/base.h and engine/math.h: FNV-1a, clamps, PCG32, the pixel ratio.
add_executable(base_test base_test.c)
target_include_directories(base_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_options(base_test PRIVATE -sENVIRONMENT=node)
add_test(NAME base_test COMMAND base_test)

# The mesh primitives in engine/mesh.h: no GPU, so nothing from the WebGPU port is needed.
add_executable(mesh_test mesh_test.c ${PROJECT_SOURCE_DIR}/engine/mesh.c)
target_include_directories(mesh_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_options(mesh_test PRIVATE -sENVIRONMENT=node)
add_test(NAME mesh_test COMMAND mesh_test)

# The scene's resolution and the rays through its image (engine/renderer_cpu.c): no GPU is used, but renderer.h includes the
# WebGPU header, which the port provides (as for gpu_format_test); nothing is linked from it.
add_executable(resolution_test resolution_test.c ${PROJECT_SOURCE_DIR}/engine/renderer_cpu.c ${PROJECT_SOURCE_DIR}/engine/scene.c)
target_include_directories(resolution_test PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_options(resolution_test PRIVATE --use-port=emdawnwebgpu)
target_link_options(resolution_test PRIVATE -sENVIRONMENT=node)
add_test(NAME resolution_test COMMAND resolution_test)

# The orbit camera in engine/camera.h: no GPU, so nothing from the WebGPU port is needed.
add_executable(camera_test camera_test.c ${PROJECT_SOURCE_DIR}/engine/camera.c)
target_include_directories(camera_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_options(camera_test PRIVATE -sENVIRONMENT=node)
add_test(NAME camera_test COMMAND camera_test)

# The auto-battler's rules and definition files (autobattler/battle.c, defs.c): no GPU. NODERAWFS reads the repository's
# autobattler/data directly.
add_executable(battle_test battle_test.c ${PROJECT_SOURCE_DIR}/autobattler/battle.c ${PROJECT_SOURCE_DIR}/autobattler/defs.c
    ${PROJECT_SOURCE_DIR}/engine/log.c)
target_include_directories(battle_test PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(battle_test PRIVATE BATTLE_DATA_DIR="${PROJECT_SOURCE_DIR}/autobattler/data")
target_link_options(battle_test PRIVATE -sENVIRONMENT=node -sNODERAWFS=1)
add_test(NAME battle_test COMMAND battle_test)
