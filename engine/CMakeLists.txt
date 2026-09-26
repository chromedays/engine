add_library(engine STATIC
    src/gpu.c
    src/surface.c)

if(EMSCRIPTEN)
    target_sources(engine PRIVATE src/window_web.c)
else()
    target_sources(engine PRIVATE src/window_glfw.c)
    target_link_libraries(engine PUBLIC glfw)
    # Stop glfw3.h from pulling in OpenGL headers; rendering goes through WebGPU.
    target_compile_definitions(engine PRIVATE GLFW_INCLUDE_NONE)
endif()

if(APPLE)
    target_sources(engine PRIVATE src/surface_metal.m)
    target_compile_options(engine PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)
    target_link_libraries(engine PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Cocoa")
endif()

target_include_directories(engine PUBLIC include PRIVATE src)
target_link_libraries(engine PUBLIC webgpu)
if(NOT MSVC)
    target_compile_options(engine PRIVATE -Wall -Wextra)
endif()
