#ifndef LUAC_RUNTIME_H
#define LUAC_RUNTIME_H

/* LuaC runtime: deterministic ownership, no tracing garbage collector. */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct LcContext LcContext;
typedef struct LcObject LcObject;
typedef struct LcString LcString;
typedef struct LcTable LcTable;
typedef struct LcFunction LcFunction;
typedef struct LcTuple LcTuple;
typedef struct LcCell LcCell;

typedef enum {
    LC_NIL = 0, LC_BOOL, LC_NUMBER, LC_STRING, LC_TABLE, LC_FUNCTION, LC_TUPLE
} LcType;

typedef struct LcValue {
    LcType type;
    union {
        int boolean;
        double number;
        LcString *string;
        LcTable *table;
        LcFunction *function;
        LcTuple *tuple;
    } as;
} LcValue;

typedef LcValue (*LcNative)(LcContext *, LcFunction *, int, LcValue *);

struct LcObject {
    size_t refs;
    int kind;
    LcObject *next;
};

struct LcString {
    LcObject object;
    size_t length;
    char data[];
};

typedef struct {
    LcValue key;
    LcValue value;
    int used;
} LcEntry;

struct LcTable {
    LcObject object;
    LcEntry *entries;
    size_t length;
    size_t capacity;
};

struct LcFunction {
    LcObject object;
    LcNative native;
    LcCell **upvalues;
    int upvalue_count;
    const char *name;
};

struct LcTuple {
    LcObject object;
    int count;
    LcValue items[];
};

struct LcCell {
    LcObject object;
    LcValue value;
};

struct LcContext {
    LcObject *objects;
    LcValue globals;
    int error;
    int line;
    const char *source;
    char message[1024];
};

static LcValue lc_nil(void) { LcValue v; memset(&v, 0, sizeof(v)); return v; }
static LcValue lc_bool(int b) { LcValue v = lc_nil(); v.type = LC_BOOL; v.as.boolean = !!b; return v; }
static LcValue lc_number(double n) { LcValue v = lc_nil(); v.type = LC_NUMBER; v.as.number = n; return v; }
static int lc_truthy(LcValue v) { return v.type != LC_NIL && !(v.type == LC_BOOL && !v.as.boolean); }
static int lc_is_multi(LcValue v) { return v.type == LC_TUPLE; }

static void lc_error(LcContext *ctx, const char *fmt, ...) {
    va_list ap;
    if (ctx->error) return;
    ctx->error = 1;
    va_start(ap, fmt);
    vsnprintf(ctx->message, sizeof(ctx->message), fmt, ap);
    va_end(ap);
}

static void *lc_alloc(LcContext *ctx, size_t size, int kind) {
    LcObject *o = (LcObject *)calloc(1, size);
    if (!o) { lc_error(ctx, "out of memory"); return NULL; }
    o->refs = 1;
    o->kind = kind;
    o->next = ctx->objects;
    ctx->objects = o;
    return o;
}

static LcObject *lc_value_object(LcValue v) {
    switch (v.type) {
        case LC_STRING: return (LcObject *)v.as.string;
        case LC_TABLE: return (LcObject *)v.as.table;
        case LC_FUNCTION: return (LcObject *)v.as.function;
        case LC_TUPLE: return (LcObject *)v.as.tuple;
        default: return NULL;
    }
}

static LcValue lc_retain(LcValue v) {
    LcObject *o = lc_value_object(v);
    if (o) o->refs++;
    return v;
}

static void lc_release(LcContext *ctx, LcValue v);

static void lc_unlink(LcContext *ctx, LcObject *o) {
    LcObject **p = &ctx->objects;
    while (*p && *p != o) p = &(*p)->next;
    if (*p == o) *p = o->next;
}

static void lc_cell_release(LcContext *ctx, LcCell *cell) {
    if (!cell) return;
    if (--cell->object.refs == 0) {
        lc_unlink(ctx, (LcObject *)cell);
        lc_release(ctx, cell->value);
        free(cell);
    }
}

static void lc_free_value(LcContext *ctx, LcValue v) {
    LcObject *o = lc_value_object(v);
    int i;
    if (!o) return;
    lc_unlink(ctx, o);
    switch (v.type) {
        case LC_TABLE:
            for (i = 0; i < (int)v.as.table->capacity; ++i) {
                if (v.as.table->entries[i].used) {
                    lc_release(ctx, v.as.table->entries[i].key);
                    lc_release(ctx, v.as.table->entries[i].value);
                }
            }
            free(v.as.table->entries);
            break;
        case LC_FUNCTION:
            for (i = 0; i < v.as.function->upvalue_count; ++i)
                lc_cell_release(ctx, v.as.function->upvalues[i]);
            free(v.as.function->upvalues);
            break;
        case LC_TUPLE:
            for (i = 0; i < v.as.tuple->count; ++i)
                lc_release(ctx, v.as.tuple->items[i]);
            break;
        default: break;
    }
    free(o);
}

