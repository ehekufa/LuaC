#ifndef LUAC_INTERPRETER_H
#define LUAC_INTERPRETER_H

/* In-process project runner for sandboxed embedders (notably Android). The
   desktop compiler still emits native C; this evaluator uses the same AST and
   native runtime libraries when a C toolchain is not available on the device. */
#include "runtime.h"

typedef struct LiEnv LiEnv;
typedef struct LiBinding LiBinding;
typedef struct LiFunction LiFunction;
typedef struct LiModule LiModule;

struct LiBinding {
    char *name;
    LcCell *cell;
    LiBinding *next;
};
struct LiEnv {
    LiEnv *parent;
    LiBinding *bindings;
    int refs;
};
struct LiFunction {
    Expr *definition;
    LiEnv *closure;
    LcFunction *native;
    LiFunction *next;
};
struct LiModule {
    char *name;
    char *source;
    Parser parser;
    Stmt *program;
    Expr *chunk; /* Synthetic function owned by this parsed source. */
    LiModule *next;
};
typedef struct {
    char *text;
    int kind, line;
    double x, y, w, h, rotation, scale_x, scale_y, origin_x, origin_y, color[4];
} LiDrawCommand;
typedef struct {
    LcContext *ctx;
    LiFunction *functions;
    LiModule *modules;
    LiEnv *root;
    char *root_path;
    char *previous_cwd;
    size_t output_offset;
    long budget;
    LiDrawCommand *draw_commands;
    int draw_count, draw_capacity, love_draw_active, love_enabled;
    double draw_color[4], background[4];
    unsigned char touch_down[32];
    double touch_x[32], touch_y[32];
} LiSession;
typedef struct {
    LiSession *session;
    LcValue varargs;
} LiFrame;
typedef enum { LI_NORMAL, LI_BREAK, LI_RETURN } LiFlow;

static LiSession *li_current;
static LcValue li_eval(LiFrame *frame, LiEnv *env, Expr *expr);
static LiFlow li_exec_statements(LiFrame *frame, LiEnv *env, Stmt *stmt, LcValue *result);
static LcValue li_require(LcContext *ctx, LcFunction *self, int argc, LcValue *argv);

