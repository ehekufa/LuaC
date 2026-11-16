#ifndef LUAC_COMPILED_PROJECT_H
#define LUAC_COMPILED_PROJECT_H

/* Include after a translation unit generated with --emit-c and LUAC_NO_MAIN.
   The project and its literal requires are native C functions; there is no
   parser or AST evaluator in this project runner. It uses runtime.h's shared
   object model and engine. The host owns working-directory/asset policy. */
typedef struct {
    LcContext *ctx;
    char *source;
    size_t output_offset;
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
/* A frame uses the same JSON shape as Studio's Android viewport. This is a
   native-only adapter: it does not parse or evaluate Lua at runtime. */
typedef struct {
    char *data;
    size_t length, capacity;
    int failed;
} LcCompiledJson;

static void lc_compiled_json_bytes(LcCompiledJson *out, const char *bytes, size_t length) {
    size_t required, capacity;
    char *data;
    if (out->failed) return;
    if (length > SIZE_MAX - out->length - 1) { out->failed = 1; return; }
    required = out->length + length + 1;
    if (required > out->capacity) {
        capacity = out->capacity ? out->capacity : 256;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) { capacity = required; break; }
            capacity *= 2;
        }
        data = (char *)realloc(out->data, capacity);
        if (!data) { out->failed = 1; return; }
        out->data = data; out->capacity = capacity;
    }
    memcpy(out->data + out->length, bytes, length);
    out->length += length; out->data[out->length] = 0;
}
static void lc_compiled_json_text(LcCompiledJson *out, const char *text) {
    lc_compiled_json_bytes(out, text, strlen(text));
}
static void lc_compiled_json_escaped(LcCompiledJson *out, const char *text, size_t length) {
    size_t i;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\"' || c == '\\') {
            char pair[2] = {'\\', (char)c}; lc_compiled_json_bytes(out, pair, 2);
        } else if (c < 32) {
            char escape[7]; snprintf(escape, sizeof(escape), "\\u%04x", c);
            lc_compiled_json_text(out, escape);
        } else if (c >= 128) {
            uint32_t codepoint; size_t next;
            if (lc_utf8_decode_at(text, length, i, &codepoint, &next)) {
                lc_compiled_json_bytes(out, text + i, next - i); i = next - 1;
            } else lc_compiled_json_text(out, "\\ufffd");
        } else lc_compiled_json_bytes(out, text + i, 1);
    }
}
static void lc_compiled_json_string(LcCompiledJson *out, const char *text, size_t length) {
    lc_compiled_json_text(out, "\"");
    if (text) lc_compiled_json_escaped(out, text, length);
    lc_compiled_json_text(out, "\"");
}
static void lc_compiled_json_number(LcCompiledJson *out, double number, double fallback) {
    char buffer[64];
    if (!isfinite(number)) number = fallback;
    snprintf(buffer, sizeof(buffer), "%.8g", number);
    { char *decimal = strchr(buffer, ','); if (decimal) *decimal = '.'; }
    lc_compiled_json_text(out, buffer);
}
static void lc_compiled_json_field(LcCompiledJson *out, LcContext *ctx, LcValue value,
                                   const char *name, double fallback) {
    lc_compiled_json_number(out, lc_engine_number(ctx, value, name, fallback), fallback);
}
static void lc_compiled_json_scene(LcCompiledProject *project, LcCompiledJson *out) {
    LcContext *ctx = project->ctx;
    LcValue world = lc_compiled_global(project, "world"), entities, camera, texts;
    int i, first = 1;
    lc_compiled_json_text(out, ",\"entities\":[");
    entities = world.type == LC_TABLE ? lc_engine_get(ctx, world, "entities") : lc_nil();
    if (entities.type == LC_TABLE) for (i = 0; i < (int)entities.as.table->capacity; ++i) {
        LcValue entity, active, texture, color;
        int c;
        if (!entities.as.table->entries[i].used) continue;
        entity = entities.as.table->entries[i].value;
        if (entity.type != LC_TABLE) continue;
        active = lc_engine_get(ctx, entity, "active");
        if (!lc_truthy(active)) { lc_release(ctx, active); continue; }
        lc_release(ctx, active);
        if (!first) lc_compiled_json_text(out, ",");
        first = 0;
        lc_compiled_json_text(out, "{\"x\":"); lc_compiled_json_field(out, ctx, entity, "x", 0);
        lc_compiled_json_text(out, ",\"y\":"); lc_compiled_json_field(out, ctx, entity, "y", 0);
        lc_compiled_json_text(out, ",\"width\":"); lc_compiled_json_field(out, ctx, entity, "width", 32);
        lc_compiled_json_text(out, ",\"height\":"); lc_compiled_json_field(out, ctx, entity, "height", 32);
        lc_compiled_json_text(out, ",\"rotation\":"); lc_compiled_json_field(out, ctx, entity, "rotation", 0);
        lc_compiled_json_text(out, ",\"scale\":"); lc_compiled_json_field(out, ctx, entity, "scale", 1);
        lc_compiled_json_text(out, ",\"texture\":");
        texture = lc_engine_get(ctx, entity, "texture");
        if (texture.type == LC_TABLE) {
            LcValue path = lc_engine_get(ctx, texture, "path");
            if (path.type == LC_STRING) lc_compiled_json_string(out, path.as.string->data, path.as.string->length);
            else lc_compiled_json_text(out, "null");
            lc_release(ctx, path);
        } else lc_compiled_json_text(out, "null");
        lc_release(ctx, texture);
        color = lc_engine_get(ctx, entity, "color");
        lc_compiled_json_text(out, ",\"color\":[");
        for (c = 1; c <= 4; ++c) {
            double number = 1;
            if (color.type == LC_TABLE) {
                LcValue component = lc_table_raw_get(ctx, color, lc_integer(c));
                LcValue first_value = lc_first_value(component);
                if (first_value.type == LC_INTEGER) number = (double)first_value.as.integer;
                else if (first_value.type == LC_NUMBER) number = first_value.as.number;
                lc_release(ctx, component);
            }
            if (c > 1) lc_compiled_json_text(out, ",");
            lc_compiled_json_number(out, number, 1);
        }
        lc_release(ctx, color); lc_compiled_json_text(out, "]}");
    }
    lc_release(ctx, entities);
    lc_compiled_json_text(out, "],\"camera\":{");
    camera = world.type == LC_TABLE ? lc_engine_get(ctx, world, "camera") : lc_nil();
    lc_compiled_json_text(out, "\"x\":");
    if (camera.type == LC_TABLE) lc_compiled_json_field(out, ctx, camera, "x", 0); else lc_compiled_json_text(out, "0");
    lc_compiled_json_text(out, ",\"y\":");
    if (camera.type == LC_TABLE) lc_compiled_json_field(out, ctx, camera, "y", 0); else lc_compiled_json_text(out, "0");
    lc_compiled_json_text(out, ",\"zoom\":");
    if (camera.type == LC_TABLE) lc_compiled_json_field(out, ctx, camera, "zoom", 1); else lc_compiled_json_text(out, "1");
    lc_release(ctx, camera);
    lc_compiled_json_text(out, "},\"texts\":[");
    texts = world.type == LC_TABLE ? lc_engine_get(ctx, world, "overlay_text") : lc_nil();
    if (texts.type == LC_TABLE) {
        int index; first = 1;
        for (index = 1; index <= lc_table_length(texts); ++index) {
            LcValue item = lc_table_raw_get(ctx, texts, lc_integer(index)), text;
            if (item.type != LC_TABLE) { lc_release(ctx, item); continue; }
            text = lc_engine_get(ctx, item, "text");
            if (!first) lc_compiled_json_text(out, ",");
            first = 0;
            lc_compiled_json_text(out, "{\"text\":");
            if (text.type == LC_STRING) lc_compiled_json_string(out, text.as.string->data, text.as.string->length);
            else lc_compiled_json_string(out, "", 0);
            lc_compiled_json_text(out, ",\"x\":"); lc_compiled_json_field(out, ctx, item, "x", 0);
            lc_compiled_json_text(out, ",\"y\":"); lc_compiled_json_field(out, ctx, item, "y", 0);
            lc_compiled_json_text(out, ",\"size\":"); lc_compiled_json_field(out, ctx, item, "size", 16);
            lc_compiled_json_text(out, "}");
            lc_release(ctx, text); lc_release(ctx, item);
        }
    }
    lc_release(ctx, texts); lc_release(ctx, world);
    lc_compiled_json_text(out, "]");
}