static void lc_release(LcContext *ctx, LcValue v) {
    LcObject *o = lc_value_object(v);
    if (o && --o->refs == 0) lc_free_value(ctx, v);
}

static LcValue lc_string_n(LcContext *ctx, const char *s, size_t n) {
    LcValue v = lc_nil();
    LcString *str = (LcString *)lc_alloc(ctx, sizeof(*str) + n + 1, LC_STRING);
    if (!str) return v;
    str->length = n;
    memcpy(str->data, s, n);
    str->data[n] = '\0';
    v.type = LC_STRING;
    v.as.string = str;
    return v;
}

static LcValue lc_string(LcContext *ctx, const char *s) { return lc_string_n(ctx, s, strlen(s)); }

static LcValue lc_table(LcContext *ctx) {
    LcValue v = lc_nil();
    LcTable *t = (LcTable *)lc_alloc(ctx, sizeof(*t), LC_TABLE);
    if (!t) return v;
    v.type = LC_TABLE;
    v.as.table = t;
    return v;
}

static LcCell *lc_cell_new(LcContext *ctx, LcValue value) {
    LcCell *cell = (LcCell *)lc_alloc(ctx, sizeof(*cell), 100);
    if (!cell) { lc_release(ctx, value); return NULL; }
    cell->value = value;
    return cell;
}

static void lc_cell_replace(LcContext *ctx, LcCell **slot, LcValue value) {
    if (*slot) lc_cell_release(ctx, *slot);
    *slot = lc_cell_new(ctx, value);
}

static void lc_cell_set(LcContext *ctx, LcCell *cell, LcValue value) {
    LcValue old;
    if (!cell) { lc_release(ctx, value); lc_error(ctx, "invalid local variable"); return; }
    old = cell->value;
    cell->value = value;
    lc_release(ctx, old);
}

static LcValue lc_cell_get(LcCell *cell) { return cell ? lc_retain(cell->value) : lc_nil(); }

static LcValue lc_tuple_new(LcContext *ctx, int count, LcValue *values, int consume) {
    LcValue v = lc_nil();
    LcTuple *t;
    int i;
    if (count < 0) count = 0;
    t = (LcTuple *)lc_alloc(ctx, sizeof(*t) + sizeof(LcValue) * (size_t)count, LC_TUPLE);
    if (!t) return v;
    t->count = count;
    for (i = 0; i < count; ++i) t->items[i] = consume ? values[i] : lc_retain(values[i]);
    v.type = LC_TUPLE;
    v.as.tuple = t;
    return v;
}

static LcValue lc_first_value(LcValue v) {
    if (v.type == LC_TUPLE) return v.as.tuple->count ? v.as.tuple->items[0] : lc_nil();
    return v;
}

static LcValue lc_first(LcValue v) { return lc_retain(lc_first_value(v)); }

static LcValue lc_tuple_list(LcContext *ctx, int count, LcValue *values) {
    if (count > 0 && values[count - 1].type == LC_TUPLE) {
        LcTuple *last = values[count - 1].as.tuple;
        int n = count - 1 + last->count, i;
        LcValue *items = (LcValue *)malloc(sizeof(LcValue) * (size_t)(n ? n : 1));
        LcValue result;
        if (!items) { lc_error(ctx, "out of memory"); return lc_nil(); }
        for (i = 0; i < count - 1; ++i) items[i] = lc_first(values[i]);
        for (i = 0; i < last->count; ++i) items[count - 1 + i] = lc_retain(last->items[i]);
        result = lc_tuple_new(ctx, n, items, 1);
        free(items);
        return result;
    }
    return lc_tuple_new(ctx, count, values, 0);
}

/* Return an owned nth result without consuming the tuple. */
static LcValue lc_nth(LcValue v, int index) {
    if (v.type == LC_TUPLE) {
        if (index < 0 || index >= v.as.tuple->count) return lc_nil();
        return lc_retain(v.as.tuple->items[index]);
    }
    return index == 0 ? lc_retain(v) : lc_nil();
}

static int lc_value_equal(LcValue a, LcValue b) {
    a = lc_first_value(a); b = lc_first_value(b);
    if (a.type != b.type) return 0;
    switch (a.type) {
        case LC_NIL: return 1;
        case LC_BOOL: return a.as.boolean == b.as.boolean;
        case LC_NUMBER: return a.as.number == b.as.number;
        case LC_STRING:
            return a.as.string->length == b.as.string->length &&
                !memcmp(a.as.string->data, b.as.string->data, a.as.string->length);
        case LC_TABLE: return a.as.table == b.as.table;
        case LC_FUNCTION: return a.as.function == b.as.function;
        default: return 0;
    }
}

