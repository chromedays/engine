#include <windows.h> // before the engine's headers: base.h defines `internal`

#include <engine/file.h>
#include <engine/local_file.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Files on the user's computer on Windows (engine/local_file_win32.c): saving to a new file, reading it back, saving in place, and
// refusing to overwrite what something else changed. The dialogs are skipped: a Debug build takes the path from NV_LOCAL_FILE.

global int failures;
#define CHECK(condition) do { if (!(condition)) { printf("FAILED line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

global u8 memory[NV_KILOBYTES(64)];

internal NvLocalFile poll(void)
{
    NvLocalFile file = {0};
    nv_local_file_poll(&file);
    return file;
}

internal void write_outside(const char* path, const char* text)
{
    FILE* file = fopen(path, "wb");
    fwrite(text, 1, strlen(text), file);
    fclose(file);
}

int main(void)
{
    char path[520];
    snprintf(path, sizeof(path), "%s\\local_file_test.abproj", LOCAL_FILE_TEST_DIR);
    remove(path);
    _putenv_s("NV_LOCAL_FILE", path);

    NvArena arena;
    nv_arena_init(&arena, memory, sizeof(memory));

    // A new file: written, kept, and named without its folder.
    CHECK(nv_local_file_save("project.abproj", "hello", 5, 1));
    NvLocalFile file = poll();
    CHECK(file.status == NV_LOCAL_FILE_WRITTEN && file.kept && strcmp(file.name, "local_file_test.abproj") == 0);
    CHECK(poll().status == NV_LOCAL_FILE_IDLE); // a result is reported once
    NvFileData read = nv_file_read(&arena, path);
    CHECK(read.ok && read.size == 5 && memcmp(read.bytes, "hello", 5) == 0);

    // In place, while nothing else touched the file.
    CHECK(nv_local_file_save("project.abproj", "hello world", 11, 0));
    CHECK(poll().status == NV_LOCAL_FILE_WRITTEN);

    // Something else changed it: not overwritten.
    Sleep(50);
    write_outside(path, "changed outside");
    CHECK(nv_local_file_save("project.abproj", "mine", 4, 0));
    file = poll();
    CHECK(file.status == NV_LOCAL_FILE_FAILED && strstr(file.error, "changed on disk"));
    read = nv_file_read(&arena, path);
    CHECK(read.ok && read.size == 15 && memcmp(read.bytes, "changed outside", 15) == 0);

    // Reading the kept file again gives what is there now, and saving in place works after that.
    CHECK(nv_local_file_reload());
    CHECK(poll().status == NV_LOCAL_FILE_READ);
    NvFileData taken = nv_local_file_take(&arena, 1000);
    CHECK(taken.ok && taken.size == 15 && memcmp(taken.bytes, "changed outside", 15) == 0 && taken.bytes[15] == 0);
    CHECK(poll().status == NV_LOCAL_FILE_IDLE);
    CHECK(nv_local_file_save("project.abproj", "mine", 4, 0));
    CHECK(poll().status == NV_LOCAL_FILE_WRITTEN);

    // Open: the same path stands for the dialog's answer. A file larger than the caller allows is refused and dropped.
    CHECK(nv_local_file_open(".abproj"));
    file = poll();
    CHECK(file.status == NV_LOCAL_FILE_READ);
    taken = nv_local_file_take(&arena, 3);
    CHECK(!taken.ok);
    CHECK(poll().status == NV_LOCAL_FILE_IDLE);

    remove(path);
    if (failures) {
        printf("local_file_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("local_file_test: ok\n");
    return 0;
}
