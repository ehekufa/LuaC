#ifndef LUAC_COMPILED_PROJECT_H
#define LUAC_COMPILED_PROJECT_H

/* Include after a translation unit generated with --emit-c and LUAC_NO_MAIN.
   The project and its literal requires are native C functions; there is no
   parser or AST evaluator in this project runner. It uses runtime.h's shared
   object model and engine. The host owns working-directory/asset policy. */
typedef struct {
    LcContext *ctx;
    char *source;
} LcCompiledProject;

static LcValue lc_compiled_global(LcCompiledProject *project, const char *name) {
    LcValue key = lc_string(project->ctx, name);
    LcValue result = lc_table_get(project->ctx, project->ctx->globals, key);
    lc_release(project->ctx, key);
    return result;
}

static LcCompiledProject *lc_compiled_open(const char *source) {
    LcCompiledProject *project = (LcCompiledProject *)calloc(1, sizeof(*project));
    size_t length;
    if (!project) return NULL;
    if (!source) source = "compiled project";
    length = strlen(source);
    project->source = (char *)malloc(length + 1);
    if (!project->source) { free(project); return NULL; }
    memcpy(project->source, source, length + 1);
    project->ctx = lc_context_new(project->source);
    if (!project->ctx) { free(project->source); free(project); return NULL; }
    project->ctx->capture_output = 1;
    (void)lc_program_main(project->ctx);
    return project;
}

static int lc_compiled_step(LcCompiledProject *project, double dt) {
    LcContext *ctx;
    LcValue update, world, engine, method;
    if (!project || !project->ctx) return 0;
    ctx = project->ctx;
    if (ctx->error) return 0;
    if (dt <= 0) return 1;
    update = lc_compiled_global(project, "update");
    if (update.type == LC_FUNCTION) {
        LcValue argument = lc_number(dt), result = lc_call(ctx, update, 1, &argument);
        lc_release(ctx, result);
    }
    lc_release(ctx, update);
    if (ctx->error) return 0;
    world = lc_compiled_global(project, "world");
    engine = lc_compiled_global(project, "engine");
    if (world.type == LC_TABLE && engine.type == LC_TABLE) {
        method = lc_engine_get(ctx, engine, "update");
        if (method.type == LC_FUNCTION) {
            LcValue args[2] = {world, lc_number(dt)};
            LcValue result = lc_call(ctx, method, 2, args);
            lc_release(ctx, result);
        }
        lc_release(ctx, method);
    }
    lc_release(ctx, world); lc_release(ctx, engine);
    return !ctx->error;
}

static int lc_compiled_touch(LcCompiledProject *project, int id, double x, double y, int down) {
    LcContext *ctx;
    LcValue world, engine, method;
    if (!project || !project->ctx || project->ctx->error) return 0;
    ctx = project->ctx;
    world = lc_compiled_global(project, "world");
    engine = lc_compiled_global(project, "engine");
    if (world.type == LC_TABLE && engine.type == LC_TABLE) {
        method = lc_engine_get(ctx, engine, "inject_touch");
        if (method.type == LC_FUNCTION) {
            LcValue args[5] = {world, lc_integer(id), lc_number(x), lc_number(y), lc_bool(down)};
            LcValue result = lc_call(ctx, method, 5, args);
            lc_release(ctx, result);
        }
        lc_release(ctx, method);
    }
    lc_release(ctx, world); lc_release(ctx, engine);
    return !ctx->error;
}

static void lc_compiled_close(LcCompiledProject *project) {
    if (!project) return;
    lc_context_destroy(project->ctx);
    free(project->source);
    free(project);
}
#endif