static int lc_table_find(LcTable *t, LcValue key) {
    size_t i;
    key = lc_first_value(key);
    if (key.type == LC_NIL) return -1;
    for (i = 0; i < t->capacity; ++i)
        if (t->entries[i].used && lc_value_equal(t->entries[i].key, key)) return (int)i;
    return -1;
}

static int lc_table_reserve(LcContext *ctx, LcTable *t, size_t needed) {
    LcEntry *p;
    size_t cap;
    if (needed <= t->capacity) return 1;
    cap = t->capacity ? t->capacity * 2 : 8;
    while (cap < needed) cap *= 2;
    p = (LcEntry *)realloc(t->entries, cap * sizeof(*p));
    if (!p) { lc_error(ctx, "out of memory growing table"); return 0; }
    memset(p + t->capacity, 0, (cap - t->capacity) * sizeof(*p));
    t->entries = p;
    t->capacity = cap;
    return 1;
}

static void lc_table_set(LcContext *ctx, LcValue table, LcValue key, LcValue value) {
    LcTable *t;
    int at;
    table = lc_first_value(table); key = lc_first_value(key); value = lc_first_value(value);
    if (table.type != LC_TABLE) { lc_error(ctx, "attempt to index a %s value", table.type == LC_NIL ? "nil" : "non-table"); return; }
    if (key.type == LC_NIL) { lc_error(ctx, "table index is nil"); return; }
    if (key.type == LC_NUMBER && isnan(key.as.number)) { lc_error(ctx, "table index is NaN"); return; }
    t = table.as.table;
    at = lc_table_find(t, key);
    if (value.type == LC_NIL) {
        if (at >= 0) {
            lc_release(ctx, t->entries[at].key);
            lc_release(ctx, t->entries[at].value);
            memset(&t->entries[at], 0, sizeof(t->entries[at]));
        }
        return;
    }
    if (at < 0) {
        if (!lc_table_reserve(ctx, t, t->length + 1)) return;
        at = (int)t->length++;
        t->entries[at].used = 1;
        t->entries[at].key = lc_retain(key);
    }
    value = lc_retain(value);
    lc_release(ctx, t->entries[at].value);
    t->entries[at].value = value;
}

static LcValue lc_table_get(LcContext *ctx, LcValue table, LcValue key) {
    int at;
    (void)ctx;
    table = lc_first_value(table); key = lc_first_value(key);
    if (table.type != LC_TABLE) { lc_error(ctx, "attempt to index a non-table value"); return lc_nil(); }
    at = lc_table_find(table.as.table, key);
    return at < 0 ? lc_nil() : lc_retain(table.as.table->entries[at].value);
}

static void lc_table_add(LcContext *ctx, LcValue table, int index, LcValue value) {
    if (value.type == LC_TUPLE) {
        int i;
        for (i = 0; i < value.as.tuple->count; ++i)
            lc_table_set(ctx, table, lc_number(index + i), value.as.tuple->items[i]);
    } else {
        lc_table_set(ctx, table, lc_number(index), value);
    }
}

static int lc_table_length(LcValue table) {
    int n = 0;
    if (table.type != LC_TABLE) return 0;
    for (;;) {
        LcValue k = lc_number((double)n + 1);
        if (lc_table_find(table.as.table, k) < 0) break;
        ++n;
    }
    return n;
}

static LcValue lc_function_new(LcContext *ctx, LcNative native, int n, LcCell **upvalues, const char *name) {
    LcValue v = lc_nil();
    LcFunction *f = (LcFunction *)lc_alloc(ctx, sizeof(*f), LC_FUNCTION);
    int i;
    if (!f) return v;
    f->native = native;
    f->name = name;
    f->upvalue_count = n;
    if (n) {
        f->upvalues = (LcCell **)malloc(sizeof(LcCell *) * (size_t)n);
        if (!f->upvalues) {
            f->upvalue_count = 0;
            v.type = LC_FUNCTION; v.as.function = f;
            lc_release(ctx, v);
            lc_error(ctx, "out of memory");
            return lc_nil();
        }
        for (i = 0; i < n; ++i) { f->upvalues[i] = upvalues[i]; upvalues[i]->object.refs++; }
    }
    v.type = LC_FUNCTION;
    v.as.function = f;
    return v;
}

static LcValue lc_call(LcContext *ctx, LcValue fn, int argc, LcValue *argv) {
    fn = lc_first_value(fn);
    if (ctx->error) return lc_nil();
    if (fn.type != LC_FUNCTION || !fn.as.function->native) {
        lc_error(ctx, "attempt to call a non-function value");
        return lc_nil();
    }
    return fn.as.function->native(ctx, fn.as.function, argc, argv);
}

