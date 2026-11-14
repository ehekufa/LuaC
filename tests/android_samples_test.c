#include "luac.h"
#include <stdio.h>
#include <string.h>

#ifndef LUAC_SAMPLE_ROOT
#error LUAC_SAMPLE_ROOT is required
#endif

int main(void) {
    const char *ids[] = {"hello", "library", "gui", "ffi"};
    const char *messages[] = {"Hello, LuaC!", "SHA-256:", "luac_gui Canvas example ready", ""};
    size_t i;
    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        char path[4096], *error = NULL, *frame;
        void *project;
        snprintf(path, sizeof(path), "%s/%s", LUAC_SAMPLE_ROOT, ids[i]);
        project = luac_project_open(path, &error);
        if (!project) {
            fprintf(stderr, "%s: %s\n", ids[i], error ? error : "cannot open");
            luac_free(error); return 1;
        }
        frame = luac_project_step(project, 0);
        if (!frame || !strstr(frame, "\"ok\":true") || !strstr(frame, messages[i])) {
            fprintf(stderr, "%s failed: %s\n", ids[i], frame ? frame : "null");
            luac_free(frame); luac_project_close(project); return 1;
        }
        luac_free(frame);
        luac_project_touch(project, 0, 40, 50, 1);
        frame = luac_project_step(project, 0.016);
        if (!frame || !strstr(frame, "\"ok\":true") || (i == 2 && !strstr(frame, "\"x\":40"))) {
            fprintf(stderr, "%s update failed: %s\n", ids[i], frame ? frame : "null");
            luac_free(frame); luac_project_close(project); return 1;
        }
        luac_free(frame); luac_project_close(project);
    }
    puts("Android sample projects: OK");
    return 0;
}
