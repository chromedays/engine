#include "sandbox.h"

#if NV_PLATFORM_WINDOWS && NV_DEBUG

#include <engine/debug_script.h>

// The sandbox's part of a scripted run (engine/debug_script.h has the commands every app has): its debug exports, and the
// viewport a `screenshot <png> viewport` cuts out. It adds no commands of its own.

internal NvRect viewport(void* user)
{
    return ((Sandbox*)user)->layout.viewport;
}

void debug_script_init(Sandbox* sandbox)
{
    nv_debug_script_init(&(NvDebugScriptDesc){
        .window = &sandbox->window,
        .gpu = &sandbox->gpu,
        .arena = &sandbox->permanent,
        .calls = sandbox_debug_calls,
        .call_count = sandbox_debug_call_count,
        .viewport = viewport,
        .user = sandbox,
    });
}

#endif