/* Lua expands only the final expression of an argument list. */
static LcValue lc_call_list(LcContext *ctx, LcValue fn, int argc, LcValue *argv) {
    LcValue result;
    if (argc > 0 && argv[argc - 1].type == LC_TUPLE) {
        LcTuple *t = argv[argc - 1].as.tuple;
        int n = argc - 1 + t->count, i;
        LcValue *a = (LcValue *)malloc(sizeof(LcValue) * (size_t)(n ? n : 1));
        if (!a) { lc_error(ctx, "out of memory in call"); return lc_nil(); }
        for (i = 0; i < argc - 1; ++i) a[i] = lc_first_value(argv[i]);
        for (i = 0; i < t->count; ++i) a[argc - 1 + i] = t->items[i];
        result = lc_call(ctx, fn, n, a);
        free(a);
        return result;
    }
    return lc_call(ctx, fn, argc, argv);
}

static double lc_to_number(LcContext *ctx, LcValue v, int *ok) {
    char *end;
    v = lc_first_value(v);
    *ok = 1;
    if (v.type == LC_NUMBER) return v.as.number;
    if (v.type == LC_STRING) {
        double n;
        errno = 0;
        n = strtod(v.as.string->data, &end);
        while (isspace((unsigned char)*end)) ++end;
        if (end != v.as.string->data && !*end && errno != ERANGE) return n;
    }
    *ok = 0;
    lc_error(ctx, "number expected");
    return 0;
}

static LcValue lc_to_string(LcContext *ctx, LcValue v) {
    char buf[128];
    v = lc_first_value(v);
    switch (v.type) {
        case LC_NIL: return lc_string(ctx, "nil");
        case LC_BOOL: return lc_string(ctx, v.as.boolean ? "true" : "false");
        case LC_NUMBER:
            snprintf(buf, sizeof(buf), "%.14g", v.as.number);
            return lc_string(ctx, buf);
        case LC_STRING: return lc_retain(v);
        case LC_TABLE:
            snprintf(buf, sizeof(buf), "table: %p", (void *)v.as.table);
            return lc_string(ctx, buf);
        case LC_FUNCTION:
            snprintf(buf, sizeof(buf), "function: %p", (void *)v.as.function);
            return lc_string(ctx, buf);
        default: return lc_string(ctx, "nil");
    }
}

static const char *lc_type_name(LcValue v) {
    static const char *names[] = {"nil", "boolean", "number", "string", "table", "function", "tuple"};
    v = lc_first_value(v);
    return names[(int)v.type];
}

static LcValue lc_binary(LcContext *ctx, int op, LcValue a, LcValue b) {
    int ok1, ok2;
    double x, y;
    LcValue r = lc_nil();
    a = lc_first_value(a); b = lc_first_value(b);
    switch (op) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6:
            x = lc_to_number(ctx, a, &ok1); y = lc_to_number(ctx, b, &ok2);
            if (!ok1 || !ok2) return r;
            if (op == 0) return lc_number(x + y);
            if (op == 1) return lc_number(x - y);
            if (op == 2) return lc_number(x * y);
            if (op == 3) { if (y == 0) { lc_error(ctx, "division by zero"); return r; } return lc_number(x / y); }
            if (op == 4) { if (y == 0) { lc_error(ctx, "division by zero"); return r; } return lc_number(floor(x / y)); }
            if (op == 5) { if (y == 0) { lc_error(ctx, "division by zero"); return r; } return lc_number(x - floor(x / y) * y); }
            return lc_number(pow(x, y));
        case 7: return lc_bool(lc_value_equal(a, b));
        case 8: return lc_bool(!lc_value_equal(a, b));
        case 9: case 10: case 11: case 12:
            if (a.type == LC_NUMBER && b.type == LC_NUMBER) {
                if (op == 9) return lc_bool(a.as.number < b.as.number);
                if (op == 10) return lc_bool(a.as.number <= b.as.number);
                if (op == 11) return lc_bool(a.as.number > b.as.number);
                return lc_bool(a.as.number >= b.as.number);
            }
            if (a.type == LC_STRING && b.type == LC_STRING) {
                size_t n = a.as.string->length < b.as.string->length ? a.as.string->length : b.as.string->length;
                int c = memcmp(a.as.string->data, b.as.string->data, n);
                if (!c) c = a.as.string->length < b.as.string->length ? -1 : a.as.string->length > b.as.string->length;
                if (op == 9) return lc_bool(c < 0);
                if (op == 10) return lc_bool(c <= 0);
                if (op == 11) return lc_bool(c > 0);
                return lc_bool(c >= 0);
            }
            lc_error(ctx, "attempt to compare incompatible values"); return r;
        case 13: {
            LcValue sa = lc_to_string(ctx, a), sb = lc_to_string(ctx, b);
            size_t na, nb;
            if (ctx->error) { lc_release(ctx, sa); lc_release(ctx, sb); return r; }
            na = sa.as.string->length; nb = sb.as.string->length;
            {
                char *s = (char *)malloc(na + nb);
                if (!s) { lc_error(ctx, "out of memory"); }
                else {
                    memcpy(s, sa.as.string->data, na); memcpy(s + na, sb.as.string->data, nb);
                    r = lc_string_n(ctx, s, na + nb); free(s);
                }
            }
            lc_release(ctx, sa); lc_release(ctx, sb); return r;
        }
        default: return r;
    }
}