static int li_tick(LiSession *session) {
    if (session->ctx->error) return 0;
    if (--session->budget <= 0) {
        lc_error(session->ctx, "execution budget exceeded (possible infinite loop)");
        return 0;
    }
    return 1;
}
static LiEnv *li_env_new(LiEnv *parent) {
    LiEnv *env = (LiEnv *)calloc(1, sizeof(*env));
    if (!env) return NULL;
    env->refs = 1; env->parent = parent;
    if (parent) parent->refs++;
    return env;
}
static void li_env_release(LcContext *ctx, LiEnv *env) {
    LiBinding *binding, *next;
    if (!env || --env->refs) return;
    for (binding = env->bindings; binding; binding = next) {
        next = binding->next;
        lc_cell_release(ctx, binding->cell);
        free(binding->name); free(binding);
    }
    li_env_release(ctx, env->parent);
    free(env);
}
static LiBinding *li_local(LiEnv *env, const char *name) {
    LiBinding *binding;
    for (binding = env ? env->bindings : NULL; binding; binding = binding->next)
        if (!strcmp(binding->name, name)) return binding;
    return NULL;
}
static LiBinding *li_resolve(LiEnv *env, const char *name) {
    for (; env; env = env->parent) {
        LiBinding *binding = li_local(env, name);
        if (binding) return binding;
    }
    return NULL;
}
static int li_declare(LiFrame *frame, LiEnv *env, const char *name, LcValue value) {
    LiBinding *binding = (LiBinding *)calloc(1, sizeof(*binding));
    if (!binding) { lc_release(frame->session->ctx, value); lc_error(frame->session->ctx, "out of memory"); return 0; }
    binding->name = xstrdup(name);
    binding->cell = lc_cell_new(frame->session->ctx, value);
    binding->next = env->bindings; env->bindings = binding;
    return binding->cell != NULL;
}
static LcValue li_global(LiSession *session, const char *name) {
    LcValue key = lc_string(session->ctx, name);
    LcValue value = lc_table_get(session->ctx, session->ctx->globals, key);
    lc_release(session->ctx, key); return value;
}
static void li_set_global(LiSession *session, const char *name, LcValue value) {
    LcValue key = lc_string(session->ctx, name);
    lc_table_set(session->ctx, session->ctx->globals, key, value);
    lc_release(session->ctx, key); lc_release(session->ctx, value);
}
static LiFunction *li_find_function(LiSession *session, LcFunction *native) {
    LiFunction *function;
    for (function = session->functions; function; function = function->next)
        if (function->native == native) return function;
    return NULL;
}
static LcValue li_call_function(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LiSession *session = li_current;
    LiFunction *function = li_find_function(session, self);
    LiEnv *env;
    LiFrame frame;
    LcValue result = lc_nil();
    int i, vararg_count;
    if (!function) { lc_error(ctx, "invalid interpreted function"); return result; }
    env = li_env_new(function->closure);
    if (!env) { lc_error(ctx, "out of memory"); return result; }
    for (i = 0; i < function->definition->nparams; ++i)
        li_declare(&(LiFrame){session, lc_nil()}, env, function->definition->params[i],
            i < argc ? lc_first(argv[i]) : lc_nil());
    vararg_count = argc > function->definition->nparams ? argc - function->definition->nparams : 0;
    frame.session = session;
    frame.varargs = function->definition->flag
        ? lc_tuple_new(ctx, vararg_count, vararg_count ? argv + function->definition->nparams : NULL, 0) : lc_nil();
    li_exec_statements(&frame, env, function->definition->body, &result);
    lc_release(ctx, frame.varargs);
    li_env_release(ctx, env);
    return result;
}
static LcValue li_closure(LiFrame *frame, LiEnv *env, Expr *definition) {
    LiSession *session = frame->session;
    LiFunction *function = (LiFunction *)calloc(1, sizeof(*function));
    LcValue value;
    if (!function) { lc_error(session->ctx, "out of memory"); return lc_nil(); }
    function->definition = definition; function->closure = env; env->refs++;
    value = lc_function_new(session->ctx, li_call_function, 0, NULL, "project function");
    function->native = value.type == LC_FUNCTION ? value.as.function : NULL;
    function->next = session->functions; session->functions = function;
    return value;
}
static LcValue li_index(LiFrame *frame, LcValue object, const char *name) {
    LcValue key = lc_string(frame->session->ctx, name);
    LcValue value = lc_table_get(frame->session->ctx, object, key);
    lc_release(frame->session->ctx, key); return value;
}
static void li_assign(LiFrame *frame, LiEnv *env, Expr *target, LcValue value) {
    LcContext *ctx = frame->session->ctx;
    if (target->kind == E_VAR) {
        LiBinding *binding = li_resolve(env, target->name);
        if (binding) { lc_cell_set(ctx, binding->cell, value); return; }
        li_set_global(frame->session, target->name, value); return;
    }
    if (target->kind == E_INDEX) {
        LcValue object = li_eval(frame, env, target->a);
        LcValue key = li_eval(frame, env, target->b);
        lc_table_set(ctx, object, key, value);
        lc_release(ctx, object); lc_release(ctx, key); lc_release(ctx, value); return;
    }
    lc_release(ctx, value); lc_error(ctx, "invalid assignment target");
}
static LcValue li_eval_call(LiFrame *frame, LiEnv *env, Expr *expr) {
    LcContext *ctx = frame->session->ctx;
    LcValue function, self = lc_nil(), result;
    LcValue *arguments;
    int base = expr->flag ? 1 : 0, count = expr->count + base, i;
    if (expr->flag) {
        self = li_eval(frame, env, expr->a);
        function = li_index(frame, self, expr->name);
    } else function = li_eval(frame, env, expr->a);
    arguments = (LcValue *)xmalloc(sizeof(LcValue) * (size_t)(count ? count : 1));
    if (base) arguments[0] = self;
    for (i = 0; i < expr->count; ++i) arguments[i + base] = li_eval(frame, env, expr->items[i]);
    result = lc_call_list(ctx, function, count, arguments);
    lc_release(ctx, function);
    for (i = 0; i < count; ++i) lc_release(ctx, arguments[i]);
    free(arguments); return result;
}
static LcValue li_eval(LiFrame *frame, LiEnv *env, Expr *expr) {
    LcContext *ctx = frame->session->ctx;
    LcValue a, b, result;
    int i;
    if (!expr || !li_tick(frame->session)) return lc_nil();
    switch (expr->kind) {
        case E_NIL: return lc_nil();
        case E_BOOL: return lc_bool(expr->flag);
        case E_NUMBER: return expr->is_integer ? lc_integer(expr->integer) : lc_number(expr->number);
        case E_STRING: return lc_string_n(ctx, expr->name ? expr->name : "", expr->name ? strlen(expr->name) : 0);
        case E_VAR: {
            LiBinding *binding = li_resolve(env, expr->name);
            return binding ? lc_cell_get(binding->cell) : li_global(frame->session, expr->name);
        }
        case E_VARARG: return lc_retain(frame->varargs);
        case E_UNARY:
            a = li_eval(frame, env, expr->a);
            result = expr->op == 3 ? lc_first(a) : lc_unary(ctx, expr->op, a);
            lc_release(ctx, a); return result;
        case E_BINARY:
            a = li_eval(frame, env, expr->a);
            if (expr->op == 14 || expr->op == 15) {
                int truth = lc_truthy(lc_first_value(a));
                if ((expr->op == 14 && truth) || (expr->op == 15 && !truth)) return a;
                lc_release(ctx, a); return li_eval(frame, env, expr->b);
            }
            b = li_eval(frame, env, expr->b);
            result = lc_binary(ctx, expr->op, a, b);
            lc_release(ctx, a); lc_release(ctx, b); return result;
        case E_INDEX:
            a = li_eval(frame, env, expr->a); b = li_eval(frame, env, expr->b);
            result = lc_table_get(ctx, a, b);
            lc_release(ctx, a); lc_release(ctx, b); return result;
        case E_TABLE: {
            int array_index = 1;
            result = lc_table(ctx);
            for (i = 0; i < expr->count; ++i) {
                Expr *key_expr = expr->keys ? expr->keys[i] : NULL;
                int expand = !key_expr && i == expr->count - 1;
                LcValue key = lc_nil(), value;
                if (key_expr) key = li_eval(frame, env, key_expr);
                else if (!expand) key = lc_number(array_index);
                value = li_eval(frame, env, expr->items[i]);
                if (expand) lc_table_add(ctx, result, array_index, value);
                else lc_table_set(ctx, result, key, value);
                lc_release(ctx, key); lc_release(ctx, value);
                if (!key_expr) array_index++;
            }
            return result;
        }
        case E_CALL: return li_eval_call(frame, env, expr);
        case E_FUNCTION: return li_closure(frame, env, expr);
        default: return lc_nil();
    }
}
static LcValue *li_eval_list(LiFrame *frame, LiEnv *env, Expr **expressions, int count) {
    LcValue *values = (LcValue *)xmalloc(sizeof(LcValue) * (size_t)(count ? count : 1));
    int i; for (i = 0; i < count; ++i) values[i] = li_eval(frame, env, expressions[i]);
    return values;
}
static void li_release_list(LcContext *ctx, LcValue *values, int count) {
    int i; for (i = 0; i < count; ++i) lc_release(ctx, values[i]);
    free(values);
}
static void li_assign_results(LiFrame *frame, LiEnv *env, Stmt *stmt, int declare) {
    LcContext *ctx = frame->session->ctx;
    int targets = declare ? stmt->nnames : stmt->flag;
    int offset = declare ? 0 : stmt->flag;
    int count = stmt->nexprs - offset, i;
    LcValue *values = li_eval_list(frame, env, stmt->exprs + offset, count);
    for (i = 0; i < targets; ++i) {
        int ri = i < count - 1 ? i : count - 1;
        int index = i < count - 1 ? 0 : i - (count - 1);
        LcValue value = ri < 0 ? lc_nil() : lc_nth(values[ri], index);
        if (declare) li_declare(frame, env, stmt->names[i], value);
        else li_assign(frame, env, stmt->exprs[i], value);
    }
    li_release_list(ctx, values, count);
}
static LiFlow li_exec_block(LiFrame *frame, LiEnv *parent, Stmt *stmt, LcValue *result) {
    LiEnv *env = li_env_new(parent);
    LiFlow flow;
    if (!env) { lc_error(frame->session->ctx, "out of memory"); return LI_NORMAL; }
    flow = li_exec_statements(frame, env, stmt, result);
    li_env_release(frame->session->ctx, env); return flow;
}
static LiFlow li_exec_statement(LiFrame *frame, LiEnv *env, Stmt *stmt, LcValue *result) {
    LiSession *session = frame->session;
    LcContext *ctx = session->ctx;
    LcValue value;
    int i;
    session->ctx->line = stmt->line;
    if (!li_tick(session)) return LI_NORMAL;
    switch (stmt->kind) {
        case S_LOCAL: li_assign_results(frame, env, stmt, 1); break;
        case S_LOCALFUNC:
            li_declare(frame, env, stmt->names[0], lc_nil());
            value = li_closure(frame, env, stmt->expr);
            lc_cell_set(ctx, li_local(env, stmt->names[0])->cell, value); break;
        case S_ASSIGN:
            if (!stmt->flag && stmt->expr && stmt->expr2) {
                value = li_eval(frame, env, stmt->expr2); li_assign(frame, env, stmt->expr, value);
            } else li_assign_results(frame, env, stmt, 0);
            break;
        case S_EXPR: value = li_eval(frame, env, stmt->expr); lc_release(ctx, value); break;
        case S_IF:
            for (i = 0; i < stmt->nblocks; ++i) {
                if (!stmt->conds[i]) return li_exec_block(frame, env, stmt->blocks[i], result);
                value = li_eval(frame, env, stmt->conds[i]);
                if (lc_truthy(lc_first_value(value))) {
                    LiFlow flow;
                    lc_release(ctx, value); flow = li_exec_block(frame, env, stmt->blocks[i], result); return flow;
                }
                lc_release(ctx, value);
            }
            break;
        case S_WHILE:
            for (;;) {
                LiFlow flow;
                value = li_eval(frame, env, stmt->expr);
                if (!lc_truthy(lc_first_value(value)) || ctx->error) { lc_release(ctx, value); break; }
                lc_release(ctx, value); flow = li_exec_block(frame, env, stmt->body, result);
                if (flow == LI_RETURN) return flow;
                if (flow == LI_BREAK || ctx->error) break;
            }
            break;
        case S_REPEAT:
            for (;;) {
                LiEnv *iteration = li_env_new(env);
                LiFlow flow = li_exec_statements(frame, iteration, stmt->body, result);
                value = flow == LI_NORMAL ? li_eval(frame, iteration, stmt->expr) : lc_bool(1);
                { int done = flow != LI_NORMAL || lc_truthy(lc_first_value(value)) || ctx->error;
                  lc_release(ctx, value); li_env_release(ctx, iteration);
                  if (flow == LI_RETURN) return flow;
                  if (done) break; }
            }
            break;
        case S_DO: return li_exec_block(frame, env, stmt->body, result);
        case S_FORNUM: {
            LcValue start = li_eval(frame, env, stmt->expr), limit = li_eval(frame, env, stmt->expr2);
            LcValue step = stmt->expr3 ? li_eval(frame, env, stmt->expr3) : lc_number(1);
            int ok1, ok2, ok3;
            double first = lc_to_number(ctx, start, &ok1), last = lc_to_number(ctx, limit, &ok2), delta = lc_to_number(ctx, step, &ok3), current;
            LiEnv *loop = li_env_new(env);
            li_declare(frame, loop, stmt->names[0], lc_number(first));
            for (current = first; (delta >= 0 ? current <= last : current >= last) && !ctx->error; current += delta) {
                LiFlow flow;
                lc_cell_set(ctx, li_local(loop, stmt->names[0])->cell, lc_number(current));
                flow = li_exec_block(frame, loop, stmt->body, result);
                if (flow == LI_RETURN) { li_env_release(ctx, loop); lc_release(ctx,start);lc_release(ctx,limit);lc_release(ctx,step);return flow; }
                if (flow == LI_BREAK) break;
            }
            li_env_release(ctx, loop); lc_release(ctx,start);lc_release(ctx,limit);lc_release(ctx,step); break;
        }
        case S_FORIN: {
            LcValue *raw = li_eval_list(frame, env, stmt->exprs, stmt->nexprs), values[3];
            LiEnv *loop = li_env_new(env);
            for (i = 0; i < 3; ++i) {
                int ri = i < stmt->nexprs - 1 ? i : stmt->nexprs - 1;
                int index = i < stmt->nexprs - 1 ? 0 : i - (stmt->nexprs - 1);
                values[i] = ri < 0 ? lc_nil() : lc_nth(raw[ri], index);
            }
            for (i = 0; i < stmt->nnames; ++i) li_declare(frame, loop, stmt->names[i], lc_nil());
            li_release_list(ctx, raw, stmt->nexprs);
            for (;;) {
                LcValue args[2], returned, first;
                LiFlow flow;
                args[0] = values[1]; args[1] = values[2];
                returned = lc_call_list(ctx, values[0], 2, args);
                first = lc_nth(returned, 0);
                if (first.type == LC_NIL || ctx->error) { lc_release(ctx,first);lc_release(ctx,returned);break; }
                lc_release(ctx, values[2]); values[2] = lc_retain(first); lc_release(ctx, first);
                for (i = 0; i < stmt->nnames; ++i) {
                    LcValue item = lc_nth(returned, i);
                    lc_cell_set(ctx, li_local(loop, stmt->names[i])->cell, item);
                }
                lc_release(ctx, returned);
                flow = li_exec_block(frame, loop, stmt->body, result);
                if (flow == LI_RETURN) { for(i=0;i<3;++i)lc_release(ctx,values[i]);li_env_release(ctx,loop);return flow; }
                if (flow == LI_BREAK || ctx->error) break;
            }
            for (i = 0; i < 3; ++i) lc_release(ctx, values[i]);
            li_env_release(ctx, loop); break;
        }
        case S_RETURN:
            if (!stmt->nexprs) *result = lc_nil();
            else if (stmt->nexprs == 1) *result = li_eval(frame, env, stmt->exprs[0]);
            else {
                LcValue *values = li_eval_list(frame, env, stmt->exprs, stmt->nexprs);
                *result = lc_tuple_list(ctx, stmt->nexprs, values);
                li_release_list(ctx, values, stmt->nexprs);
            }
            return LI_RETURN;
        case S_BREAK: return LI_BREAK;
        case S_LABEL: break;
        case S_GOTO: lc_error(ctx, "goto is not supported by the embedded project runner"); break;
        default: break;
    }
    return LI_NORMAL;
}
static LiFlow li_exec_statements(LiFrame *frame, LiEnv *env, Stmt *stmt, LcValue *result) {
    LiEnv *scope = li_env_new(env);
    LiFlow flow = LI_NORMAL;
    if (!scope) { lc_error(frame->session->ctx, "out of memory"); return flow; }
    for (; stmt && flow == LI_NORMAL && !frame->session->ctx->error; stmt = stmt->next)
        flow = li_exec_statement(frame, scope, stmt, result);
    li_env_release(frame->session->ctx, scope); return flow;
}
static char *li_read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    char *source; long size;
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    source = (char *)xmalloc((size_t)size + 1);
    if (fread(source, 1, (size_t)size, file) != (size_t)size) { fclose(file); free(source); return NULL; }
    fclose(file); source[size] = 0; if (length) *length = (size_t)size; return source;
}
/* Project search paths are relative to the project root. Reject absolute paths
   and lexical parent traversal (this is not a symlink security boundary). */
