#ifndef LUAC_LOVE_H
#define LUAC_LOVE_H

/* Small LÖVE-style API for the embedded project runner. Commands are owned
   by the session and discarded at the beginning of each draw frame. */
static void li_love_clear(LiSession *session) {
    int i;
    for (i = 0; i < session->draw_count; ++i) free(session->draw_commands[i].text);
    session->draw_count = 0;
}
static void li_love_call(LiSession *session, const char *name, int argc, LcValue *argv) {
    LcValue love = li_global(session, "love"), function = li_field(session->ctx, love, name);
    if (function.type == LC_FUNCTION) {
        LcValue result = lc_call(session->ctx, function, argc, argv);
        lc_release(session->ctx, result);
    }
    lc_release(session->ctx, function); lc_release(session->ctx, love);
}
static double li_love_channel(LcContext *ctx, int argc, LcValue *argv, int index, double fallback) {
    double n = lc_lib_number_arg(ctx, argc, argv, index, fallback);
    if (!isfinite(n)) { lc_error(ctx, "color must be finite"); return 0; }
    return n < 0 ? 0 : n > 1 ? 1 : n;
}
static LcValue li_love_image_size(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcValue width, height;
    if (!argc || lc_first_value(argv[0]).type != LC_TABLE) { lc_error(ctx, "image expected"); return lc_nil(); }
    width = li_field(ctx, argv[0], "width"); height = li_field(ctx, argv[0], "height");
    if (!strcmp(self->name, "getDimensions")) {
        LcValue values[2] = {width, height}; return lc_tuple_new(ctx, 2, values, 1);
    }
    if (!strcmp(self->name, "getWidth")) { lc_release(ctx, height); return width; }
    lc_release(ctx, width); return height;
}
static LcValue li_love_graphics(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LiSession *session = li_current;
    const char *name = self->name;
    LiDrawCommand *cmd;
    int i;
    if (!session || session->ctx != ctx) { lc_error(ctx, "no active project"); return lc_nil(); }
    if (!strcmp(name, "setColor") || !strcmp(name, "setBackgroundColor")) {
        double *target = !strcmp(name, "setColor") ? session->draw_color : session->background;
        for (i = 0; i < 4; ++i) target[i] = li_love_channel(ctx, argc, argv, i, i == 3 ? 1 : 0);
        return lc_nil();
    }
    if (!strcmp(name, "getColor") || !strcmp(name, "getBackgroundColor")) {
        double *color = !strcmp(name, "getColor") ? session->draw_color : session->background;
        LcValue result[4]; for (i = 0; i < 4; ++i) result[i] = lc_number(color[i]);
        return lc_tuple_new(ctx, 4, result, 1);
    }
    if (!strcmp(name, "getWidth")) return lc_integer(800);
    if (!strcmp(name, "getHeight")) return lc_integer(600);
    if (!strcmp(name, "getDimensions")) { LcValue dims[2] = {lc_integer(800), lc_integer(600)}; return lc_tuple_new(ctx, 2, dims, 1); }
    if (!strcmp(name, "newImage")) {
        LcString *path = lc_lib_string_arg(ctx, argc, argv, 0);
        LcValue image; int width, height, channels;
        if (!path) return lc_nil();
        if (!stbi_info(path->data, &width, &height, &channels)) { lc_error(ctx, "cannot open image: %s", path->data); return lc_nil(); }
        image = lc_table(ctx);
        lc_set_field(ctx, image, "path", lc_string_n(ctx, path->data, path->length));
        lc_set_field(ctx, image, "width", lc_integer(width)); lc_set_field(ctx, image, "height", lc_integer(height));
        lc_add_native(ctx, image, "getWidth", li_love_image_size);
        lc_add_native(ctx, image, "getHeight", li_love_image_size);
        lc_add_native(ctx, image, "getDimensions", li_love_image_size);
        return image;
    }
    if (!session->love_draw_active) { lc_error(ctx, "love.graphics.%s requires love.draw", name); return lc_nil(); }
    if (session->draw_count >= 4096) { lc_error(ctx, "love.draw command limit exceeded"); return lc_nil(); }
    if (session->draw_count == session->draw_capacity) {
        int capacity = session->draw_capacity ? session->draw_capacity * 2 : 64;
        LiDrawCommand *commands = (LiDrawCommand *)realloc(session->draw_commands, sizeof(*commands) * (size_t)capacity);
        if (!commands) { lc_error(ctx, "out of memory"); return lc_nil(); }
        session->draw_commands = commands; session->draw_capacity = capacity;
    }
    cmd = &session->draw_commands[session->draw_count++]; memset(cmd, 0, sizeof(*cmd));
    memcpy(cmd->color, session->draw_color, sizeof(cmd->color));
    if (!strcmp(name, "draw")) {
        LcValue image = argc ? lc_first_value(argv[0]) : lc_nil(), path;
        cmd->kind = 4;
        if (image.type != LC_TABLE) { lc_error(ctx, "image expected"); return lc_nil(); }
        path = li_field(ctx, image, "path");
        if (path.type != LC_STRING) { lc_release(ctx, path); lc_error(ctx, "image expected"); return lc_nil(); }
        cmd->text = (char *)malloc(path.as.string->length + 1);
        if (!cmd->text) lc_error(ctx, "out of memory");
        else { memcpy(cmd->text, path.as.string->data, path.as.string->length); cmd->text[path.as.string->length] = 0; }
        lc_release(ctx, path);
        cmd->x = lc_lib_number_arg(ctx, argc, argv, 1, 0); cmd->y = lc_lib_number_arg(ctx, argc, argv, 2, 0);
        cmd->rotation = lc_lib_number_arg(ctx, argc, argv, 3, 0);
        cmd->scale_x = lc_lib_number_arg(ctx, argc, argv, 4, 1);
        cmd->scale_y = lc_lib_number_arg(ctx, argc, argv, 5, cmd->scale_x);
        cmd->origin_x = lc_lib_number_arg(ctx, argc, argv, 6, 0); cmd->origin_y = lc_lib_number_arg(ctx, argc, argv, 7, 0);
    } else if (!strcmp(name, "print")) {
        LcString *text = lc_lib_string_arg(ctx, argc, argv, 0);
        cmd->kind = 3;
        if (text) {
            cmd->text = (char *)malloc(text->length + 1);
            if (!cmd->text) lc_error(ctx, "out of memory");
            else { memcpy(cmd->text, text->data, text->length); cmd->text[text->length] = 0; }
        }
        cmd->x = lc_lib_number_arg(ctx, argc, argv, 1, 0);
        cmd->y = lc_lib_number_arg(ctx, argc, argv, 2, 0);
    } else {
        LcString *mode = lc_lib_string_arg(ctx, argc, argv, 0);
        if (mode && strcmp(mode->data, "fill") && strcmp(mode->data, "line")) lc_error(ctx, "expected 'fill' or 'line'");
        cmd->line = mode && !strcmp(mode->data, "line");
        cmd->kind = !strcmp(name, "circle") ? 2 : 1;
        cmd->x = lc_lib_number_arg(ctx, argc, argv, 1, 0);
        cmd->y = lc_lib_number_arg(ctx, argc, argv, 2, 0);
        cmd->w = lc_lib_number_arg(ctx, argc, argv, 3, 0);
        cmd->h = cmd->kind == 1 ? lc_lib_number_arg(ctx, argc, argv, 4, 0) : 0;
    }
    if (!isfinite(cmd->x) || !isfinite(cmd->y) || !isfinite(cmd->w) || !isfinite(cmd->h) || !isfinite(cmd->rotation) || !isfinite(cmd->scale_x) || !isfinite(cmd->scale_y) || !isfinite(cmd->origin_x) || !isfinite(cmd->origin_y)) {
        cmd->x = cmd->y = cmd->w = cmd->h = cmd->rotation = cmd->scale_x = cmd->scale_y = cmd->origin_x = cmd->origin_y = 0;
        lc_error(ctx, "drawing coordinates must be finite");
    }
    return lc_nil();
}
static void li_love_init(LiSession *session) {
    LcContext *ctx = session->ctx;
    LcValue love = lc_table(ctx), graphics = lc_table(ctx);
    const char *names[] = {"setColor", "getColor", "setBackgroundColor", "getBackgroundColor", "getWidth", "getHeight", "getDimensions", "rectangle", "circle", "print", "newImage", "draw", NULL};
    int i;
    for (i = 0; i < 4; ++i) { session->draw_color[i] = 1; session->background[i] = i == 3 ? 1 : 0; }
    for (i = 0; names[i]; ++i) lc_add_native(ctx, graphics, names[i], li_love_graphics);
    lc_set_field(ctx, love, "graphics", graphics);
    lc_lib_register(ctx, "love", love);
}
static void li_love_frame(LiSession *session, double dt) {
    LcValue love = li_global(session, "love"), draw = li_field(session->ctx, love, "draw");
    session->love_enabled = draw.type == LC_FUNCTION;
    lc_release(session->ctx, draw); lc_release(session->ctx, love);
    li_love_clear(session);
    if (dt > 0) { LcValue value = lc_number(dt); li_love_call(session, "update", 1, &value); }
    if (!session->ctx->error && session->love_enabled) {
        session->love_draw_active = 1; li_love_call(session, "draw", 0, NULL); session->love_draw_active = 0;
    }
}
static void li_love_touch(LiSession *session, int id, double x, double y, int down) {
    LcValue args[6]; const char *callback; int was;
    if (id < 0 || id >= 32) return;
    was = session->touch_down[id]; callback = down ? (was ? "touchmoved" : "touchpressed") : "touchreleased";
    if (!down && !was) return;
    args[0] = lc_integer(id); args[1] = lc_number(x); args[2] = lc_number(y);
    args[3] = lc_number(was ? x - session->touch_x[id] : 0);
    args[4] = lc_number(was ? y - session->touch_y[id] : 0); args[5] = lc_number(down ? 1 : 0);
    session->touch_down[id] = !!down; session->touch_x[id] = x; session->touch_y[id] = y;
    session->budget = 250000; li_love_call(session, callback, 6, args);
}
static void li_love_json(LiSession *session, Str *json) {
    int i, c;
    if (!session->love_enabled) { sb_add(json, ",\"love\":null"); return; }
    sb_add(json, ",\"love\":{\"width\":800,\"height\":600,\"background\":[");
    for (c = 0; c < 4; ++c) { if (c) sb_add(json, ","); sb_printf(json, "%.8g", session->background[c]); }
    sb_add(json, "],\"commands\":[");
    for (i = 0; i < session->draw_count; ++i) {
        LiDrawCommand *cmd = &session->draw_commands[i];
        if (i) sb_add(json, ",");
        sb_printf(json, "{\"kind\":%d,\"line\":%s,\"x\":%.8g,\"y\":%.8g,\"w\":%.8g,\"h\":%.8g,\"color\":[", cmd->kind, cmd->line ? "true" : "false", cmd->x, cmd->y, cmd->w, cmd->h);
        for (c = 0; c < 4; ++c) { if (c) sb_add(json, ","); sb_printf(json, "%.8g", cmd->color[c]); }
        sb_add(json, "]");
        if (cmd->kind == 3 || cmd->kind == 4) { sb_add(json, ",\"text\":"); li_json_string(json, cmd->text); }
        if (cmd->kind == 4) sb_printf(json, ",\"rotation\":%.8g,\"sx\":%.8g,\"sy\":%.8g,\"ox\":%.8g,\"oy\":%.8g", cmd->rotation, cmd->scale_x, cmd->scale_y, cmd->origin_x, cmd->origin_y);
        sb_add(json, "}");
    }
    sb_add(json, "]}");
}
#endif