static LcValue lc_unary(LcContext *ctx, int op, LcValue a) {
    int ok;
    a = lc_first_value(a);
    if (op == 0) return lc_bool(!lc_truthy(a));
    if (op == 1) return lc_number(-lc_to_number(ctx, a, &ok));
    if (op == 2) {
        if (a.type == LC_STRING) return lc_number((double)a.as.string->length);
        if (a.type == LC_TABLE) return lc_number(lc_table_length(a));
        lc_error(ctx, "attempt to get length of a %s value", lc_type_name(a));
    }
    return lc_nil();
}

/* Built-in library. */
static LcValue lc_builtin_print(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int i; (void)self;
    for (i = 0; i < argc; ++i) {
        LcValue s = lc_to_string(ctx, argv[i]);
        if (i) putchar('\t');
        if (s.type == LC_STRING) fwrite(s.as.string->data, 1, s.as.string->length, stdout);
        lc_release(ctx, s);
    }
    putchar('\n'); fflush(stdout); return lc_nil();
}

static LcValue lc_builtin_type(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    (void)self; return lc_string(ctx, lc_type_name(argc ? argv[0] : lc_nil()));
}
static LcValue lc_builtin_tostring(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    (void)self; return lc_to_string(ctx, argc ? argv[0] : lc_nil());
}
static LcValue lc_builtin_tonumber(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int ok; double n; (void)self;
    if (!argc) return lc_nil();
    /* Conversion failure in tonumber is a value, not a runtime error. */
    { LcValue v = lc_first_value(argv[0]);
      if (v.type == LC_NUMBER) return lc_retain(v);
      if (v.type == LC_STRING) { char *end; n = strtod(v.as.string->data, &end); while (isspace((unsigned char)*end)) ++end;
        if (end != v.as.string->data && !*end) return lc_number(n); }
      (void)ok; return lc_nil(); }
}
static LcValue lc_builtin_assert(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    (void)self;
    if (!argc || !lc_truthy(argv[0])) { if (argc > 1 && argv[1].type == LC_STRING) lc_error(ctx, "%s", argv[1].as.string->data); else lc_error(ctx, "assertion failed!"); return lc_nil(); }
    return lc_tuple_new(ctx, argc, argv, 0);
}
static LcValue lc_builtin_error(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    (void)self;
    if (argc && argv[0].type == LC_STRING) lc_error(ctx, "%s", argv[0].as.string->data); else lc_error(ctx, "error");
    return lc_nil();
}
static LcValue lc_builtin_pcall(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcValue r, out[2]; (void)self;
    if (!argc) { lc_error(ctx, "pcall expects a function"); return lc_nil(); }
    r = lc_call_list(ctx, argv[0], argc - 1, argv + 1);
    if (ctx->error) {
        out[0] = lc_bool(0); out[1] = lc_string(ctx, ctx->message);
        ctx->error = 0; ctx->message[0] = 0;
        lc_release(ctx, r); return lc_tuple_new(ctx, 2, out, 1);
    }
    if (r.type == LC_TUPLE) {
        int n = r.as.tuple->count + 1, i; LcValue *items = malloc(sizeof(LcValue) * (size_t)n);
        if (!items) { lc_release(ctx, r); lc_error(ctx, "out of memory"); return lc_nil(); }
        items[0] = lc_bool(1); for (i = 1; i < n; ++i) items[i] = lc_retain(r.as.tuple->items[i - 1]);
        lc_release(ctx, r); r = lc_tuple_new(ctx, n, items, 1); free(items); return r;
    }
    out[0] = lc_bool(1); out[1] = r; return lc_tuple_new(ctx, 2, out, 1);
}
static LcValue lc_builtin_select(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int n, i; (void)self;
    if (!argc) return lc_nil();
    if (argv[0].type == LC_STRING && !strcmp(argv[0].as.string->data, "#")) return lc_number(argc - 1);
    n = (int)lc_to_number(ctx, argv[0], &i); if (n < 0) n = argc + n; if (n < 1) n = 1;
    return lc_tuple_new(ctx, argc - n > 0 ? argc - n : 0, argv + n, 0);
}

