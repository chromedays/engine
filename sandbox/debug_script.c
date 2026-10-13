#include "sandbox.h"

#if defined(_WIN32) && !defined(NDEBUG)

#include <engine/debug_script.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The sandbox's part of a scripted run (engine/debug_script.h has the commands every app has): the effects cases, the fixed game clock
// and the benchmark.
//   wait-fixed <n>               frames until the fixed game clock (`call fixed_dt`) has run n frames
//   benchmark <file>             runs the shown scene's benchmark (the Stress tab's Run) and writes its results table (the Copy button's text) to the
//                                file when it is over; the next command waits for it
//   case <id> <dt> <counters 0|1>  starts <out>/<id>.json, the dumps of an effects case; case-frame <frame> <full|counters> adds the
//                                particle dump of that frame (the next command waits for it), case-end closes the file

#define PATH_CAPACITY 520

typedef struct SandboxScript {
    // An effects case being written (docs/specs/d3d12_port.md, "Validation"): the particle dump of a frame, in the web harness's JSON.
    FILE* case_file;
    b32 case_has_frame;
    b32 dump_pending;
    u32 dump_frame;
    b32 waiting_fixed;    // until the fixed clock (the `fixed_dt` export) has run this many frames
    u32 fixed_frames_wanted;
    b32 waiting_benchmark; // until the benchmark run (the `benchmark` command) is over
    char benchmark_path[PATH_CAPACITY];
} SandboxScript;

global SandboxScript script;

internal void write_number(FILE* file, f64 value)
{
    if (value == floor(value) && fabs(value) < 1e15)
        fprintf(file, "%lld", (long long)value);
    else
        fprintf(file, "%.9g", value);
}

// The dump of one frame, as the web harness writes it (tools/d3d12_port/capture.mjs, dumpOnce).
internal void write_dump(Sandbox* sandbox)
{
    NvVfx* vfx = &sandbox->vfx;
    FILE* file = script.case_file;
    fprintf(file, "%s\"%u\":{\"capacity\":%u,\"counters\":[", script.case_has_frame ? "," : "", script.dump_frame, vfx->capacity);
    for (u32 i = 0; i < 16; ++i)
        fprintf(file, "%s%u", i ? "," : "", nv_vfx_debug_dump_counter(vfx, i));
    fprintf(file, "],\"simulateArgs\":[");
    for (u32 i = 0; i < 4; ++i)
        fprintf(file, "%s%u", i ? "," : "", nv_vfx_debug_dump_args(vfx, 0, i));
    fprintf(file, "],\"drawArgs\":[");
    for (u32 i = 0; i < 8; ++i)
        fprintf(file, "%s%u", i ? "," : "", nv_vfx_debug_dump_args(vfx, 1, i));
    NvVfxStats stats = nv_vfx_stats(vfx);
    fprintf(file, "],\"violations\":%u,\"segments\":%u,\"decals\":%u,\"particles\":[", nv_vfx_debug_dump_violations(vfx), stats.segments,
            stats.decals);
    u32 count = nv_vfx_debug_dump_particle_count(vfx);
    for (u32 i = 0; i < count; ++i) {
        fprintf(file, "%s[", i ? "," : "");
        for (u32 field = 0; field < 12; ++field) {
            fprintf(file, "%s", field ? "," : "");
            write_number(file, nv_vfx_debug_dump_particle(vfx, i, field));
        }
        fprintf(file, "]");
    }
    fprintf(file, "]}");
    script.case_has_frame = 1;
}

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
    } else if (strcmp(name, "wait-fixed") == 0 && count == 2) {
        script.waiting_fixed = 1;
        script.fixed_frames_wanted = (u32)atoi(words[1]);
    } else if (strcmp(name, "case") == 0 && count == 4) {
        char path[PATH_CAPACITY], file_name[160];
        snprintf(file_name, sizeof(file_name), "%s.json", words[1]);
        nv_debug_script_resolve_path(path, sizeof(path), file_name);
        script.case_file = fopen(path, "wb");
        if (!script.case_file) {
            nv_log(NV_LOG_ERROR, "script", "cannot write %s", path);
            exit(2);
        }
        script.case_has_frame = 0;
        fprintf(script.case_file, "{\"case\":\"%s\",\"dt\":%.17g,\"counters\":%s,\"frames\":{", words[1], atof(words[2]),
                atoi(words[3]) ? "true" : "false");
    } else if (strcmp(name, "case-frame") == 0 && count == 3 && script.case_file) {
        script.dump_frame = (u32)atoi(words[1]);
        nv_vfx_debug_dump_start(&sandbox->vfx, strcmp(words[2], "full") == 0);
        script.dump_pending = 1;
    } else if (strcmp(name, "case-end") == 0 && script.case_file) {
        fprintf(script.case_file, "}}");
        fclose(script.case_file);
        script.case_file = NULL;
    } else {
        return 0;
    }
    return 1;
}

// True while a dump's readbacks, the fixed clock or the benchmark have not got where the script waits for them.
internal b32 busy(void* user)
{
    Sandbox* sandbox = user;
    if (script.dump_pending) {
        if (nv_vfx_debug_dump_state(&sandbox->vfx) != NV_VFX_DUMP_READY)
            return 1;
        write_dump(sandbox);
        nv_vfx_debug_dump_finish(&sandbox->vfx);
        script.dump_pending = 0;
    }
    if (script.waiting_fixed) {
        // The frames the fixed clock has run, which the `frames` export tells.
        for (u32 i = 0; i < sandbox_debug_call_count; ++i) {
            if (strcmp(sandbox_debug_calls[i].name, "frames") == 0) {
                if (((int (*)(void))sandbox_debug_calls[i].function)() < (int)script.fixed_frames_wanted)
                    return 1;
            }
        }
        script.waiting_fixed = 0;
    }
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