static int li_safe_search_path(const char *path) {
    const char *segment = path, *p;
    if (!*path || *path == '/' || strchr(path, '\\') || strchr(path, ':')) return 0;
    for (p = path;; ++p) {
        if (*p == '/' || !*p) {
            if (p - segment == 2 && segment[0] == '.' && segment[1] == '.') return 0;
            if (!*p) break;
            segment = p + 1;
        }
    }
    return 1;
}
static char *li_module_path(LiSession *session, const char *name, int main_file) {
    Str path, module_name;
    LcValue package, key, patterns;
    const char *start, *end;
    size_t i;
    if (main_file) {
        sb_init(&path); sb_add(&path, session->root_path); sb_add(&path, "/main.lua");
        return path.data;
    }
    if (!*name || name[0] == '.' || strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) return NULL;
    sb_init(&module_name);
    for (i = 0; name[i]; ++i) { char c = name[i] == '.' ? '/' : name[i]; sb_n(&module_name, &c, 1); }
    package = li_global(session, "package");
    key = lc_string(session->ctx, "path");
    patterns = lc_table_get(session->ctx, package, key);
    lc_release(session->ctx, key); lc_release(session->ctx, package);
    if (patterns.type != LC_STRING) {
        lc_release(session->ctx, patterns); sb_free(&module_name);
        lc_error(session->ctx, "package.path must be a string"); return NULL;
    }
    start = patterns.as.string->data;
    while (*start) {
        Str candidate;
        end = strchr(start, ';'); if (!end) end = start + strlen(start);
        sb_init(&candidate);
        for (; start < end; ++start) {
            if (*start == '?') sb_add(&candidate, module_name.data);
            else sb_n(&candidate, start, 1);
        }
        if (candidate.data && li_safe_search_path(candidate.data)) {
            sb_init(&path); sb_add(&path, session->root_path); sb_add(&path, "/"); sb_add(&path, candidate.data);
            sb_free(&candidate);
            if (access(path.data, R_OK) == 0) {
                lc_release(session->ctx, patterns); sb_free(&module_name); return path.data;
            }
            sb_free(&path);
        } else sb_free(&candidate);
        if (!*end) break;
        start = end + 1;
    }
    lc_release(session->ctx, patterns); sb_free(&module_name);
    return NULL;
}
static LiModule *li_parse_module(LiSession *session, const char *name, int main_file) {
    LiModule *module;
    char *path = li_module_path(session, name, main_file), *source;
    if (!path) { if (!session->ctx->error) lc_error(session->ctx, "module '%s' not found in project package.path", name); return NULL; }
    source = li_read_file(path, NULL);
    if (!source) { lc_error(session->ctx, "module '%s' not found at %s", main_file ? "main" : name, path); free(path); return NULL; }
    module = (LiModule *)calloc(1, sizeof(*module));
    if (!module) { free(source);free(path);lc_error(session->ctx,"out of memory");return NULL; }
    module->name = path; module->source = source;
    api_parse(&module->parser, source, path, &module->program);
    module->next = session->modules; session->modules = module;
    if (module->parser.failed) lc_error(session->ctx, "%s", module->parser.error);
    return module;
}
/* Compile text into an interpreted, vararg chunk. Keep its AST in the session
   until close: returned functions and nested closures can outlive the call. */
