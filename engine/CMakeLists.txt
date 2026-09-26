add_library(engine STATIC
    src/Window.cpp
    src/GpuContext.cpp
    src/GlfwSurface.cpp)

if(APPLE)
    target_sources(engine PRIVATE src/GlfwSurfaceMetal.mm)
    target_link_libraries(engine PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Cocoa")
endif()

target_include_directories(engine PUBLIC include PRIVATE src)
target_link_libraries(engine PUBLIC wgpu_native glfw)
# Stop glfw3.h from pulling in OpenGL headers; rendering goes through WebGPU.
target_compile_definitions(engine PRIVATE GLFW_INCLUDE_NONE)
