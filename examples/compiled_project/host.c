/* Generated at build time from main.lua, not interpreted at runtime. */
#define LUAC_NO_MAIN
#include "generated.c"
#include "luac_compiled_project.h"

int main(void) {
    LcCompiledProject *project = lc_compiled_open("compiled project");
    LcCompiledProject *other = lc_compiled_open("other project");
    LcValue hero, world, engine, touch, result, args[2];
    double x;
    if (!project || !other || project->ctx->error || other->ctx->error) return 1;
    if (!project->ctx->captured_output || !strstr(project->ctx->captured_output, "Compiled project ready")) return 2;
    if (!lc_compiled_step(project, .5) || !lc_compiled_touch(project, 3, 40, 50, 1)) return 3;
    hero = lc_compiled_global(other, "hero");
    x = lc_engine_number(other->ctx, hero, "x", -1);
    lc_release(other->ctx, hero);
    if (x != 0) return 6;
    hero = lc_compiled_global(project, "hero");
    x = lc_engine_number(project->ctx, hero, "x", 0);
    lc_release(project->ctx, hero);
    world = lc_compiled_global(project, "world");
    engine = lc_compiled_global(project, "engine");
    touch = lc_engine_get(project->ctx, engine, "touch_pos");
    args[0] = world; args[1] = lc_integer(3);
    result = lc_call(project->ctx, touch, 2, args);
    if (x != 5 || result.type != LC_TUPLE || result.as.tuple->count != 2 ||
        result.as.tuple->items[0].type != LC_NUMBER || result.as.tuple->items[0].as.number != 40) return 4;
    lc_release(project->ctx, result); lc_release(project->ctx, touch);
    lc_release(project->ctx, engine); lc_release(project->ctx, world);
    if (!lc_compiled_touch(project, 3, 40, 50, 0)) return 5;
    lc_compiled_close(other);
    lc_compiled_close(project);
    puts("Compiled project without interpreter: OK");
    return 0;
}