static LcValue li_load_text(LiSession *session, const char *source, size_t length, const char *name) {
    LcContext *ctx = session->ctx;
    LiModule *module;
    LiFrame frame;
    if (memchr(source, '\0', length)) {
        LcValue values[2] = {lc_nil(), lc_string(ctx, "binary chunks and embedded NUL bytes are not supported")};
        return lc_tuple_new(ctx, 2, values, 1);
    }
    module = (LiModule *)calloc(1, sizeof(*module));
    if (!module) { lc_error(ctx, "out of memory"); return lc_nil(); }
    module->source = xstrndup(source, length);
    module->name = xstrdup(name);
    api_parse(&module->parser, module->source, module->name, &module->program);
    if (module->parser.failed) {
        LcValue values[2] = {lc_nil(), lc_string(ctx, module->parser.error)};
        token_clear(&module->parser.token); free_stmt(module->program);
        free(module->source); free(module->name); free(module);
        return lc_tuple_new(ctx, 2, values, 1);
    }
    module->chunk = (Expr *)calloc(1, sizeof(Expr));
    if (!module->chunk) {
        token_clear(&module->parser.token); free_stmt(module->program);
        free(module->source); free(module->name); free(module);
        lc_error(ctx, "out of memory"); return lc_nil();
    }
    module->chunk->kind = E_FUNCTION; module->chunk->flag = 1;
    module->chunk->body = module->program;
    module->next = session->modules; session->modules = module;
    frame.session = session; frame.varargs = lc_nil();
    return li_closure(&frame, session->root, module->chunk);
}
static LcValue li_load_error(LcContext *ctx, const char *message) {
    LcValue values[2] = {lc_nil(), lc_string(ctx, message)};
    return lc_tuple_new(ctx, 2, values, 1);
}
static LcValue li_builtin_load(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LiSession *session = li_current;
    LcValue source, arg;
    const char *name = "=(load)";
    int mode_index = 2;
    (void)self;
    if (!argc || (source = lc_first_value(argv[0])).type != LC_STRING) {
        lc_error(ctx, "load currently requires a source string"); return lc_nil();
    }
    if (argc > 1 && (arg = lc_first_value(argv[1])).type != LC_NIL) {
        if (arg.type != LC_STRING) { lc_error(ctx, "load chunk name must be a string"); return lc_nil(); }
        name = arg.as.string->data;
    }
    if (argc > mode_index && (arg = lc_first_value(argv[mode_index])).type != LC_NIL) {
        if (arg.type != LC_STRING) { lc_error(ctx, "load mode must be a string"); return lc_nil(); }
        if (!strchr(arg.as.string->data, 't')) return li_load_error(ctx, "text chunk rejected by mode");
    }
    if (argc > 3 && lc_first_value(argv[3]).type != LC_NIL) {
        lc_error(ctx, "load with a custom environment is not supported"); return lc_nil();
    }
    return li_load_text(session, source.as.string->data, source.as.string->length, name);
}
static LcValue li_builtin_loadfile(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcValue file, mode, result;
    char *source; size_t length;
    (void)self;
    if (!argc || (file = lc_first_value(argv[0])).type != LC_STRING) {
        lc_error(ctx, "loadfile currently requires a filename"); return lc_nil();
    }
    if (argc > 1 && (mode = lc_first_value(argv[1])).type != LC_NIL) {
        if (mode.type != LC_STRING) { lc_error(ctx, "loadfile mode must be a string"); return lc_nil(); }
        if (!strchr(mode.as.string->data, 't')) return li_load_error(ctx, "text chunk rejected by mode");
    }
    if (argc > 2 && lc_first_value(argv[2]).type != LC_NIL) {
        lc_error(ctx, "loadfile with a custom environment is not supported"); return lc_nil();
    }
    if (strlen(file.as.string->data) != file.as.string->length) return li_load_error(ctx, "filename contains NUL");
    source = li_read_file(file.as.string->data, &length);
    if (!source) {
        char message[512]; snprintf(message, sizeof(message), "cannot open file '%s'", file.as.string->data);
        return li_load_error(ctx, message);
    }
    result = li_load_text(li_current, source, length, file.as.string->data);
    free(source); return result;
}
static LcValue li_builtin_dofile(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcValue loaded = li_builtin_loadfile(ctx, self, argc ? 1 : 0, argv);
    LcValue function = lc_first(loaded), result;
    if (function.type != LC_FUNCTION) {
        if (!ctx->error) {
            LcValue message = lc_nth(loaded, 1);
            lc_error(ctx, "%s", message.type == LC_STRING ? message.as.string->data : "cannot load file");
            lc_release(ctx, message);
        }
        lc_release(ctx, function); lc_release(ctx, loaded); return lc_nil();
    }
    result = lc_call(ctx, function, 0, NULL);
    lc_release(ctx, function); lc_release(ctx, loaded); return result;
}
static LcValue li_execute_module(LiSession *session, LiModule *module) {
    LiEnv *env = li_env_new(NULL);
    LiFrame frame;
    LcValue result = lc_nil();
    if (!env) { lc_error(session->ctx,"out of memory");return result; }
    frame.session = session; frame.varargs = lc_nil();
    if (!session->ctx->error) li_exec_statements(&frame, env, module->program, &result);
    lc_release(session->ctx, frame.varargs); li_env_release(session->ctx, env);
    return result;
}
static LcValue li_require(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LiSession *session = li_current;
    const char *name;
    LcValue loaded, key, result, preload, loader;
    LiModule *module;
    if (!argc || lc_first_value(argv[0]).type != LC_STRING) { lc_error(ctx,"module name expected");return lc_nil(); }
    if (lc_native_module_name(lc_first_value(argv[0]).as.string->data))
        return lc_builtin_require(ctx, self, argc, argv);
    name = lc_first_value(argv[0]).as.string->data;
    loaded = lc_package_table(ctx, "loaded"); key = lc_string(ctx, name);
    result = lc_table_get(ctx, loaded, key);
    if (lc_truthy(result)) { lc_release(ctx,loaded);lc_release(ctx,key);return result; }
    lc_release(ctx, result);
    /* Project require must honor package.preload just like the native runner. */
    preload = lc_package_table(ctx, "preload");
    loader = lc_table_get(ctx, preload, key);
    lc_release(ctx, preload);
    if (loader.type == LC_FUNCTION) {
        lc_release(ctx, loader); lc_release(ctx, loaded); lc_release(ctx, key);
        return lc_builtin_require(ctx, self, argc, argv);
    }
    lc_release(ctx, loader);
    lc_table_set(ctx, loaded, key, lc_bool(1));
    module = li_parse_module(session, name, 0);
    result = module ? li_execute_module(session, module) : lc_nil();
    if (ctx->error) lc_table_set(ctx, loaded, key, lc_nil());
    else if (result.type != LC_NIL) lc_table_set(ctx, loaded, key, result);
    lc_release(ctx, result);
    result = lc_table_get(ctx, loaded, key);
    if (!ctx->error && result.type == LC_NIL) {
        lc_release(ctx, result); result = lc_bool(1);
        lc_table_set(ctx, loaded, key, result);
    }
    lc_release(ctx, loaded); lc_release(ctx, key); return result;
}
static void li_json_string(Str *json, const char *text) {
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    sb_add(json, "\"");
    for (; *p; ++p) {
        char escaped[7];
        if (*p == '\"' || *p == '\\') { char pair[2] = {'\\', (char)*p}; sb_n(json,pair,2); }
        else if (*p == '\n') sb_add(json,"\\n");
        else if (*p == '\r') sb_add(json,"\\r");
        else if (*p == '\t') sb_add(json,"\\t");
        else if (*p < 32) { snprintf(escaped,sizeof(escaped),"\\u%04x",*p);sb_add(json,escaped); }
        else sb_n(json,(const char *)p,1);
    }
    sb_add(json, "\"");
}
static LcValue li_field(LcContext *ctx, LcValue table, const char *name) {
    LcValue key = lc_string(ctx,name), value = lc_table_raw_get(ctx,table,key);
    lc_release(ctx,key); return value;
}
static double li_number_field(LcContext *ctx, LcValue table, const char *name, double fallback) {
    LcValue original = li_field(ctx,table,name);
    LcValue value = lc_first_value(original);
    double number = fallback;
    if (value.type == LC_NUMBER) number = value.as.number;
    else if (value.type == LC_INTEGER) number = (double)value.as.integer;
    lc_release(ctx,original);
    return number;
}
#include "luac_love.h"

