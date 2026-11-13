#include "luac.h"
#include <stdio.h>
#include <string.h>

static int expect(const char *condition, int ok, const char *json) {
    if (ok) return 1;
    fprintf(stderr, "embedded project check failed: %s\n%s\n", condition, json ? json : "(null)");
    return 0;
}
int main(void) {
    char *error = NULL, *json;
    void *project = luac_project_open(LUAC_TEST_PROJECT, &error);
    int ok = 1;
    if (!project) {
        fprintf(stderr, "cannot open embedded project: %s\n", error ? error : "unknown error");
        luac_free(error); return 1;
    }
    json = luac_project_step(project, 0);
    ok &= expect("main and nested disk require execute", json && strstr(json, "Embedded project OK"), json);
    ok &= expect("warnings are captured", json && strstr(json, "Lua warning: Embedded warning"), json);
    ok &= expect("print output is captured", json && strstr(json, "Project started:"), json);
    ok &= expect("PNG sprite is decoded", json && strstr(json, "assets/luac_logo.png"), json);
    ok &= expect("world overlay is exported", json && strstr(json, "\"texts\""), json);
    luac_free(json);
    luac_project_touch(project, 0, 40, 50, 1);
    json = luac_project_step(project, .5);
    ok &= expect("update and physics advance the world", json && strstr(json, "\"x\":10"), json);
    ok &= expect("runtime remains healthy", json && strstr(json, "\"ok\":true"), json);
    luac_free(json);
    luac_project_touch(project, 0, 40, 50, 0);
    luac_project_close(project);
    if (ok) puts("LuaC embedded multi-file project: OK");
    return ok ? 0 : 1;
}
