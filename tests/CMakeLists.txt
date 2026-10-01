# Tests that need no browser. Emscripten builds them for Node, which ctest runs them with.
add_executable(chunk_test chunk_test.c ${PROJECT_SOURCE_DIR}/engine/src/chunk.c)
target_include_directories(chunk_test PRIVATE ${PROJECT_SOURCE_DIR}/engine/include)
target_link_options(chunk_test PRIVATE -sENVIRONMENT=node)
add_test(NAME chunk_test COMMAND chunk_test)

add_executable(log_test log_test.c ${PROJECT_SOURCE_DIR}/engine/src/log.c)
target_include_directories(log_test PRIVATE ${PROJECT_SOURCE_DIR}/engine/include)
target_link_options(log_test PRIVATE -sENVIRONMENT=node)
add_test(NAME log_test COMMAND log_test)

# Only nv/gpu.h's inline helpers: the WebGPU header comes from the port, nothing is linked from it.
add_executable(gpu_format_test gpu_format_test.c)
target_include_directories(gpu_format_test PRIVATE ${PROJECT_SOURCE_DIR}/engine/include)
target_compile_options(gpu_format_test PRIVATE --use-port=emdawnwebgpu)
target_link_options(gpu_format_test PRIVATE -sENVIRONMENT=node)
add_test(NAME gpu_format_test COMMAND gpu_format_test)

# The Korean UI strings: every wrapped English text has a row, and rows keep their printf conversions.
add_test(NAME strings_test COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} ${PROJECT_SOURCE_DIR}/tests/strings_test.mjs)