static void li_json_scene(LiSession *session, Str *json) {
    LcContext *ctx = session->ctx;
    LcValue world = li_global(session,"world"), entities, camera, overlay;
    int i, first = 1;
    sb_add(json,",\"entities\":[");
    if (world.type == LC_TABLE) {
        entities = li_field(ctx,world,"entities");
        if (entities.type == LC_TABLE) for (i = 0; i < (int)entities.as.table->capacity; ++i) {
            LcValue entity, active, texture, color;
            if (!entities.as.table->entries[i].used) continue;
            entity = entities.as.table->entries[i].value;
            if (entity.type != LC_TABLE) continue;
            active = li_field(ctx,entity,"active");
            if (!lc_truthy(active)) { lc_release(ctx,active);continue; }
            lc_release(ctx,active);
            if (!first) sb_add(json,","); first = 0;
            sb_add(json,"{");sb_add(json,"\"x\":");sb_printf(json,"%.8g",li_number_field(ctx,entity,"x",0));
            sb_add(json,",\"y\":");sb_printf(json,"%.8g",li_number_field(ctx,entity,"y",0));
            sb_add(json,",\"width\":");sb_printf(json,"%.8g",li_number_field(ctx,entity,"width",32));
            sb_add(json,",\"height\":");sb_printf(json,"%.8g",li_number_field(ctx,entity,"height",32));
            sb_add(json,",\"rotation\":");sb_printf(json,"%.8g",li_number_field(ctx,entity,"rotation",0));
            sb_add(json,",\"scale\":");sb_printf(json,"%.8g",li_number_field(ctx,entity,"scale",1));
            texture = li_field(ctx,entity,"texture");
            sb_add(json,",\"texture\":");
            if (texture.type == LC_TABLE) { LcValue path=li_field(ctx,texture,"path");if(path.type==LC_STRING)li_json_string(json,path.as.string->data);else sb_add(json,"null");lc_release(ctx,path); }
            else sb_add(json,"null");
            lc_release(ctx,texture);
            color = li_field(ctx,entity,"color"); sb_add(json,",\"color\":[");
            if (color.type == LC_TABLE) {
                int c; for(c=1;c<=4;++c){LcValue channel=lc_table_raw_get(ctx,color,lc_integer(c));double n=lc_first_value(channel).type==LC_NUMBER?lc_first_value(channel).as.number:lc_first_value(channel).type==LC_INTEGER?(double)lc_first_value(channel).as.integer:1;if(c>1)sb_add(json,",");sb_printf(json,"%.4g",n);lc_release(ctx,channel);}
            } else sb_add(json,"1,1,1,1");
            sb_add(json,"]}"); lc_release(ctx,color);
        }
        lc_release(ctx,entities);
    }
    sb_add(json,"],\"camera\":");
    camera = world.type == LC_TABLE ? li_field(ctx,world,"camera") : lc_nil();
    if (camera.type == LC_TABLE) {
        sb_add(json,"{\"x\":");sb_printf(json,"%.8g",li_number_field(ctx,camera,"x",0));
        sb_add(json,",\"y\":");sb_printf(json,"%.8g",li_number_field(ctx,camera,"y",0));
        sb_add(json,",\"zoom\":");sb_printf(json,"%.8g",li_number_field(ctx,camera,"zoom",1));sb_add(json,"}");
    } else sb_add(json,"{\"x\":0,\"y\":0,\"zoom\":1}");
    lc_release(ctx,camera);
    sb_add(json,",\"texts\":[");
    overlay = world.type == LC_TABLE ? li_field(ctx,world,"overlay_text") : lc_nil();
    if (overlay.type == LC_TABLE) {
        int index; first = 1;
        for (index = 1; index <= lc_table_length(overlay); ++index) {
            LcValue item = lc_table_raw_get(ctx,overlay,lc_integer(index)), text;
            if (item.type != LC_TABLE) { lc_release(ctx,item);continue; }
            text = li_field(ctx,item,"text"); if(!first)sb_add(json,",");first=0;
            sb_add(json,"{\"text\":");if(text.type==LC_STRING)li_json_string(json,text.as.string->data);else li_json_string(json,"");
            sb_add(json,",\"x\":");sb_printf(json,"%.8g",li_number_field(ctx,item,"x",0));
            sb_add(json,",\"y\":");sb_printf(json,"%.8g",li_number_field(ctx,item,"y",0));
            sb_add(json,",\"size\":");sb_printf(json,"%.8g",li_number_field(ctx,item,"size",16));sb_add(json,"}");
            lc_release(ctx,text);lc_release(ctx,item);
        }
    }
    lc_release(ctx,overlay);lc_release(ctx,world);sb_add(json,"]");
}
static char *li_project_frame(LiSession *session, double dt) {
    LcContext *ctx = session->ctx;
    Str json;
    session->budget = 250000;
    if (!ctx->error && dt > 0) {
        LcValue update = li_global(session,"update");
        if (update.type == LC_FUNCTION) {
            LcValue argument = lc_number(dt), ignored = lc_call(ctx,update,1,&argument);
            lc_release(ctx,ignored);
        }
        lc_release(ctx,update);
        if (!ctx->error) {
            LcValue world = li_global(session,"world"), engine = li_global(session,"engine");
            if (world.type == LC_TABLE && engine.type == LC_TABLE) {
                LcValue method = li_field(ctx,engine,"update");
                if (method.type == LC_FUNCTION) { LcValue args[2];args[0]=world;args[1]=lc_number(dt);{LcValue ignored=lc_call(ctx,method,2,args);lc_release(ctx,ignored);} }
                lc_release(ctx,method);
            }
            lc_release(ctx,world);lc_release(ctx,engine);
        }
    }
    if (!ctx->error) li_love_frame(session, dt);
    sb_init(&json);sb_add(&json,"{\"ok\":");sb_add(&json,ctx->error?"false":"true");
    sb_add(&json,",\"output\":");
    if (ctx->captured_output && session->output_offset < ctx->captured_length)
        li_json_string(&json,ctx->captured_output + session->output_offset);
    else li_json_string(&json,"");
    session->output_offset = ctx->captured_length;
    sb_add(&json,",\"error\":");
    if (ctx->error) { Str message;sb_init(&message);sb_printf(&message,"%s:%d: %s",ctx->source?ctx->source:"project",ctx->line,ctx->message);li_json_string(&json,message.data);sb_free(&message); }
    else sb_add(&json,"null");
    li_json_scene(session,&json);li_love_json(session, &json);sb_add(&json,"}");
    return json.data;
}

