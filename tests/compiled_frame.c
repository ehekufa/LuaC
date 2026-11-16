#define LUAC_NO_MAIN
#include "generated.c"
#include "luac_compiled_project.h"

int main(void) {
    LcCompiledProject *a = lc_compiled_open("project\"\nname"), *b = lc_compiled_open("other");
    LcValue world, engine, text_fn, args[5], ignored;
    char *frame;
    const char text[] = "Hello \"\nПривет\0tail\xff";
    if (!a || !b || a->ctx->error || b->ctx->error) return 1;
    world = lc_compiled_global(a, "world"); engine = lc_compiled_global(a, "engine");
    text_fn = lc_engine_get(a->ctx, engine, "text");
    args[0] = world; args[1] = lc_string_n(a->ctx, text, sizeof(text) - 1);
    args[2] = lc_integer(10); args[3] = lc_integer(20); args[4] = lc_integer(18);
    ignored = lc_call(a->ctx, text_fn, 5, args);
    lc_release(a->ctx, ignored); lc_release(a->ctx, args[1]);
    lc_release(a->ctx, text_fn); lc_release(a->ctx, engine); lc_release(a->ctx, world);
    frame = lc_compiled_frame(a, 0); if (!frame) return 2; puts(frame); free(frame);
    frame = lc_compiled_frame(a, .5); if (!frame) return 3; puts(frame); free(frame);
    frame = lc_compiled_frame(b, 0); if (!frame) return 4; puts(frame); free(frame);
    frame = lc_compiled_frame(a, NAN); if (!frame) return 5; puts(frame); free(frame);
    lc_compiled_close(a); lc_compiled_close(b);
    return 0;
}
