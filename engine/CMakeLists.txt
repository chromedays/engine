add_library(nv STATIC
    src/gpu.c
    src/surface.c)

if(EMSCRIPTEN)
    target_sources(nv PRIVATE src/window_web.c)
else()
    target_sources(nv PRIVATE src/window_glfw.c)
    target_link_libraries(nv PUBLIC glfw)
    # Stop glfw3.h from pulling in OpenGL headers; rendering goes through WebGPU.
    target_compile_definitions(nv PRIVATE GLFW_INCLUDE_NONE)
endif()

if(APPLE)
    target_sources(nv PRIVATE src/surface_metal.m)
    target_compile_options(nv PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)
    target_link_libraries(nv PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Cocoa")
endif()

target_include_directories(nv PUBLIC include PRIVATE src)
target_link_libraries(nv PUBLIC webgpu)
if(NOT MSVC)
    target_compile_options(nv PRIVATE -Wall -Wextra)
endif()
