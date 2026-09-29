# Tests that need no browser. Emscripten builds them for Node, which ctest runs them with.
add_executable(chunk_test chunk_test.c ${PROJECT_SOURCE_DIR}/engine/src/chunk.c)
target_include_directories(chunk_test PRIVATE ${PROJECT_SOURCE_DIR}/engine/include)
target_link_options(chunk_test PRIVATE -sENVIRONMENT=node)
add_test(NAME chunk_test COMMAND chunk_test)

add_executable(log_test log_test.c ${PROJECT_SOURCE_DIR}/engine/src/log.c)
target_include_directories(log_test PRIVATE ${PROJECT_SOURCE_DIR}/engine/include)
target_link_options(log_test PRIVATE -sENVIRONMENT=node)
add_test(NAME log_test COMMAND log_test)