static LcValue lc_builtin_next(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcTable *t; int i = -1; LcValue out[2]; (void)self;
    if (!argc || argv[0].type != LC_TABLE) { lc_error(ctx, "table expected"); return lc_nil(); }
    t = argv[0].as.table;
    if (argc > 1 && argv[1].type != LC_NIL) {
        i = lc_table_find(t, argv[1]);
        if (i < 0) { lc_error(ctx, "invalid key to 'next'"); return lc_nil(); }
    }
    for (++i; i < (int)t->capacity; ++i) if (t->entries[i].used) {
        out[0] = lc_retain(t->entries[i].key); out[1] = lc_retain(t->entries[i].value);
        return lc_tuple_new(ctx, 2, out, 1);
    }
    return lc_nil();
}
static LcValue lc_builtin_pairs(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcValue out[3]; (void)self;
    out[0] = lc_function_new(ctx, lc_builtin_next, 0, NULL, "next");
    out[1] = argc ? lc_retain(argv[0]) : lc_nil(); out[2] = lc_nil();
    return lc_tuple_new(ctx, 3, out, 1);
}
static LcValue lc_builtin_ipairs_iter(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int i; LcValue out[2]; (void)self;
    if (argc < 2) return lc_nil();
    i = (int)lc_to_number(ctx, argv[1], &(int){0}) + 1;
    out[0] = lc_number(i); out[1] = lc_table_get(ctx, argv[0], out[0]);
    if (out[1].type == LC_NIL) { lc_release(ctx, out[0]); return lc_nil(); }
    return lc_tuple_new(ctx, 2, out, 1);
}
static LcValue lc_builtin_ipairs(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcValue out[3]; (void)self;
    out[0] = lc_function_new(ctx, lc_builtin_ipairs_iter, 0, NULL, "ipairs iterator");
    out[1] = argc ? lc_retain(argv[0]) : lc_nil(); out[2] = lc_number(0);
    return lc_tuple_new(ctx, 3, out, 1);
}

static LcValue lc_math_unary(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int ok; double x;
    x = lc_to_number(ctx, argc ? argv[0] : lc_nil(), &ok);
    /* Function pointer stored in the name field is avoided; dispatch by name. */
    if (!strcmp(self->name, "sin")) return lc_number(sin(x));
    if (!strcmp(self->name, "cos")) return lc_number(cos(x));
    if (!strcmp(self->name, "tan")) return lc_number(tan(x));
    if (!strcmp(self->name, "asin")) return lc_number(asin(x));
    if (!strcmp(self->name, "acos")) return lc_number(acos(x));
    if (!strcmp(self->name, "atan")) return lc_number(atan(x));
    if (!strcmp(self->name, "exp")) return lc_number(exp(x));
    if (!strcmp(self->name, "log")) return lc_number(log(x));
    if (!strcmp(self->name, "sqrt")) return lc_number(sqrt(x));
    if (!strcmp(self->name, "floor")) return lc_number(floor(x));
    if (!strcmp(self->name, "ceil")) return lc_number(ceil(x));
    if (!strcmp(self->name, "abs")) return lc_number(fabs(x));
    return lc_nil();
}
static LcValue lc_math_minmax(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int i, ok; double best, x; if (!argc) { lc_error(ctx, "number expected"); return lc_nil(); }
    best = lc_to_number(ctx, argv[0], &ok);
    for (i = 1; i < argc; ++i) { x = lc_to_number(ctx, argv[i], &ok); if ((!strcmp(self->name, "min") && x < best) || (!strcmp(self->name, "max") && x > best)) best = x; }
    return lc_number(best);
}
static LcValue lc_math_random(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    double r = (double)rand() / ((double)RAND_MAX + 1.0); int ok;
    if (!argc) return lc_number(r);
    if (argc == 1) return lc_number(1 + floor(r * lc_to_number(ctx, argv[0], &ok)));
    { int lo = (int)lc_to_number(ctx, argv[0], &ok), hi = (int)lc_to_number(ctx, argv[1], &ok); return lc_number(lo + floor(r * (hi - lo + 1))); }
}
static LcValue lc_math_randomseed(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int ok; if (argc) srand((unsigned)lc_to_number(ctx, argv[0], &ok)); (void)self; return lc_nil();
}

