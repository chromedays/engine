#include "sandbox.h"

#if defined(_WIN32) && !defined(NDEBUG)

#include <engine/debug_script.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The sandbox's part of a scripted run (engine/debug_script.h has the commands every app has): the benchmark.
//   benchmark <file>             runs the shown scene's benchmark (the Stress tab's Run) and writes its results table (the Copy button's text) to the
//                                file when it is over; the next command waits for it

#define PATH_CAPACITY 520

typedef struct SandboxScript {
    b32 waiting_benchmark; // until the benchmark run (the `benchmark` command) is over
    char benchmark_path[PATH_CAPACITY];
} SandboxScript;

global SandboxScript script;

internal NvRect viewport(void* user)
{
    return ((Sandbox*)user)->layout.viewport;
}

internal b32 command(void* user, const char* const* words, u32 count)
{
    Sandbox* sandbox = user;
    const char* name = words[0];
    if (strcmp(name, "benchmark") == 0 && count == 2) {
        benchmark_start(sandbox);
        if (!sandbox->benchmark.running) {
            nv_log(NV_LOG_ERROR, "script", "the shown scene has no benchmark to run");
            exit(2);
        }
        nv_debug_script_resolve_path(script.benchmark_path, sizeof(script.benchmark_path), words[1]);
        script.waiting_benchmark = 1;
    } else {
        return 0;
    }
    return 1;
}

// True while the benchmark has not got where the script waits for it.
internal b32 busy(void* user)
{
    Sandbox* sandbox = user;
    if (script.waiting_benchmark) {
        if (sandbox->benchmark.running)
            return 1;
        char table[4096];
        benchmark_table_text(sandbox, table, sizeof(table));
        FILE* file = fopen(script.benchmark_path, "wb");
        if (!file) {
            nv_log(NV_LOG_ERROR, "script", "cannot write %s", script.benchmark_path);
            exit(2);
        }
        fputs(table, file);
        fclose(file);
        script.waiting_benchmark = 0;
    }
    return 0;
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
        .command = command,
        .busy = busy,
        .user = sandbox,
    });
}

#endif
