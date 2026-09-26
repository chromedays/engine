cmake_minimum_required(VERSION 3.24)
project(engine LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    enable_language(OBJC)
endif()

include(cmake/Dependencies.cmake)

add_subdirectory(engine)
add_subdirectory(examples/triangle)