static LcValue lc_string_len(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    (void)self; if (!argc || lc_first_value(argv[0]).type != LC_STRING) { lc_error(ctx, "string expected"); return lc_nil(); } return lc_number((double)lc_first_value(argv[0]).as.string->length);
}
static LcValue lc_string_sub(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcValue v; int len, start, end, ok; (void)self;
    if (!argc || lc_first_value(argv[0]).type != LC_STRING) { lc_error(ctx, "string expected"); return lc_nil(); }
    v = lc_first_value(argv[0]); len = (int)v.as.string->length;
    start = argc > 1 ? (int)lc_to_number(ctx, argv[1], &ok) : 1; end = argc > 2 ? (int)lc_to_number(ctx, argv[2], &ok) : -1;
    if (start < 0) start = len + start + 1;
    if (end < 0) end = len + end + 1;
    if (start < 1) start = 1;
    if (end > len) end = len;
    if (start > end || start > len) return lc_string(ctx, "");
    return lc_string_n(ctx, v.as.string->data + start - 1, (size_t)(end - start + 1));
}
static LcValue lc_string_case(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcValue v, r; size_t i; int upper = !strcmp(self->name, "upper"); (void)self;
    if (!argc || lc_first_value(argv[0]).type != LC_STRING) { lc_error(ctx, "string expected"); return lc_nil(); }
    v = lc_first_value(argv[0]); r = lc_string_n(ctx, v.as.string->data, v.as.string->length);
    for (i = 0; i < r.as.string->length; ++i) r.as.string->data[i] = (char)(upper ? toupper((unsigned char)r.as.string->data[i]) : tolower((unsigned char)r.as.string->data[i]));
    return r;
}
static LcValue lc_string_rep(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcValue v, r; int n, ok; size_t i, len; char *s; (void)self;
    if (argc < 2 || lc_first_value(argv[0]).type != LC_STRING) { lc_error(ctx, "string expected"); return lc_nil(); }
    v = lc_first_value(argv[0]); n = (int)lc_to_number(ctx, argv[1], &ok); if (n < 0) n = 0;
    len = v.as.string->length * (size_t)n; s = malloc(len + 1); if (!s) { lc_error(ctx, "out of memory"); return lc_nil(); }
    for (i = 0; i < (size_t)n; ++i) memcpy(s + i * v.as.string->length, v.as.string->data, v.as.string->length);
    r = lc_string_n(ctx, s, len); free(s); return r;
}

static LcValue lc_table_insert(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int n, pos, i; (void)self;
    if (argc < 2 || argv[0].type != LC_TABLE) { lc_error(ctx, "table expected"); return lc_nil(); }
    n = lc_table_length(argv[0]); pos = argc > 2 ? (int)lc_to_number(ctx, argv[1], &(int){0}) : n + 1;
    for (i = n; i >= pos; --i) { LcValue v = lc_table_get(ctx, argv[0], lc_number(i)); lc_table_set(ctx, argv[0], lc_number(i + 1), v); lc_release(ctx, v); }
    lc_table_set(ctx, argv[0], lc_number(pos), argv[argc - 1]); return lc_nil();
}
static LcValue lc_table_remove(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int n, pos, i; LcValue old; (void)self;
    if (!argc || argv[0].type != LC_TABLE) { lc_error(ctx, "table expected"); return lc_nil(); }
    n = lc_table_length(argv[0]); pos = argc > 1 ? (int)lc_to_number(ctx, argv[1], &(int){0}) : n;
    old = lc_table_get(ctx, argv[0], lc_number(pos));
    for (i = pos; i < n; ++i) { LcValue v = lc_table_get(ctx, argv[0], lc_number(i + 1)); lc_table_set(ctx, argv[0], lc_number(i), v); lc_release(ctx, v); }
    lc_table_set(ctx, argv[0], lc_number(n), lc_nil()); return old;
}
static LcValue lc_table_concat(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int i, n, start = 1, end; LcValue acc; (void)self;
    if (!argc || argv[0].type != LC_TABLE) { lc_error(ctx, "table expected"); return lc_nil(); }
    n = lc_table_length(argv[0]);
    end = argc > 3 ? (int)lc_to_number(ctx, argv[3], &(int){0}) : n;
    if (argc > 2 && argv[2].type != LC_NIL) start = (int)lc_to_number(ctx, argv[2], &(int){0});
    acc = lc_string(ctx, "");
    for (i = start; i <= end; ++i) {
        LcValue v = lc_table_get(ctx, argv[0], lc_number(i)), s = lc_to_string(ctx, v), next;
        if (i > start && argc > 1) next = lc_binary(ctx, 13, acc, argv[1]); else next = lc_retain(acc);
        lc_release(ctx, acc); acc = lc_binary(ctx, 13, next, s);
        lc_release(ctx, next); lc_release(ctx, s); lc_release(ctx, v);
    }
    return acc;
}

static LcValue lc_io_write(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    int i; (void)self;
    for (i = 0; i < argc; ++i) { LcValue s = lc_to_string(ctx, argv[i]); if (s.type == LC_STRING) fwrite(s.as.string->data, 1, s.as.string->length, stdout); lc_release(ctx, s); }
    fflush(stdout); return lc_nil();
}
static LcValue lc_os_clock(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) { (void)self; (void)argc; (void)argv; return lc_number((double)clock() / CLOCKS_PER_SEC); }
static LcValue lc_os_time(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) { (void)ctx; (void)self; (void)argc; (void)argv; return lc_number((double)time(NULL)); }

