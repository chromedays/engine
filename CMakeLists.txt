cmake_minimum_required(VERSION 3.24)
project(engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    enable_language(OBJCXX)
endif()

include(cmake/Dependencies.cmake)

add_subdirectory(engine)
add_subdirectory(examples/triangle)