LUAC_API void *luac_project_open(const char *root_path, char **error_message) {
    LiSession *session;
    LiModule *main_module;
    LcValue require;
    char cwd[PATH_MAX];
    if (error_message) *error_message = NULL;
    if (!root_path || !*root_path) { if(error_message)*error_message=xstrdup("project path required");return NULL; }
    session = (LiSession *)calloc(1,sizeof(*session));
    if (!session) { if(error_message)*error_message=xstrdup("out of memory");return NULL; }
    session->previous_cwd = getcwd(cwd,sizeof(cwd)) ? xstrdup(cwd) : NULL;
    if (root_path[0] == '/' || root_path[0] == '\\' || (root_path[0] && root_path[1] == ':') || !session->previous_cwd) session->root_path = xstrdup(root_path);
    else { Str absolute; sb_init(&absolute); sb_add(&absolute, session->previous_cwd); sb_add(&absolute, "/"); sb_add(&absolute, root_path); session->root_path = absolute.data; }
    if (chdir(root_path) != 0) {
        if(error_message){Str message;sb_init(&message);sb_printf(&message,"cannot open project: %s",strerror(errno));*error_message=message.data;}
        free(session->previous_cwd);free(session->root_path);free(session);return NULL;
    }
    session->ctx = lc_context_new(session->root_path); session->ctx->capture_output = 1;
    li_love_init(session);
    session->root = li_env_new(NULL); session->budget = 1000000; li_current = session;
    require = lc_function_new(session->ctx,li_require,0,NULL,"project require");
    li_set_global(session,"require",require);
    li_set_global(session,"load",lc_function_new(session->ctx,li_builtin_load,0,NULL,"load"));
    li_set_global(session,"loadstring",lc_function_new(session->ctx,li_builtin_load,0,NULL,"loadstring"));
    li_set_global(session,"loadfile",lc_function_new(session->ctx,li_builtin_loadfile,0,NULL,"loadfile"));
    li_set_global(session,"dofile",lc_function_new(session->ctx,li_builtin_dofile,0,NULL,"dofile"));
    { LcValue package = li_global(session, "package");
      lc_set_field(session->ctx, package, "path", lc_string(session->ctx, "./?.lua;./?/init.lua"));
      lc_release(session->ctx, package); }
    main_module = li_parse_module(session,"main",1);
    if (main_module && !session->ctx->error) {
        LcValue ignored = li_execute_module(session,main_module);lc_release(session->ctx,ignored);
        if (!session->ctx->error) li_love_call(session, "load", 0, NULL);
    }
    return session;
}
LUAC_API char *luac_project_step(void *handle, double dt) {
    LiSession *session = (LiSession *)handle;
    if (!session) return xstrdup("{\"ok\":false,\"error\":\"project is not running\",\"output\":\"\",\"entities\":[],\"texts\":[]}");
    li_current = session; return li_project_frame(session,dt);
}
LUAC_API void luac_project_touch(void *handle, int id, double x, double y, int down) {
    LiSession *session = (LiSession *)handle;
    LcValue world,engine,method,args[5],ignored;
    if (!session || session->ctx->error) return;
    li_current = session;
    li_love_touch(session, id, x, y, down);
    world=li_global(session,"world");engine=li_global(session,"engine");
    if(world.type==LC_TABLE&&engine.type==LC_TABLE){method=li_field(session->ctx,engine,"inject_touch");if(method.type==LC_FUNCTION){args[0]=world;args[1]=lc_integer(id);args[2]=lc_number(x);args[3]=lc_number(y);args[4]=lc_bool(down);ignored=lc_call(session->ctx,method,5,args);lc_release(session->ctx,ignored);}lc_release(session->ctx,method);}
    lc_release(session->ctx,world);lc_release(session->ctx,engine);
}
LUAC_API void luac_project_close(void *handle) {
    LiSession *session = (LiSession *)handle;
    LiFunction *function,*next_function;
    LiModule *module,*next_module;
    if (!session) return;
    li_current = session;
    for(function=session->functions;function;function=next_function){next_function=function->next;li_env_release(session->ctx,function->closure);free(function);}
    li_env_release(session->ctx,session->root);
    for(module=session->modules;module;module=next_module){next_module=module->next;token_clear(&module->parser.token);free_stmt(module->program);free(module->chunk);free(module->source);free(module->name);free(module);}
    li_love_clear(session);
    free(session->draw_commands);
    lc_context_destroy(session->ctx);
    if(session->previous_cwd)chdir(session->previous_cwd);
    free(session->previous_cwd);free(session->root_path);free(session);li_current=NULL;
}

#endif