static void lc_set_global(LcContext *ctx, const char *name, LcValue value) {
    LcValue key = lc_string(ctx, name); lc_table_set(ctx, ctx->globals, key, value); lc_release(ctx, key); lc_release(ctx, value);
}
static void lc_set_field(LcContext *ctx, LcValue table, const char *name, LcValue value) {
    LcValue key = lc_string(ctx, name); lc_table_set(ctx, table, key, value); lc_release(ctx, key); lc_release(ctx, value);
}
static void lc_add_native(LcContext *ctx, LcValue table, const char *name, LcNative fn) {
    lc_set_field(ctx, table, name, lc_function_new(ctx, fn, 0, NULL, name));
}

static LcContext *lc_context_new(const char *source) {
    LcContext *ctx = (LcContext *)calloc(1, sizeof(*ctx));
    LcValue math, str, tab, io, os;
    static const char *unary[] = {"sin","cos","tan","asin","acos","atan","exp","log","sqrt","floor","ceil","abs",NULL};
    int i;
    if (!ctx) return NULL;
    ctx->source = source;
    ctx->globals = lc_table(ctx);
    lc_set_global(ctx, "print", lc_function_new(ctx, lc_builtin_print, 0, NULL, "print"));
    lc_set_global(ctx, "type", lc_function_new(ctx, lc_builtin_type, 0, NULL, "type"));
    lc_set_global(ctx, "tostring", lc_function_new(ctx, lc_builtin_tostring, 0, NULL, "tostring"));
    lc_set_global(ctx, "tonumber", lc_function_new(ctx, lc_builtin_tonumber, 0, NULL, "tonumber"));
    lc_set_global(ctx, "assert", lc_function_new(ctx, lc_builtin_assert, 0, NULL, "assert"));
    lc_set_global(ctx, "error", lc_function_new(ctx, lc_builtin_error, 0, NULL, "error"));
    lc_set_global(ctx, "pcall", lc_function_new(ctx, lc_builtin_pcall, 0, NULL, "pcall"));
    lc_set_global(ctx, "select", lc_function_new(ctx, lc_builtin_select, 0, NULL, "select"));
    lc_set_global(ctx, "next", lc_function_new(ctx, lc_builtin_next, 0, NULL, "next"));
    lc_set_global(ctx, "pairs", lc_function_new(ctx, lc_builtin_pairs, 0, NULL, "pairs"));
    lc_set_global(ctx, "ipairs", lc_function_new(ctx, lc_builtin_ipairs, 0, NULL, "ipairs"));
    lc_set_global(ctx, "_VERSION", lc_string(ctx, "LuaC 0.2 (Lua-compatible syntax)"));
    math = lc_table(ctx);
    for (i = 0; unary[i]; ++i) lc_add_native(ctx, math, unary[i], lc_math_unary);
    lc_add_native(ctx, math, "min", lc_math_minmax); lc_add_native(ctx, math, "max", lc_math_minmax);
    lc_add_native(ctx, math, "random", lc_math_random); lc_add_native(ctx, math, "randomseed", lc_math_randomseed);
    lc_set_field(ctx, math, "pi", lc_number(3.14159265358979323846)); lc_set_field(ctx, math, "huge", lc_number(HUGE_VAL));
    lc_set_global(ctx, "math", math);
    str = lc_table(ctx); lc_add_native(ctx, str, "len", lc_string_len); lc_add_native(ctx, str, "sub", lc_string_sub);
    lc_add_native(ctx, str, "upper", lc_string_case); lc_add_native(ctx, str, "lower", lc_string_case); lc_add_native(ctx, str, "rep", lc_string_rep);
    lc_set_global(ctx, "string", str);
    tab = lc_table(ctx); lc_add_native(ctx, tab, "insert", lc_table_insert); lc_add_native(ctx, tab, "remove", lc_table_remove); lc_add_native(ctx, tab, "concat", lc_table_concat);
    lc_set_global(ctx, "table", tab);
    io = lc_table(ctx); lc_add_native(ctx, io, "write", lc_io_write); lc_set_global(ctx, "io", io);
    os = lc_table(ctx); lc_add_native(ctx, os, "clock", lc_os_clock); lc_add_native(ctx, os, "time", lc_os_time); lc_set_global(ctx, "os", os);
    return ctx;
}

static void lc_context_destroy(LcContext *ctx) {
    LcObject *o, *next;
    if (!ctx) return;
    /* Emergency ownership cleanup also handles reference cycles. No traversal
       happens during normal execution and no child reference is followed here. */
    o = ctx->objects; ctx->objects = NULL;
    while (o) {
        next = o->next;
        if (o->kind == LC_TABLE) { free(((LcTable *)o)->entries); }
        else if (o->kind == LC_FUNCTION) { free(((LcFunction *)o)->upvalues); }
        free(o); o = next;
    }
    free(ctx);
}

static int lc_report_error(LcContext *ctx) {
    if (!ctx->error) return 0;
    fprintf(stderr, "%s:%d: %s\n", ctx->source ? ctx->source : "luac", ctx->line, ctx->message);
    return 1;
}

#endif
