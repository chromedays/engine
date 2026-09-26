#include "glfw_surface.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

void* eng_get_or_create_metal_layer(GLFWwindow* window)
{
    NSWindow* ns_window = glfwGetCocoaWindow(window);
    NSView* view = [ns_window contentView];
    if (![view.layer isKindOfClass:[CAMetalLayer class]]) {
        [view setWantsLayer:YES];
        [view setLayer:[CAMetalLayer layer]];
    }
    return (__bridge void*)view.layer;
}
