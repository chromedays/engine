#include "GlfwSurface.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

namespace engine {

void* getOrCreateMetalLayer(GLFWwindow* window)
{
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    NSView* view = [nsWindow contentView];
    if (![view.layer isKindOfClass:[CAMetalLayer class]]) {
        [view setWantsLayer:YES];
        [view setLayer:[CAMetalLayer layer]];
    }
    return (__bridge void*)view.layer;
}

} // namespace engine