/* Returned JSON is owned by the caller and released with free(). */
static char *lc_compiled_frame(LcCompiledProject *project, double dt) {
    LcCompiledJson json = {0};
    LcContext *ctx;
    if (!project || !project->ctx) return NULL;
    ctx = project->ctx;
    if (!isfinite(dt) || dt < 0) lc_error(ctx, "frame time must be finite and non-negative");
    else (void)lc_compiled_step(project, dt);
    lc_compiled_json_text(&json, ctx->error ? "{\"ok\":false,\"output\":" : "{\"ok\":true,\"output\":");
    if (ctx->captured_output && project->output_offset < ctx->captured_length)
        lc_compiled_json_string(&json, ctx->captured_output + project->output_offset, ctx->captured_length - project->output_offset);
    else lc_compiled_json_string(&json, "", 0);
    lc_compiled_json_text(&json, ",\"error\":");
    if (ctx->error) {
        char line[32]; const char *source = ctx->source ? ctx->source : "project";
        snprintf(line, sizeof(line), ":%d: ", ctx->line);
        lc_compiled_json_text(&json, "\"");
        lc_compiled_json_escaped(&json, source, strlen(source));
        lc_compiled_json_escaped(&json, line, strlen(line));
        lc_compiled_json_escaped(&json, ctx->message, strlen(ctx->message));
        lc_compiled_json_text(&json, "\"");
    } else lc_compiled_json_text(&json, "null");
    lc_compiled_json_scene(project, &json);
    lc_compiled_json_text(&json, ",\"love\":null}");
    if (json.failed) { free(json.data); return NULL; }
    project->output_offset = ctx->captured_length;
    return json.data;
}
#endif
