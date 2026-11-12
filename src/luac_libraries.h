#ifndef LUAC_LIBRARIES_H
#define LUAC_LIBRARIES_H

/* Optional native libraries. This file is included by runtime.h after the
   ownership, table and function APIs are defined. No interpreter or external
   Lua dependency is used: every entry below is an ordinary C99 function. */

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif
#ifndef _WIN32
#include <netinet/tcp.h>
#endif

typedef struct { char *data; size_t length, capacity; } LcLibBuffer;

static int lc_lib_reserve(LcContext *ctx, LcLibBuffer *b, size_t extra) {
    size_t needed, capacity; char *memory;
    if (extra > SIZE_MAX - b->length - 1) { lc_error(ctx, "buffer size overflow"); return 0; }
    needed = b->length + extra + 1;
    if (needed <= b->capacity) return 1;
    capacity = b->capacity ? b->capacity : 128;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
        capacity *= 2;
    }
    memory = (char *)realloc(b->data, capacity);
    if (!memory) { lc_error(ctx, "out of memory"); return 0; }
    b->data = memory; b->capacity = capacity; return 1;
}
static int lc_lib_append(LcContext *ctx, LcLibBuffer *b, const char *data, size_t length) {
    if (!lc_lib_reserve(ctx, b, length)) return 0;
    if (length) memcpy(b->data + b->length, data, length);
    b->length += length; b->data[b->length] = 0; return 1;
}
static int lc_lib_puts(LcContext *ctx, LcLibBuffer *b, const char *text) {
    return lc_lib_append(ctx, b, text, strlen(text));
}
static int lc_lib_putc(LcContext *ctx, LcLibBuffer *b, char c) {
    return lc_lib_append(ctx, b, &c, 1);
}
static int lc_lib_printf(LcContext *ctx, LcLibBuffer *b, const char *format, ...) {
    va_list args, copy; int length;
    va_start(args, format); va_copy(copy, args);
    length = vsnprintf(NULL, 0, format, copy); va_end(copy);
    if (length < 0 || !lc_lib_reserve(ctx, b, (size_t)(length < 0 ? 0 : length))) { va_end(args); return 0; }
    vsnprintf(b->data + b->length, b->capacity - b->length, format, args); va_end(args);
    b->length += (size_t)length; return 1;
}
static LcValue lc_lib_buffer_value(LcContext *ctx, LcLibBuffer *b) {
    LcValue value = lc_string_n(ctx, b->data ? b->data : "", b->length);
    free(b->data); memset(b, 0, sizeof(*b)); return value;
}
static LcValue lc_lib_failure(LcContext *ctx, const char *operation) {
    LcValue values[2]; char message[256];
    int code =
#ifdef _WIN32
        WSAGetLastError();
#else
        errno;
#endif
    (void)code;
    snprintf(message, sizeof(message), "%s: %s", operation, strerror(errno));
    values[0] = lc_nil(); values[1] = lc_string(ctx, message);
    return lc_tuple_new(ctx, 2, values, 1);
}
static LcString *lc_lib_string_arg(LcContext *ctx, int argc, LcValue *argv, int index) {
    LcValue value;
    if (index >= argc) { lc_error(ctx, "string expected"); return NULL; }
    value = lc_first_value(argv[index]);
    if (value.type != LC_STRING) { lc_error(ctx, "string expected"); return NULL; }
    return value.as.string;
}
static double lc_lib_number_arg(LcContext *ctx, int argc, LcValue *argv, int index, double fallback) {
    int ok;
    if (index >= argc || lc_first_value(argv[index]).type == LC_NIL) return fallback;
    return lc_to_number(ctx, argv[index], &ok);
}
static const char *lc_lib_name(LcFunction *self) { return self && self->name ? self->name : ""; }

/* ------------------------------- text ---------------------------------- */
static LcValue lc_text_simple(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0); const char *name = lc_lib_name(self);
    size_t begin = 0, end; LcValue result;
    if (!s) return lc_nil(); end = s->length;
    if (!strcmp(name, "trim") || !strcmp(name, "ltrim")) while (begin < end && isspace((unsigned char)s->data[begin])) ++begin;
    if (!strcmp(name, "trim") || !strcmp(name, "rtrim")) while (end > begin && isspace((unsigned char)s->data[end - 1])) --end;
    if (!strcmp(name, "reverse")) {
        char *copy = (char *)malloc(end ? end : 1); size_t i;
        if (!copy) { lc_error(ctx, "out of memory"); return lc_nil(); }
        for (i = 0; i < end; ++i) copy[i] = s->data[end - i - 1];
        result = lc_string_n(ctx, copy, end); free(copy); return result;
    }
    if (!strcmp(name, "capitalize") || !strcmp(name, "title") || !strcmp(name, "swapcase")) {
        char *copy = (char *)malloc(end + 1); size_t i; int word = 1;
        if (!copy) { lc_error(ctx, "out of memory"); return lc_nil(); }
        for (i = 0; i < end; ++i) {
            unsigned char c = (unsigned char)s->data[i];
            if (!strcmp(name, "swapcase")) copy[i] = (char)(isupper(c) ? tolower(c) : islower(c) ? toupper(c) : c);
            else if (!strcmp(name, "capitalize")) copy[i] = (char)(i == 0 ? toupper(c) : tolower(c));
            else { copy[i] = (char)(word ? toupper(c) : tolower(c)); word = isspace(c) || ispunct(c); }
        }
        result = lc_string_n(ctx, copy, end); free(copy); return result;
    }
    return lc_string_n(ctx, s->data + begin, end - begin);
}
static LcValue lc_text_predicate(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0), *part = lc_lib_string_arg(ctx, argc, argv, 1);
    const char *name = lc_lib_name(self); size_t i, count = 0; int at;
    if (!s || !part) return lc_nil();
    if (!strcmp(name, "startswith")) return lc_bool(part->length <= s->length && !memcmp(s->data, part->data, part->length));
    if (!strcmp(name, "endswith")) return lc_bool(part->length <= s->length && !memcmp(s->data + s->length - part->length, part->data, part->length));
    if (!strcmp(name, "contains")) {
        if (!part->length) return lc_bool(1);
        for (i = 0; i + part->length <= s->length; ++i) if (!memcmp(s->data + i, part->data, part->length)) return lc_bool(1);
        return lc_bool(0);
    }
    at = (int)lc_lib_number_arg(ctx, argc, argv, 2, 1); if (at < 1) at = 1;
    if (!part->length) return lc_number(at <= (int)s->length + 1 ? at : 0);
    for (i = (size_t)(at - 1); i + part->length <= s->length; ++i) if (!memcmp(s->data + i, part->data, part->length)) {
        if (!strcmp(name, "find")) return lc_number((double)i + 1);
        ++count; i += part->length - 1;
    }
    return lc_number(!strcmp(name, "find") ? 0 : count);
}
static LcValue lc_text_split(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0), *separator = lc_lib_string_arg(ctx, argc, argv, 1);
    LcValue result, item; size_t start = 0, i = 0; int index = 1, plain = 1;
    if (!s || !separator) return lc_nil(); result = lc_table(ctx);
    if (!separator->length) {
        for (i = 0; i < s->length; ++i) { item = lc_string_n(ctx, s->data + i, 1); lc_table_add(ctx, result, index++, item); lc_release(ctx, item); }
        return result;
    }
    (void)plain;
    while (i + separator->length <= s->length) {
        if (!memcmp(s->data + i, separator->data, separator->length)) {
            item = lc_string_n(ctx, s->data + start, i - start); lc_table_add(ctx, result, index++, item); lc_release(ctx, item);
            i += separator->length; start = i;
        } else ++i;
    }
    item = lc_string_n(ctx, s->data + start, s->length - start); lc_table_add(ctx, result, index, item); lc_release(ctx, item);
    return result;
}
static LcValue lc_text_join(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *separator; LcValue table; LcLibBuffer out = {0}; int i, length;
    if (!argc || lc_first_value(argv[0]).type != LC_TABLE) { lc_error(ctx, "table expected"); return lc_nil(); }
    table = lc_first_value(argv[0]); separator = argc > 1 ? lc_lib_string_arg(ctx, argc, argv, 1) : NULL;
    length = lc_table_length(table);
    for (i = 1; i <= length; ++i) {
        LcValue value = lc_table_raw_get(ctx, table, lc_number(i)); LcValue string = lc_to_string(ctx, value);
        if (i > 1 && separator) lc_lib_append(ctx, &out, separator->data, separator->length);
        lc_lib_append(ctx, &out, string.as.string->data, string.as.string->length);
        lc_release(ctx, string); lc_release(ctx, value);
    }
    return lc_lib_buffer_value(ctx, &out);
}
static LcValue lc_text_replace(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0), *from = lc_lib_string_arg(ctx, argc, argv, 1), *to = lc_lib_string_arg(ctx, argc, argv, 2);
    LcLibBuffer out = {0}; size_t i = 0; int limit = (int)lc_lib_number_arg(ctx, argc, argv, 3, -1), replacements = 0;
    if (!s || !from || !to) return lc_nil();
    while (i < s->length) {
        if (from->length && (limit < 0 || replacements < limit) && i + from->length <= s->length && !memcmp(s->data + i, from->data, from->length)) {
            lc_lib_append(ctx, &out, to->data, to->length); i += from->length; ++replacements;
        } else lc_lib_putc(ctx, &out, s->data[i++]);
    }
    { LcValue values[2]; values[0] = lc_lib_buffer_value(ctx, &out); values[1] = lc_number(replacements); return lc_tuple_new(ctx, 2, values, 1); }
}
static LcValue lc_text_pad(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0), *padding = NULL;
    int width = (int)lc_lib_number_arg(ctx, argc, argv, 1, 0), total, left, right, i;
    LcLibBuffer out = {0}; char pad = ' ';
    if (!s) return lc_nil(); if (argc > 2) { padding = lc_lib_string_arg(ctx, argc, argv, 2); if (padding && padding->length) pad = padding->data[0]; }
    total = width > (int)s->length ? width - (int)s->length : 0;
    if (!strcmp(lc_lib_name(self), "lpad")) { left = total; right = 0; }
    else if (!strcmp(lc_lib_name(self), "rpad")) { left = 0; right = total; }
    else { left = total / 2; right = total - left; }
    for (i = 0; i < left; ++i) lc_lib_putc(ctx, &out, pad);
    lc_lib_append(ctx, &out, s->data, s->length);
    for (i = 0; i < right; ++i) lc_lib_putc(ctx, &out, pad);
    return lc_lib_buffer_value(ctx, &out);
}
static LcValue lc_text_repeat(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0); int count = (int)lc_lib_number_arg(ctx, argc, argv, 1, 1), i;
    LcLibBuffer out = {0}; if (!s) return lc_nil(); if (count < 0) count = 0;
    if (s->length && (size_t)count > SIZE_MAX / s->length) { lc_error(ctx, "result too large"); return lc_nil(); }
    for (i = 0; i < count; ++i) if (!lc_lib_append(ctx, &out, s->data, s->length)) break;
    return lc_lib_buffer_value(ctx, &out);
}

/* ---------------------------- hex / base64 ------------------------------ */
static LcValue lc_hex_encode(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    static const char digits[] = "0123456789abcdef"; LcString *s = lc_lib_string_arg(ctx, argc, argv, 0);
    LcValue result; char *out; size_t i; int upper = 0; (void)self;
    if (argc > 1 && lc_first_value(argv[1]).type == LC_STRING) upper = !strcmp(lc_first_value(argv[1]).as.string->data, "upper");
    if (!s) return lc_nil(); if (s->length > SIZE_MAX / 2) { lc_error(ctx, "result too large"); return lc_nil(); }
    out = (char *)malloc(s->length * 2 + 1); if (!out) { lc_error(ctx, "out of memory"); return lc_nil(); }
    for (i = 0; i < s->length; ++i) { unsigned char c = (unsigned char)s->data[i]; out[i*2] = digits[c >> 4]; out[i*2+1] = digits[c & 15]; }
    if (upper) for (i = 0; i < s->length * 2; ++i) out[i] = (char)toupper((unsigned char)out[i]);
    result = lc_string_n(ctx, out, s->length * 2); free(out); return result;
}
static int lc_hex_digit(char c) { if (c >= '0' && c <= '9') return c - '0'; c = (char)tolower((unsigned char)c); return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
static LcValue lc_hex_decode(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0); LcValue result; char *out; size_t i, used = 0; (void)self;
    if (!s) return lc_nil(); out = (char *)malloc(s->length / 2 + 1); if (!out) { lc_error(ctx, "out of memory"); return lc_nil(); }
    for (i = 0; i < s->length;) {
        int high, low; while (i < s->length && isspace((unsigned char)s->data[i])) ++i; if (i == s->length) break;
        high = lc_hex_digit(s->data[i++]); while (i < s->length && isspace((unsigned char)s->data[i])) ++i;
        if (high < 0 || i == s->length || (low = lc_hex_digit(s->data[i++])) < 0) { free(out); return lc_nil(); }
        out[used++] = (char)((high << 4) | low);
    }
    result = lc_string_n(ctx, out, used); free(out); return result;
}
static LcValue lc_base64_encode(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0); LcLibBuffer out = {0}; size_t i; (void)self;
    if (!s) return lc_nil(); if (s->length > (SIZE_MAX - 2) / 3) { lc_error(ctx, "input too large"); return lc_nil(); }
    for (i = 0; i < s->length; i += 3) {
        uint32_t value = (uint32_t)(unsigned char)s->data[i] << 16; int remain = (int)(s->length - i);
        if (remain > 1) value |= (uint32_t)(unsigned char)s->data[i+1] << 8;
        if (remain > 2) value |= (unsigned char)s->data[i+2];
        lc_lib_putc(ctx, &out, alphabet[(value >> 18) & 63]); lc_lib_putc(ctx, &out, alphabet[(value >> 12) & 63]);
        lc_lib_putc(ctx, &out, remain > 1 ? alphabet[(value >> 6) & 63] : '='); lc_lib_putc(ctx, &out, remain > 2 ? alphabet[value & 63] : '=');
    }
    return lc_lib_buffer_value(ctx, &out);
}
static int lc_base64_digit(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+' || c == '-') return 62; if (c == '/' || c == '_') return 63; return -1;
}
static LcValue lc_base64_decode(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0); LcLibBuffer out = {0}; uint32_t value = 0; int bits = 0, padding = 0; size_t i; (void)self;
    if (!s) return lc_nil();
    for (i = 0; i < s->length; ++i) {
        unsigned char c = (unsigned char)s->data[i]; int digit;
        if (isspace(c)) continue; if (c == '=') { ++padding; continue; }
        digit = lc_base64_digit(c); if (digit < 0 || padding) { free(out.data); return lc_nil(); }
        value = (value << 6) | (uint32_t)digit; bits += 6;
        if (bits >= 8) { bits -= 8; lc_lib_putc(ctx, &out, (char)((value >> bits) & 255)); }
    }
    if (padding > 2 || (bits == 6) || (padding && bits == 0)) { free(out.data); return lc_nil(); }
    return lc_lib_buffer_value(ctx, &out);
}

/* -------------------------------- URL ----------------------------------- */
static int lc_url_safe(unsigned char c) { return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~'; }
static LcValue lc_url_encode(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0); LcLibBuffer out = {0}; size_t i; int plus = !strcmp(lc_lib_name(self), "queryencode");
    if (!s) return lc_nil();
    for (i = 0; i < s->length; ++i) { unsigned char c = (unsigned char)s->data[i];
        if (lc_url_safe(c)) lc_lib_putc(ctx, &out, (char)c); else if (plus && c == ' ') lc_lib_putc(ctx, &out, '+'); else lc_lib_printf(ctx, &out, "%%%02X", c); }
    return lc_lib_buffer_value(ctx, &out);
}
static LcValue lc_url_decode(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0); LcLibBuffer out = {0}; size_t i; int plus = !strcmp(lc_lib_name(self), "querydecode");
    if (!s) return lc_nil();
    for (i = 0; i < s->length; ++i) {
        if (s->data[i] == '%') { int a, b; if (i + 2 >= s->length || (a = lc_hex_digit(s->data[i+1])) < 0 || (b = lc_hex_digit(s->data[i+2])) < 0) { free(out.data); return lc_nil(); } lc_lib_putc(ctx, &out, (char)((a << 4) | b)); i += 2; }
        else lc_lib_putc(ctx, &out, plus && s->data[i] == '+' ? ' ' : s->data[i]);
    }
    return lc_lib_buffer_value(ctx, &out);
}
static LcValue lc_url_parse(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0); LcValue result, value; const char *start, *at, *end, *authority, *path, *query = NULL, *fragment = NULL;
    size_t length; (void)self; if (!s) return lc_nil(); start = s->data; end = start + s->length; result = lc_table(ctx);
    at = strstr(start, "://");
    if (at && at < end) { value = lc_string_n(ctx, start, (size_t)(at - start)); lc_set_field(ctx, result, "scheme", value); authority = at + 3; }
    else authority = start;
    path = authority; while (path < end && *path != '/' && *path != '?' && *path != '#') ++path;
    for (at = authority; at < path; ++at) if (*at == '@') authority = at + 1;
    { const char *host = authority, *port = NULL, *colon = NULL;
      for (at = authority; at < path; ++at) if (*at == ':') colon = at;
      if (authority < path && *authority == '[') { host = authority + 1; for (at = host; at < path && *at != ']'; ++at) {} if (at < path && at + 1 < path && at[1] == ':') colon = at + 1; }
      if (colon) port = colon + 1;
      value = lc_string_n(ctx, host, (size_t)((colon ? colon : path) - host)); lc_set_field(ctx, result, "host", value);
      if (port) { value = lc_number(strtod(port, NULL)); lc_set_field(ctx, result, "port", value); }
    }
    { const char *cursor = path;
      for (at = path; at < end; ++at) { if (*at == '?' && !query) query = at + 1; if (*at == '#') { fragment = at + 1; break; } }
      length = (size_t)((query ? query - 1 : fragment ? fragment - 1 : end) - cursor);
      value = lc_string_n(ctx, cursor, length); lc_set_field(ctx, result, "path", value);
      if (query) { value = lc_string_n(ctx, query, (size_t)((fragment ? fragment - 1 : end) - query)); lc_set_field(ctx, result, "query", value); }
      if (fragment) { value = lc_string_n(ctx, fragment, (size_t)(end - fragment)); lc_set_field(ctx, result, "fragment", value); }
    }
    return result;
}
static LcValue lc_url_build(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    static const char *names[] = {"scheme", "host", "path", "query", "fragment"};
    LcLibBuffer out = {0}; int i; (void)self;
    if (!argc || lc_first_value(argv[0]).type != LC_TABLE) { lc_error(ctx, "table expected"); return lc_nil(); }
    for (i = 0; i < 5; ++i) {
        LcValue key = lc_string(ctx, names[i]), value = lc_table_raw_get(ctx, argv[0], key); lc_release(ctx, key);
        if (i == 1) {
            LcValue pk = lc_string(ctx, "port"), pv = lc_table_raw_get(ctx, argv[0], pk); lc_release(ctx, pk);
            if (value.type != LC_NIL) {
                lc_lib_puts(ctx, &out, "://");
                if (value.type == LC_STRING) lc_lib_append(ctx, &out, value.as.string->data, value.as.string->length);
            }
            if (pv.type == LC_NUMBER) lc_lib_printf(ctx, &out, ":%d", (int)pv.as.number);
            lc_release(ctx, pv);
        } else if (value.type != LC_NIL) {
            if (i == 3) lc_lib_putc(ctx, &out, '?'); else if (i == 4) lc_lib_putc(ctx, &out, '#');
            if (value.type == LC_STRING) lc_lib_append(ctx, &out, value.as.string->data, value.as.string->length);
        }
        lc_release(ctx, value);
    }
    return lc_lib_buffer_value(ctx, &out);
}

/* -------------------------------- JSON ---------------------------------- */
typedef struct { LcContext *ctx; const char *data; size_t length, position; int failed; char message[160]; } LcJsonParser;
static void lc_json_skip(LcJsonParser *p) { while (p->position < p->length && isspace((unsigned char)p->data[p->position])) ++p->position; }
static void lc_json_fail(LcJsonParser *p, const char *message) { if (!p->failed) { p->failed = 1; snprintf(p->message, sizeof(p->message), "%s at byte %d", message, (int)p->position + 1); } }
static int lc_json_hex4(LcJsonParser *p, uint32_t *value) {
    int i, digit; *value = 0; if (p->position + 4 > p->length) return 0;
    for (i = 0; i < 4; ++i) { digit = lc_hex_digit(p->data[p->position++]); if (digit < 0) return 0; *value = (*value << 4) | (uint32_t)digit; } return 1;
}
static LcValue lc_json_string(LcJsonParser *p) {
    LcLibBuffer out = {0}; if (p->position >= p->length || p->data[p->position++] != '"') { lc_json_fail(p, "string expected"); return lc_nil(); }
    while (p->position < p->length) {
        unsigned char c = (unsigned char)p->data[p->position++];
        if (c == '"') return lc_lib_buffer_value(p->ctx, &out);
        if (c < 32) { lc_json_fail(p, "control character in string"); break; }
        if (c != '\\') { lc_lib_putc(p->ctx, &out, (char)c); continue; }
        if (p->position >= p->length) { lc_json_fail(p, "unfinished escape"); break; }
        c = (unsigned char)p->data[p->position++];
        switch (c) {
            case '"': case '\\': case '/': lc_lib_putc(p->ctx, &out, (char)c); break;
            case 'b': lc_lib_putc(p->ctx, &out, '\b'); break; case 'f': lc_lib_putc(p->ctx, &out, '\f'); break;
            case 'n': lc_lib_putc(p->ctx, &out, '\n'); break; case 'r': lc_lib_putc(p->ctx, &out, '\r'); break; case 't': lc_lib_putc(p->ctx, &out, '\t'); break;
            case 'u': {
                uint32_t cp, low; char bytes[4]; int count;
                if (!lc_json_hex4(p, &cp)) { lc_json_fail(p, "invalid Unicode escape"); goto failed; }
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (p->position + 2 > p->length || p->data[p->position] != '\\' || p->data[p->position+1] != 'u') { lc_json_fail(p, "missing low surrogate"); goto failed; }
                    p->position += 2; if (!lc_json_hex4(p, &low) || low < 0xdc00 || low > 0xdfff) { lc_json_fail(p, "invalid low surrogate"); goto failed; }
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) { lc_json_fail(p, "unexpected low surrogate"); goto failed; }
                if (cp < 0x80) { bytes[0] = (char)cp; count = 1; }
                else if (cp < 0x800) { bytes[0] = (char)(0xc0 | (cp >> 6)); bytes[1] = (char)(0x80 | (cp & 63)); count = 2; }
                else if (cp < 0x10000) { bytes[0] = (char)(0xe0 | (cp >> 12)); bytes[1] = (char)(0x80 | ((cp >> 6) & 63)); bytes[2] = (char)(0x80 | (cp & 63)); count = 3; }
                else { bytes[0] = (char)(0xf0 | (cp >> 18)); bytes[1] = (char)(0x80 | ((cp >> 12) & 63)); bytes[2] = (char)(0x80 | ((cp >> 6) & 63)); bytes[3] = (char)(0x80 | (cp & 63)); count = 4; }
                lc_lib_append(p->ctx, &out, bytes, (size_t)count); break;
            }
            default: lc_json_fail(p, "invalid escape"); goto failed;
        }
    }
    lc_json_fail(p, "unterminated string");
failed: free(out.data); return lc_nil();
}
static LcValue lc_json_value(LcJsonParser *p, int depth);
static LcValue lc_json_array(LcJsonParser *p, int depth) {
    LcContext *ctx = p->ctx; LcValue result = lc_table(ctx), item; int index = 1; ++p->position; lc_json_skip(p);
    if (p->position < p->length && p->data[p->position] == ']') { ++p->position; return result; }
    for (;;) {
        item = lc_json_value(p, depth + 1); if (p->failed) { lc_release(ctx, result); return item; }
        lc_table_add(ctx, result, index++, item); lc_release(ctx, item); lc_json_skip(p);
        if (p->position >= p->length) { lc_json_fail(p, "unterminated array"); break; }
        if (p->data[p->position] == ']') { ++p->position; return result; }
        if (p->data[p->position++] != ',') { lc_json_fail(p, "comma expected"); break; } lc_json_skip(p);
    }
    lc_release(ctx, result); return lc_nil();
}
static LcValue lc_json_object(LcJsonParser *p, int depth) {
    LcContext *ctx = p->ctx; LcValue result = lc_table(ctx), key, value; ++p->position; lc_json_skip(p);
    if (p->position < p->length && p->data[p->position] == '}') { ++p->position; return result; }
    for (;;) {
        if (p->position >= p->length || p->data[p->position] != '"') { lc_json_fail(p, "object key expected"); break; }
        key = lc_json_string(p); lc_json_skip(p);
        if (p->position >= p->length || p->data[p->position++] != ':') { lc_release(ctx, key); lc_json_fail(p, "colon expected"); break; }
        lc_json_skip(p); value = lc_json_value(p, depth + 1);
        if (!p->failed) lc_table_raw_set(ctx, result, key, value); lc_release(ctx, key); lc_release(ctx, value);
        if (p->failed) break; lc_json_skip(p);
        if (p->position >= p->length) { lc_json_fail(p, "unterminated object"); break; }
        if (p->data[p->position] == '}') { ++p->position; return result; }
        if (p->data[p->position++] != ',') { lc_json_fail(p, "comma expected"); break; } lc_json_skip(p);
    }
    lc_release(ctx, result); return lc_nil();
}
static LcValue lc_json_value(LcJsonParser *p, int depth) {
    char *end; double number;
    if (depth > 128) { lc_json_fail(p, "nesting too deep"); return lc_nil(); } lc_json_skip(p);
    if (p->position >= p->length) { lc_json_fail(p, "value expected"); return lc_nil(); }
    switch (p->data[p->position]) {
        case '"': return lc_json_string(p); case '[': return lc_json_array(p, depth); case '{': return lc_json_object(p, depth);
        case 'n': if (p->position + 4 <= p->length && !memcmp(p->data + p->position, "null", 4)) { p->position += 4; return lc_nil(); } break;
        case 't': if (p->position + 4 <= p->length && !memcmp(p->data + p->position, "true", 4)) { p->position += 4; return lc_bool(1); } break;
        case 'f': if (p->position + 5 <= p->length && !memcmp(p->data + p->position, "false", 5)) { p->position += 5; return lc_bool(0); } break;
        default: break;
    }
    if (p->data[p->position] == '-' || isdigit((unsigned char)p->data[p->position])) {
        errno = 0; number = strtod(p->data + p->position, &end);
        if (end != p->data + p->position && errno != ERANGE) { p->position = (size_t)(end - p->data); return lc_number(number); }
    }
    lc_json_fail(p, "invalid value"); return lc_nil();
}
static LcValue lc_json_decode(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0); LcJsonParser parser; LcValue value; (void)self;
    if (!s) return lc_nil(); memset(&parser, 0, sizeof(parser)); parser.ctx = ctx; parser.data = s->data; parser.length = s->length;
    value = lc_json_value(&parser, 0); lc_json_skip(&parser);
    if (!parser.failed && parser.position != parser.length) lc_json_fail(&parser, "trailing data");
    if (parser.failed) { LcValue out[2]; lc_release(ctx, value); out[0] = lc_nil(); out[1] = lc_string(ctx, parser.message); return lc_tuple_new(ctx, 2, out, 1); }
    return value;
}
static int lc_json_encode_string(LcContext *ctx, LcLibBuffer *out, LcString *s) {
    size_t i; lc_lib_putc(ctx, out, '"');
    for (i = 0; i < s->length; ++i) { unsigned char c = (unsigned char)s->data[i];
        switch (c) { case '"': lc_lib_puts(ctx, out, "\\\""); break; case '\\': lc_lib_puts(ctx, out, "\\\\"); break;
            case '\b': lc_lib_puts(ctx, out, "\\b"); break; case '\f': lc_lib_puts(ctx, out, "\\f"); break; case '\n': lc_lib_puts(ctx, out, "\\n"); break;
            case '\r': lc_lib_puts(ctx, out, "\\r"); break; case '\t': lc_lib_puts(ctx, out, "\\t"); break;
            default: if (c < 32) lc_lib_printf(ctx, out, "\\u%04x", c); else lc_lib_putc(ctx, out, (char)c); }
    } return lc_lib_putc(ctx, out, '"');
}
static int lc_json_encode_value(LcContext *ctx, LcLibBuffer *out, LcValue value, int depth) {
    value = lc_first_value(value); if (depth > 128) { lc_error(ctx, "JSON nesting too deep"); return 0; }
    switch (value.type) {
        case LC_NIL: return lc_lib_puts(ctx, out, "null"); case LC_BOOL: return lc_lib_puts(ctx, out, value.as.boolean ? "true" : "false");
        case LC_NUMBER: if (!isfinite(value.as.number)) return lc_lib_puts(ctx, out, "null"); return lc_lib_printf(ctx, out, "%.17g", value.as.number);
        case LC_STRING: return lc_json_encode_string(ctx, out, value.as.string);
        case LC_TABLE: {
            int i, array = lc_table_length(value) == (int)value.as.table->length;
            lc_lib_putc(ctx, out, array ? '[' : '{');
            for (i = 0; i < (int)value.as.table->capacity; ++i) if (value.as.table->entries[i].used) {
                LcEntry *entry = &value.as.table->entries[i];
                if (!array) {
                    if (entry->key.type == LC_STRING) lc_json_encode_string(ctx, out, entry->key.as.string);
                    else if (entry->key.type == LC_NUMBER) { LcValue key = lc_to_string(ctx, entry->key); lc_json_encode_string(ctx, out, key.as.string); lc_release(ctx, key); }
                    else { lc_error(ctx, "JSON object keys must be strings or numbers"); return 0; }
                    lc_lib_putc(ctx, out, ':');
                }
                if (!lc_json_encode_value(ctx, out, entry->value, depth + 1)) return 0;
                lc_lib_putc(ctx, out, ',');
            }
            if (out->length && out->data[out->length - 1] == ',') --out->length;
            return lc_lib_putc(ctx, out, array ? ']' : '}');
        }
        default: lc_error(ctx, "cannot encode %s as JSON", lc_type_name(value)); return 0;
    }
}
static LcValue lc_json_encode(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcLibBuffer out = {0}; (void)self; if (!argc) return lc_string(ctx, "null");
    if (!lc_json_encode_value(ctx, &out, argv[0], 0)) { free(out.data); return lc_nil(); }
    return lc_lib_buffer_value(ctx, &out);
}

/* ------------------------------- hashes --------------------------------- */
static LcValue lc_hash32(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s = lc_lib_string_arg(ctx, argc, argv, 0); const char *name = lc_lib_name(self); uint32_t hash; size_t i; int j;
    if (!s) return lc_nil();
    if (!strcmp(name, "crc32")) { hash = 0xffffffffu; for (i = 0; i < s->length; ++i) { hash ^= (unsigned char)s->data[i]; for (j = 0; j < 8; ++j) hash = (hash >> 1) ^ (0xedb88320u & (uint32_t)-(int)(hash & 1)); } hash ^= 0xffffffffu; }
    else if (!strcmp(name, "adler32")) { uint32_t a = 1, b = 0; for (i = 0; i < s->length; ++i) { a = (a + (unsigned char)s->data[i]) % 65521; b = (b + a) % 65521; } hash = (b << 16) | a; }
    else if (!strcmp(name, "fnv1a")) { hash = 2166136261u; for (i = 0; i < s->length; ++i) { hash ^= (unsigned char)s->data[i]; hash *= 16777619u; } }
    else { hash = 5381; for (i = 0; i < s->length; ++i) hash = ((hash << 5) + hash) ^ (unsigned char)s->data[i]; }
    return lc_number((double)hash);
}
typedef struct { uint32_t state[8]; uint64_t bits; unsigned char block[64]; size_t used; } LcSha256;
static uint32_t lc_sha_rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static void lc_sha_block(LcSha256 *s, const unsigned char *block) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2; int i;
    for (i = 0; i < 16; ++i) w[i] = (uint32_t)block[i*4]<<24 | (uint32_t)block[i*4+1]<<16 | (uint32_t)block[i*4+2]<<8 | block[i*4+3];
    for (i = 16; i < 64; ++i) { uint32_t x=w[i-15], y=w[i-2]; w[i] = (lc_sha_rotr(x,7)^lc_sha_rotr(x,18)^(x>>3)) + w[i-16] + (lc_sha_rotr(y,17)^lc_sha_rotr(y,19)^(y>>10)) + w[i-7]; }
    a=s->state[0];b=s->state[1];c=s->state[2];d=s->state[3];e=s->state[4];f=s->state[5];g=s->state[6];h=s->state[7];
    for (i = 0; i < 64; ++i) { t1=h+(lc_sha_rotr(e,6)^lc_sha_rotr(e,11)^lc_sha_rotr(e,25))+((e&f)^((~e)&g))+k[i]+w[i]; t2=(lc_sha_rotr(a,2)^lc_sha_rotr(a,13)^lc_sha_rotr(a,22))+((a&b)^(a&c)^(b&c)); h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
    s->state[0]+=a;s->state[1]+=b;s->state[2]+=c;s->state[3]+=d;s->state[4]+=e;s->state[5]+=f;s->state[6]+=g;s->state[7]+=h;
}
static void lc_sha_init(LcSha256 *s) { static const uint32_t initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}; memset(s,0,sizeof(*s)); memcpy(s->state,initial,sizeof(initial)); }
static void lc_sha_update(LcSha256 *s, const void *data, size_t length) {
    const unsigned char *bytes=(const unsigned char *)data; s->bits += (uint64_t)length * 8;
    while (length) { size_t take = 64 - s->used; if (take > length) take = length; memcpy(s->block+s->used,bytes,take);s->used+=take;bytes+=take;length-=take;if(s->used==64){lc_sha_block(s,s->block);s->used=0;} }
}
static void lc_sha_final(LcSha256 *s, unsigned char digest[32]) {
    int i; uint64_t bits=s->bits; unsigned char one=0x80, zero=0; lc_sha_update(s,&one,1); while(s->used!=56)lc_sha_update(s,&zero,1);
    { unsigned char length[8]; for(i=0;i<8;++i)length[7-i]=(unsigned char)(bits>>(i*8));
      /* Final writes must not alter the preserved message length. */
      { uint64_t saved=s->bits; lc_sha_update(s,length,8); s->bits=saved; } }
    for(i=0;i<8;++i){digest[i*4]=(unsigned char)(s->state[i]>>24);digest[i*4+1]=(unsigned char)(s->state[i]>>16);digest[i*4+2]=(unsigned char)(s->state[i]>>8);digest[i*4+3]=(unsigned char)s->state[i];}
}
static LcValue lc_digest_value(LcContext *ctx, const unsigned char *digest, size_t length, int raw) {
    static const char digits[]="0123456789abcdef"; LcValue result; size_t i;
    if(raw)return lc_string_n(ctx,(const char *)digest,length);
    { char *hex=(char *)malloc(length*2+1);if(!hex){lc_error(ctx,"out of memory");return lc_nil();}for(i=0;i<length;++i){hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15];}result=lc_string_n(ctx,hex,length*2);free(hex);return result; }
}
static LcValue lc_hash_sha256(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *s=lc_lib_string_arg(ctx,argc,argv,0);LcSha256 sha;unsigned char digest[32];int raw=!strcmp(lc_lib_name(self),"sha256raw");if(!s)return lc_nil();lc_sha_init(&sha);lc_sha_update(&sha,s->data,s->length);lc_sha_final(&sha,digest);return lc_digest_value(ctx,digest,32,raw);
}
static LcValue lc_hash_hmac(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *key=lc_lib_string_arg(ctx,argc,argv,0),*message=lc_lib_string_arg(ctx,argc,argv,1);unsigned char k[64]={0},ipad[64],opad[64],inner[32],digest[32];LcSha256 sha;size_t i;
    if(!key||!message)return lc_nil();if(key->length>64){lc_sha_init(&sha);lc_sha_update(&sha,key->data,key->length);lc_sha_final(&sha,k);}else memcpy(k,key->data,key->length);
    for(i=0;i<64;++i){ipad[i]=k[i]^0x36;opad[i]=k[i]^0x5c;}lc_sha_init(&sha);lc_sha_update(&sha,ipad,64);lc_sha_update(&sha,message->data,message->length);lc_sha_final(&sha,inner);lc_sha_init(&sha);lc_sha_update(&sha,opad,64);lc_sha_update(&sha,inner,32);lc_sha_final(&sha,digest);return lc_digest_value(ctx,digest,32,0);
}

/* ----------------------------- filesystem ------------------------------- */
static int lc_fs_stat_path(const char *path, struct stat *st) {
#ifdef _WIN32
    return _stat(path, (struct _stat *)st);
#else
    return stat(path, st);
#endif
}
static LcValue lc_fs_stat(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *path=lc_lib_string_arg(ctx,argc,argv,0);struct stat st;LcValue result,value;(void)self;if(!path)return lc_nil();
    if(lc_fs_stat_path(path->data,&st)!=0)return lc_lib_failure(ctx,path->data);result=lc_table(ctx);
    value=lc_number((double)st.st_size);lc_set_field(ctx,result,"size",value);
    value=lc_number((double)st.st_mtime);lc_set_field(ctx,result,"modified",value);
#ifdef _WIN32
    value=lc_bool((st.st_mode&_S_IFMT)==_S_IFDIR);
#else
    value=lc_bool(S_ISDIR(st.st_mode));
#endif
    lc_set_field(ctx,result,"directory",value);
    value=lc_bool(!lc_truthy(value));lc_set_field(ctx,result,"file",value);
    value=lc_bool(!(st.st_mode&
#ifdef _WIN32
        _S_IWRITE
#else
        S_IWUSR
#endif
        ));lc_set_field(ctx,result,"readonly",value);return result;
}
static LcValue lc_fs_predicate(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcString *path=lc_lib_string_arg(ctx,argc,argv,0);struct stat st;const char *name=lc_lib_name(self);if(!path)return lc_nil();if(lc_fs_stat_path(path->data,&st)!=0)return !strcmp(name,"exists")?lc_bool(0):lc_nil();
#ifdef _WIN32
    if(!strcmp(name,"isdir"))return lc_bool((st.st_mode&_S_IFMT)==_S_IFDIR);if(!strcmp(name,"isfile"))return lc_bool((st.st_mode&_S_IFMT)==_S_IFREG);
#else
    if(!strcmp(name,"isdir"))return lc_bool(S_ISDIR(st.st_mode));if(!strcmp(name,"isfile"))return lc_bool(S_ISREG(st.st_mode));
#endif
    if(!strcmp(name,"size"))return lc_number((double)st.st_size);if(!strcmp(name,"modified"))return lc_number((double)st.st_mtime);return lc_bool(1);
}
static LcValue lc_fs_list(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcString *path=lc_lib_string_arg(ctx,argc,argv,0);LcValue result,item;int index=1;(void)self;if(!path)return lc_nil();result=lc_table(ctx);
#ifdef _WIN32
    {struct _finddata_t data;intptr_t handle;LcLibBuffer pattern={0};lc_lib_append(ctx,&pattern,path->data,path->length);if(path->length&&path->data[path->length-1]!='/'&&path->data[path->length-1]!='\\')lc_lib_putc(ctx,&pattern,'/');lc_lib_puts(ctx,&pattern,"*");handle=_findfirst(pattern.data,&data);free(pattern.data);if(handle==-1){lc_release(ctx,result);return lc_lib_failure(ctx,"findfirst");}do{if(strcmp(data.name,".")&&strcmp(data.name,"..")){item=lc_string(ctx,data.name);lc_table_add(ctx,result,index++,item);lc_release(ctx,item);}}while(_findnext(handle,&data)==0);_findclose(handle);}
#else
    {DIR *directory=opendir(path->data);struct dirent *entry;if(!directory){lc_release(ctx,result);return lc_lib_failure(ctx,path->data);}errno=0;while((entry=readdir(directory))){if(strcmp(entry->d_name,".")&&strcmp(entry->d_name,"..")){item=lc_string(ctx,entry->d_name);lc_table_add(ctx,result,index++,item);lc_release(ctx,item);}}closedir(directory);}
#endif
    return result;
}
static LcValue lc_fs_directory(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    const char *name=lc_lib_name(self);LcString *path;(void)self;
    if(!strcmp(name,"getcwd")){char buffer[4096];
#ifdef _WIN32
        if(!_getcwd(buffer,sizeof(buffer)))
#else
        if(!getcwd(buffer,sizeof(buffer)))
#endif
            return lc_lib_failure(ctx,"getcwd");return lc_string(ctx,buffer);}
    path=lc_lib_string_arg(ctx,argc,argv,0);if(!path)return lc_nil();
    if(!strcmp(name,"mkdir")){int status;
#ifdef _WIN32
        status=_mkdir(path->data);
#else
        status=mkdir(path->data,0777);
#endif
        return status==0?lc_bool(1):lc_lib_failure(ctx,path->data);}
    if(!strcmp(name,"rmdir")){int status;
#ifdef _WIN32
        status=_rmdir(path->data);
#else
        status=rmdir(path->data);
#endif
        return status==0?lc_bool(1):lc_lib_failure(ctx,path->data);}
    if(!strcmp(name,"chdir")){int status;
#ifdef _WIN32
        status=_chdir(path->data);
#else
        status=chdir(path->data);
#endif
        return status==0?lc_bool(1):lc_lib_failure(ctx,path->data);}
    if(!strcmp(name,"remove"))return remove(path->data)==0?lc_bool(1):lc_lib_failure(ctx,path->data);
    return lc_nil();
}
static LcValue lc_fs_rename(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcString *from=lc_lib_string_arg(ctx,argc,argv,0),*to=lc_lib_string_arg(ctx,argc,argv,1);(void)self;if(!from||!to)return lc_nil();return rename(from->data,to->data)==0?lc_bool(1):lc_lib_failure(ctx,"rename");}
static LcValue lc_fs_readfile(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcString *path=lc_lib_string_arg(ctx,argc,argv,0);FILE *file;LcLibBuffer out={0};char buffer[8192];size_t got;(void)self;if(!path)return lc_nil();file=fopen(path->data,"rb");if(!file)return lc_lib_failure(ctx,path->data);
    while((got=fread(buffer,1,sizeof(buffer),file))>0)if(!lc_lib_append(ctx,&out,buffer,got))break;if(ferror(file)){free(out.data);fclose(file);return lc_lib_failure(ctx,path->data);}fclose(file);return lc_lib_buffer_value(ctx,&out);
}
static LcValue lc_fs_writefile(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcString *path=lc_lib_string_arg(ctx,argc,argv,0),*content=lc_lib_string_arg(ctx,argc,argv,1);FILE *file;size_t written=0;int append=!strcmp(lc_lib_name(self),"appendfile");if(!path||!content)return lc_nil();file=fopen(path->data,append?"ab":"wb");if(!file)return lc_lib_failure(ctx,path->data);
    while(written<content->length){size_t n=fwrite(content->data+written,1,content->length-written,file);if(!n)break;written+=n;}if(fclose(file)!=0||written!=content->length)return lc_lib_failure(ctx,path->data);return lc_number((double)written);
}
static LcValue lc_fs_copyfile(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcString *from=lc_lib_string_arg(ctx,argc,argv,0),*to=lc_lib_string_arg(ctx,argc,argv,1);FILE *input,*output;char buffer[8192];size_t got;int ok=1;(void)self;if(!from||!to)return lc_nil();input=fopen(from->data,"rb");if(!input)return lc_lib_failure(ctx,from->data);output=fopen(to->data,"wb");if(!output){fclose(input);return lc_lib_failure(ctx,to->data);}
    while((got=fread(buffer,1,sizeof(buffer),input))>0)if(fwrite(buffer,1,got,output)!=got){ok=0;break;}if(ferror(input))ok=0;if(fclose(output)!=0)ok=0;fclose(input);return ok?lc_bool(1):lc_lib_failure(ctx,"copy");
}
static LcValue lc_fs_touch(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcString *path=lc_lib_string_arg(ctx,argc,argv,0);FILE *file;(void)self;if(!path)return lc_nil();file=fopen(path->data,"ab");if(!file)return lc_lib_failure(ctx,path->data);return fclose(file)==0?lc_bool(1):lc_lib_failure(ctx,path->data);}
static LcValue lc_fs_mkdirs(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcString *path=lc_lib_string_arg(ctx,argc,argv,0);char *copy;size_t i,length;(void)self;if(!path)return lc_nil();copy=(char *)malloc(path->length+1);if(!copy){lc_error(ctx,"out of memory");return lc_nil();}memcpy(copy,path->data,path->length+1);length=path->length;
    for(i=1;i<=length;++i)if(copy[i]=='/'||copy[i]=='\\'||copy[i]==0){char saved=copy[i];struct stat st;copy[i]=0;if(*copy&&lc_fs_stat_path(copy,&st)!=0){
#ifdef _WIN32
            if(_mkdir(copy)!=0&&errno!=EEXIST)
#else
            if(mkdir(copy,0777)!=0&&errno!=EEXIST)
#endif
                {LcValue failure=lc_lib_failure(ctx,copy);copy[i]=saved;free(copy);return failure;}}
        copy[i]=saved;}free(copy);return lc_bool(1);
}

/* -------------------------------- paths --------------------------------- */
static int lc_path_sep(char c){return c=='/'||c=='\\';}
static LcValue lc_path_name(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcString *path=lc_lib_string_arg(ctx,argc,argv,0);const char *name=lc_lib_name(self);size_t end,start=0,i,dot=SIZE_MAX;if(!path)return lc_nil();end=path->length;while(end>1&&lc_path_sep(path->data[end-1]))--end;
    for(i=0;i<end;++i){if(lc_path_sep(path->data[i]))start=i+1;else if(path->data[i]=='.')dot=i;}
    if(!strcmp(name,"dirname")){size_t cut=start?start-1:0;while(cut>1&&lc_path_sep(path->data[cut-1]))--cut;return lc_string_n(ctx,cut?path->data:".",cut?cut:1);}
    if(!strcmp(name,"extension")||!strcmp(name,"ext"))return dot!=SIZE_MAX&&dot>=start?lc_string_n(ctx,path->data+dot,end-dot):lc_string(ctx,"");
    if(!strcmp(name,"stem")){size_t n=(dot!=SIZE_MAX&&dot>=start?dot:end)-start;return lc_string_n(ctx,path->data+start,n);}
    return lc_string_n(ctx,path->data+start,end-start);
}
static LcValue lc_path_join(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcLibBuffer out={0};int i;(void)self;for(i=0;i<argc;++i){LcString *part=lc_lib_string_arg(ctx,argc,argv,i);if(!part)break;if(!part->length)continue;if(out.length&&!lc_path_sep(out.data[out.length-1])&&!lc_path_sep(part->data[0]))lc_lib_putc(ctx,&out,
#ifdef _WIN32
        '\\'
#else
        '/'
#endif
        );lc_lib_append(ctx,&out,part->data,part->length);}return lc_lib_buffer_value(ctx,&out);}
static LcValue lc_path_isabsolute(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcString *path=lc_lib_string_arg(ctx,argc,argv,0);(void)self;if(!path)return lc_nil();
#ifdef _WIN32
    return lc_bool(path->length&&lc_path_sep(path->data[0])||(path->length>2&&isalpha((unsigned char)path->data[0])&&path->data[1]==':'));
#else
    return lc_bool(path->length&&path->data[0]=='/');
#endif
}
static LcValue lc_path_normalize(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcString *path=lc_lib_string_arg(ctx,argc,argv,0);LcValue parts,item,key;LcLibBuffer out={0};int absolute=0,i,count;(void)self;if(!path)return lc_nil();
#ifdef _WIN32
    absolute=path->length&&lc_path_sep(path->data[0]);if(path->length>1&&path->data[1]==':'){lc_lib_append(ctx,&out,path->data,2);}
#else
    absolute=path->length&&path->data[0]=='/';
#endif
    {LcValue args[2];args[0]=argv[0];args[1]=lc_string(ctx,"/");parts=lc_text_split(ctx,NULL,2,args);lc_release(ctx,args[1]);}count=lc_table_length(parts);
    /* A lexical normalizer: preserve leading .. and collapse interior pairs. */
    for(i=1;i<=count;++i){LcValue v=lc_table_raw_get(ctx,parts,lc_number(i));if(v.type==LC_STRING&&v.as.string->length&&strcmp(v.as.string->data,".")){
            if(!strcmp(v.as.string->data,"..")&&out.length){char *slash=strrchr(out.data,'/');if(slash){out.length=(size_t)(slash-out.data);out.data[out.length]=0;}else out.length=0;}
            else if(strcmp(v.as.string->data,"..")||!absolute){if(out.length&&out.data[out.length-1]!='/')lc_lib_putc(ctx,&out,'/');lc_lib_append(ctx,&out,v.as.string->data,v.as.string->length);}}
        lc_release(ctx,v);}lc_release(ctx,parts);if(absolute&&(out.length==0||out.data[0]!='/')){LcLibBuffer rooted={0};lc_lib_putc(ctx,&rooted,'/');lc_lib_append(ctx,&rooted,out.data?out.data:"",out.length);free(out.data);out=rooted;}if(!out.length)lc_lib_putc(ctx,&out,'.');(void)item;(void)key;return lc_lib_buffer_value(ctx,&out);
}
static LcValue lc_path_absolute(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcString *path=lc_lib_string_arg(ctx,argc,argv,0);char cwd[4096];LcValue args[3],result;(void)self;if(!path)return lc_nil();
#ifdef _WIN32
    if(!_getcwd(cwd,sizeof(cwd)))
#else
    if(!getcwd(cwd,sizeof(cwd)))
#endif
        return lc_lib_failure(ctx,"getcwd");args[0]=lc_string(ctx,cwd);args[1]=argv[0];result=lc_path_join(ctx,self,2,args);lc_release(ctx,args[0]);args[0]=result;result=lc_path_normalize(ctx,self,1,args);lc_release(ctx,args[0]);return result;}

/* -------------------------------- time ---------------------------------- */
static double lc_time_monotonic_seconds(void){
#ifdef _WIN32
    LARGE_INTEGER frequency,counter;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&counter);return (double)counter.QuadPart/(double)frequency.QuadPart;
#else
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (double)ts.tv_sec+(double)ts.tv_nsec/1000000000.0;
#endif
}
static LcValue lc_time_now(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){const char *name=lc_lib_name(self);double value;(void)argc;(void)argv;
    if(!strcmp(name,"monotonic")||!strcmp(name,"ticks"))value=lc_time_monotonic_seconds();else value=(double)time(NULL);
    if(!strcmp(name,"millis"))value*=1000.0;return lc_number(value);}
static LcValue lc_time_sleep(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){double seconds=lc_lib_number_arg(ctx,argc,argv,0,0);(void)self;if(seconds<0)seconds=0;
#ifdef _WIN32
    Sleep((DWORD)(seconds*1000.0));
#else
    {struct timespec duration;duration.tv_sec=(time_t)seconds;duration.tv_nsec=(long)((seconds-(double)duration.tv_sec)*1000000000.0);while(nanosleep(&duration,&duration)!=0&&errno==EINTR){}}
#endif
    return lc_bool(1);}
static LcValue lc_time_iso(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){time_t stamp=(time_t)lc_lib_number_arg(ctx,argc,argv,0,(double)time(NULL));struct tm fields;char buffer[64];int local=!strcmp(lc_lib_name(self),"localiso");
#ifdef _WIN32
    if(local)localtime_s(&fields,&stamp);else gmtime_s(&fields,&stamp);
#else
    if(local)localtime_r(&stamp,&fields);else gmtime_r(&stamp,&fields);
#endif
    strftime(buffer,sizeof(buffer),"%Y-%m-%dT%H:%M:%S",&fields);return lc_string(ctx,buffer);}
static LcValue lc_time_parts(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){time_t stamp=(time_t)lc_lib_number_arg(ctx,argc,argv,0,(double)time(NULL));struct tm fields;LcValue result,value;(void)self;
#ifdef _WIN32
    if(!strcmp(lc_lib_name(self),"localparts"))localtime_s(&fields,&stamp);else gmtime_s(&fields,&stamp);
#else
    if(!strcmp(lc_lib_name(self),"localparts"))localtime_r(&stamp,&fields);else gmtime_r(&stamp,&fields);
#endif
    result=lc_table(ctx);value=lc_number(fields.tm_year+1900);lc_set_field(ctx,result,"year",value);value=lc_number(fields.tm_mon+1);lc_set_field(ctx,result,"month",value);value=lc_number(fields.tm_mday);lc_set_field(ctx,result,"day",value);value=lc_number(fields.tm_hour);lc_set_field(ctx,result,"hour",value);value=lc_number(fields.tm_min);lc_set_field(ctx,result,"min",value);value=lc_number(fields.tm_sec);lc_set_field(ctx,result,"sec",value);value=lc_number(fields.tm_wday+1);lc_set_field(ctx,result,"wday",value);value=lc_number(fields.tm_yday+1);lc_set_field(ctx,result,"yday",value);return result;}

/* -------------------------------- sockets ------------------------------- */
#ifdef _WIN32
typedef SOCKET LcOsSocket;
#else
typedef int LcOsSocket;
#endif
static LcValue lc_socket_error(LcContext *ctx,const char *operation){
    LcValue out[2];char message[256];
#ifdef _WIN32
    snprintf(message,sizeof(message),"%s: Winsock error %d",operation,WSAGetLastError());
#else
    snprintf(message,sizeof(message),"%s: %s",operation,strerror(errno));
#endif
    out[0]=lc_nil();out[1]=lc_string(ctx,message);return lc_tuple_new(ctx,2,out,1);
}
static int lc_socket_startup(LcContext *ctx){
#ifdef _WIN32
    if(!ctx->socket_initialized){WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)!=0){lc_error(ctx,"WSAStartup failed");return 0;}ctx->socket_initialized=1;}
#endif
    (void)ctx;return 1;
}
static LcValue lc_socket_value(LcContext *ctx,LcOsSocket handle,int type){LcValue value=lc_nil();LcSocket *sock=(LcSocket *)lc_alloc(ctx,sizeof(*sock),LC_SOCKET);if(!sock)return value;sock->handle=(intptr_t)handle;sock->closed=handle==LC_INVALID_SOCKET;sock->type=type;sock->timeout_ms=-1;value.type=LC_SOCKET;value.as.socket=sock;return value;}
static LcSocket *lc_socket_arg(LcContext *ctx,int argc,LcValue *argv,int index,int allow_closed){LcValue value;if(index>=argc){lc_error(ctx,"socket expected");return NULL;}value=lc_first_value(argv[index]);if(value.type!=LC_SOCKET||(!allow_closed&&value.as.socket->closed)){lc_error(ctx,allow_closed?"socket expected":"open socket expected");return NULL;}return value.as.socket;}
static LcOsSocket lc_socket_handle(LcSocket *sock){return
#ifdef _WIN32
    (SOCKET)
#else
    (int)
#endif
    sock->handle;}
static int lc_socket_lookup(LcContext *ctx,const char *host,const char *port,int type,int passive,struct addrinfo **addresses){struct addrinfo hints;int status;memset(&hints,0,sizeof(hints));hints.ai_family=AF_UNSPEC;hints.ai_socktype=type;if(passive)hints.ai_flags=AI_PASSIVE;status=getaddrinfo(host&&*host?host:NULL,port,&hints,addresses);if(status!=0){lc_error(ctx,"address resolution failed: %s",gai_strerror(status));return 0;}return 1;}
static void lc_socket_apply_timeout(LcSocket *sock){long timeout=sock->timeout_ms;
#ifdef _WIN32
    DWORD ms=timeout<0?0:(DWORD)timeout;setsockopt(lc_socket_handle(sock),SOL_SOCKET,SO_RCVTIMEO,(const char *)&ms,sizeof(ms));setsockopt(lc_socket_handle(sock),SOL_SOCKET,SO_SNDTIMEO,(const char *)&ms,sizeof(ms));
#else
    struct timeval tv;if(timeout<0){tv.tv_sec=0;tv.tv_usec=0;}else{tv.tv_sec=timeout/1000;tv.tv_usec=(timeout%1000)*1000;}setsockopt(lc_socket_handle(sock),SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(lc_socket_handle(sock),SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
#endif
}
static LcValue lc_socket_create(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){int type=!strcmp(lc_lib_name(self),"udp")?SOCK_DGRAM:SOCK_STREAM;(void)argc;(void)argv;if(!lc_socket_startup(ctx))return lc_nil();return lc_socket_value(ctx,LC_INVALID_SOCKET,type);}
static LcValue lc_socket_open_create(LcContext *ctx,LcSocket *sock){struct addrinfo hints,*addresses=NULL,*at;LcOsSocket handle=LC_INVALID_SOCKET;int status;memset(&hints,0,sizeof(hints));hints.ai_family=AF_UNSPEC;hints.ai_socktype=sock->type;status=getaddrinfo(NULL,"0",&hints,&addresses);if(status==0){for(at=addresses;at;at=at->ai_next){handle=socket(at->ai_family,at->ai_socktype,at->ai_protocol);if(handle!=LC_INVALID_SOCKET)break;}freeaddrinfo(addresses);}if(handle==LC_INVALID_SOCKET){lc_error(ctx,"cannot create socket");return lc_nil();}sock->handle=(intptr_t)handle;sock->closed=0;lc_socket_apply_timeout(sock);return lc_bool(1);}
static LcValue lc_socket_connect(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,1);LcString *host=lc_lib_string_arg(ctx,argc,argv,1);struct addrinfo *addresses=NULL,*at;char port[32];int ok=0;if(!socket||!host)return lc_nil();snprintf(port,sizeof(port),"%d",(int)lc_lib_number_arg(ctx,argc,argv,2,0));
    if(!lc_socket_lookup(ctx,host->data,port,sock->type,0,&addresses))return lc_nil();
    for(at=addresses;at;at=at->ai_next){if(sock->closed){sock->handle=(intptr_t)socket(at->ai_family,at->ai_socktype,at->ai_protocol);sock->closed=sock->handle==LC_INVALID_SOCKET;}if(!sock->closed&&connect(lc_socket_handle(sock),at->ai_addr,(int)at->ai_addrlen)==0){ok=1;break;}if(!sock->closed){lc_socket_close_handle(sock);}}
    freeaddrinfo(addresses);return ok?lc_retain(argv[0]):lc_socket_error(ctx,"connect");
}
static LcValue lc_socket_bind(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,1);LcString *host=lc_lib_string_arg(ctx,argc,argv,1);struct addrinfo *addresses=NULL,*at;char port[32];int ok=0,reuse=1;if(!socket||!host)return lc_nil();snprintf(port,sizeof(port),"%d",(int)lc_lib_number_arg(ctx,argc,argv,2,0));if(!lc_socket_lookup(ctx,host->data,port,sock->type,1,&addresses))return lc_nil();
    for(at=addresses;at;at=at->ai_next){if(sock->closed){sock->handle=(intptr_t)socket(at->ai_family,at->ai_socktype,at->ai_protocol);sock->closed=sock->handle==LC_INVALID_SOCKET;}if(sock->closed)continue;setsockopt(lc_socket_handle(sock),SOL_SOCKET,SO_REUSEADDR,(const char *)&reuse,sizeof(reuse));if(bind(lc_socket_handle(sock),at->ai_addr,(int)at->ai_addrlen)==0){ok=1;break;}lc_socket_close_handle(sock);}freeaddrinfo(addresses);return ok?lc_retain(argv[0]):lc_socket_error(ctx,"bind");
}
static LcValue lc_socket_listen(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,0);int backlog=(int)lc_lib_number_arg(ctx,argc,argv,1,32);if(!sock)return lc_nil();return listen(lc_socket_handle(sock),backlog)==0?lc_retain(argv[0]):lc_socket_error(ctx,"listen");}
static int lc_socket_wait(LcSocket *sock,int write){fd_set set;struct timeval tv,*pointer=NULL;int status;FD_ZERO(&set);FD_SET(lc_socket_handle(sock),&set);if(sock->timeout_ms>=0){tv.tv_sec=sock->timeout_ms/1000;tv.tv_usec=(sock->timeout_ms%1000)*1000;pointer=&tv;}status=select((int)lc_socket_handle(sock)+1,write?NULL:&set,write?&set:NULL,NULL,pointer);return status;}
static LcValue lc_socket_accept(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcSocket *server=lc_socket_arg(ctx,argc,argv,0,0);LcOsSocket handle;int waited;(void)argc;(void)argv;if(!server)return lc_nil();if(server->timeout_ms>=0){waited=lc_socket_wait(server,0);if(waited==0){LcValue out[2];out[0]=lc_nil();out[1]=lc_string(ctx,"timeout");return lc_tuple_new(ctx,2,out,1);}if(waited<0)return lc_socket_error(ctx,"select");}handle=accept(lc_socket_handle(server),NULL,NULL);if(handle==LC_INVALID_SOCKET)return lc_socket_error(ctx,"accept");return lc_socket_value(ctx,handle,SOCK_STREAM);}
static LcValue lc_socket_close(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,1);(void)ctx;(void)self;(void)argc;(void)argv;if(!sock)return lc_nil();lc_socket_close_handle(sock);return lc_bool(1);}
static LcValue lc_socket_send(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,0);LcString *data=lc_lib_string_arg(ctx,argc,argv,1);size_t sent=0;(void)self;if(!socket||!data)return lc_nil();while(sent<data->length){int flags=0;int n;
#ifndef _WIN32
#ifdef MSG_NOSIGNAL
        flags=MSG_NOSIGNAL;
#endif
#endif
        n=send(lc_socket_handle(sock),data->data+sent,(int)(data->length-sent>INT32_MAX?INT32_MAX:data->length-sent),flags);if(n<=0)return lc_socket_error(ctx,"send");sent+=(size_t)n;}return lc_number((double)sent);}
static LcValue lc_socket_receive(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,0);LcLibBuffer out={0};int amount=-1,line=0;char buffer[8192];if(!sock)return lc_nil();if(argc>1){LcValue format=lc_first_value(argv[1]);if(format.type==LC_STRING){line=!strcmp(format.as.string->data,"*l")||!strcmp(format.as.string->data,"l");if(!strcmp(format.as.string->data,"*a")||!strcmp(format.as.string->data,"a"))amount=-2;}else amount=(int)lc_lib_number_arg(ctx,argc,argv,1,8192);}else line=1;
    if(amount>=0){if(!lc_lib_reserve(ctx,&out,(size_t)amount))return lc_nil();while((int)out.length<amount){int n=recv(lc_socket_handle(sock),buffer,(int)(amount-(int)out.length>8192?8192:amount-(int)out.length),0);if(n<=0)break;lc_lib_append(ctx,&out,buffer,(size_t)n);}}
    else if(amount==-2){for(;;){int n=recv(lc_socket_handle(sock),buffer,sizeof(buffer),0);if(n<=0)break;lc_lib_append(ctx,&out,buffer,(size_t)n);}}
    else{for(;;){char c;int n=recv(lc_socket_handle(sock),&c,1,0);if(n<=0)break;if(c=='\n')break;if(c!='\r')lc_lib_putc(ctx,&out,c);}}
    if(!out.length){free(out.data);return lc_socket_error(ctx,"receive");}return lc_lib_buffer_value(ctx,&out);
}
static LcValue lc_socket_sendto(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,1);LcString *data=lc_lib_string_arg(ctx,argc,argv,1),*host=lc_lib_string_arg(ctx,argc,argv,2);struct addrinfo *addresses=NULL;char port[32];int sent;(void)self;if(!socket||!data||!host)return lc_nil();snprintf(port,sizeof(port),"%d",(int)lc_lib_number_arg(ctx,argc,argv,3,0));if(sock->closed){struct addrinfo hints;memset(&hints,0,sizeof(hints));hints.ai_socktype=sock->type;hints.ai_family=AF_UNSPEC;}if(!lc_socket_lookup(ctx,host->data,port,sock->type,0,&addresses))return lc_nil();if(sock->closed){sock->handle=(intptr_t)socket(addresses->ai_family,addresses->ai_socktype,addresses->ai_protocol);sock->closed=sock->handle==LC_INVALID_SOCKET;}sent=sock->closed?-1:sendto(lc_socket_handle(sock),data->data,(int)data->length,0,addresses->ai_addr,(int)addresses->ai_addrlen);freeaddrinfo(addresses);return sent>=0?lc_number(sent):lc_socket_error(ctx,"sendto");}
static LcValue lc_socket_recvfrom(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,0);int amount=(int)lc_lib_number_arg(ctx,argc,argv,1,8192);char *buffer;struct sockaddr_storage address;socklen_t length=sizeof(address);int got;LcValue out[3];char host[NI_MAXHOST],port[NI_MAXSERV];(void)self;if(!sock)return lc_nil();if(amount<1||amount>16*1024*1024)amount=8192;buffer=(char *)malloc((size_t)amount);if(!buffer){lc_error(ctx,"out of memory");return lc_nil();}got=recvfrom(lc_socket_handle(sock),buffer,amount,0,(struct sockaddr *)&address,&length);if(got<0){free(buffer);return lc_socket_error(ctx,"recvfrom");}out[0]=lc_string_n(ctx,buffer,(size_t)got);free(buffer);if(getnameinfo((struct sockaddr *)&address,length,host,sizeof(host),port,sizeof(port),NI_NUMERICHOST|NI_NUMERICSERV)==0){out[1]=lc_string(ctx,host);out[2]=lc_number(strtod(port,NULL));}else{out[1]=lc_nil();out[2]=lc_nil();}return lc_tuple_new(ctx,3,out,1);}
static LcValue lc_socket_timeout(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,1);double seconds=lc_lib_number_arg(ctx,argc,argv,1,-1);(void)self;if(!sock)return lc_nil();sock->timeout_ms=seconds<0?-1:(int)(seconds*1000.0);if(!sock->closed)lc_socket_apply_timeout(sock);return lc_bool(1);}
static LcValue lc_socket_option(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,0);LcString *name=lc_lib_string_arg(ctx,argc,argv,1);int enabled=argc<3||lc_truthy(lc_first_value(argv[2])),status=0;(void)self;if(!socket||!name)return lc_nil();
#ifdef _WIN32
    if(!strcmp(name->data,"nonblocking")){u_long mode=enabled;status=ioctlsocket(lc_socket_handle(sock),FIONBIO,&mode);}else
#else
    if(!strcmp(name->data,"nonblocking")){int flags=fcntl(lc_socket_handle(sock),F_GETFL,0);status=flags<0?-1:fcntl(lc_socket_handle(sock),F_SETFL,enabled?flags|O_NONBLOCK:flags&~O_NONBLOCK);}else
#endif
    if(!strcmp(name->data,"reuseaddr"))status=setsockopt(lc_socket_handle(sock),SOL_SOCKET,SO_REUSEADDR,(const char *)&enabled,sizeof(enabled));
    else if(!strcmp(name->data,"broadcast"))status=setsockopt(lc_socket_handle(sock),SOL_SOCKET,SO_BROADCAST,(const char *)&enabled,sizeof(enabled));
    else if(!strcmp(name->data,"keepalive"))status=setsockopt(lc_socket_handle(sock),SOL_SOCKET,SO_KEEPALIVE,(const char *)&enabled,sizeof(enabled));
    else if(!strcmp(name->data,"nodelay"))status=setsockopt(lc_socket_handle(sock),IPPROTO_TCP,TCP_NODELAY,(const char *)&enabled,sizeof(enabled));
#ifdef SO_REUSEPORT
    else if(!strcmp(name->data,"reuseport"))status=setsockopt(lc_socket_handle(sock),SOL_SOCKET,SO_REUSEPORT,(const char *)&enabled,sizeof(enabled));
#endif
    else{lc_error(ctx,"unknown socket option: %s",name->data);return lc_nil();}return status==0?lc_bool(1):lc_socket_error(ctx,"setoption");}
static LcValue lc_socket_name(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,0);struct sockaddr_storage address;socklen_t length=sizeof(address);char host[NI_MAXHOST],port[NI_MAXSERV];LcValue out[2];int peer=!strcmp(lc_lib_name(self),"getpeername");int status=peer?getpeername(lc_socket_handle(sock),(struct sockaddr *)&address,&length):getsockname(lc_socket_handle(sock),(struct sockaddr *)&address,&length);if(status!=0)return lc_socket_error(ctx,peer?"getpeername":"getsockname");if(getnameinfo((struct sockaddr *)&address,length,host,sizeof(host),port,sizeof(port),NI_NUMERICHOST|NI_NUMERICSERV)!=0)return lc_socket_error(ctx,"getnameinfo");out[0]=lc_string(ctx,host);out[1]=lc_number(strtod(port,NULL));return lc_tuple_new(ctx,2,out,1);}
static LcValue lc_socket_shutdown(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcSocket *sock=lc_socket_arg(ctx,argc,argv,0,0);LcString *mode=argc>1?lc_lib_string_arg(ctx,argc,argv,1):NULL;int how=
#ifdef _WIN32
    SD_BOTH;if(mode&&!strcmp(mode->data,"send"))how=SD_SEND;else if(mode&&!strcmp(mode->data,"receive"))how=SD_RECEIVE;
#else
    SHUT_RDWR;if(mode&&!strcmp(mode->data,"send"))how=SHUT_WR;else if(mode&&!strcmp(mode->data,"receive"))how=SHUT_RD;
#endif
    (void)self;if(!sock)return lc_nil();return shutdown(lc_socket_handle(sock),how)==0?lc_bool(1):lc_socket_error(ctx,"shutdown");}
static LcValue lc_socket_resolve(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcString *host=lc_lib_string_arg(ctx,argc,argv,0);struct addrinfo *addresses=NULL,*at;LcValue list=lc_table(ctx),item;int index=1;char numeric[NI_MAXHOST];if(!host)return lc_nil();if(!lc_socket_lookup(ctx,host->data,"0",SOCK_STREAM,0,&addresses))return lc_nil();for(at=addresses;at;at=at->ai_next)if(getnameinfo(at->ai_addr,(socklen_t)at->ai_addrlen,numeric,sizeof(numeric),NULL,0,NI_NUMERICHOST)==0){item=lc_string(ctx,numeric);lc_table_add(ctx,list,index++,item);lc_release(ctx,item);}freeaddrinfo(addresses);if(!strcmp(lc_lib_name(self),"resolve")){item=lc_table_raw_get(ctx,list,lc_number(1));lc_release(ctx,list);return item;}return list;}
static LcValue lc_socket_hostname(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){char name[256];(void)self;(void)argc;(void)argv;if(!lc_socket_startup(ctx))return lc_nil();return gethostname(name,sizeof(name))==0?lc_string(ctx,name):lc_socket_error(ctx,"gethostname");}
static void lc_socket_fill_set(LcContext *ctx,LcValue table,fd_set *set,int *maximum){int i,n=lc_table_length(table);for(i=1;i<=n;++i){LcValue value=lc_table_raw_get(ctx,table,lc_number(i));if(value.type==LC_SOCKET&&!value.as.socket->closed){LcOsSocket handle=lc_socket_handle(value.as.socket);FD_SET(handle,set);if((int)handle>*maximum)*maximum=(int)handle;}lc_release(ctx,value);}}
static LcValue lc_socket_ready_set(LcContext *ctx,LcValue table,fd_set *set){LcValue result=lc_table(ctx);int i,n=lc_table_length(table),index=1;for(i=1;i<=n;++i){LcValue value=lc_table_raw_get(ctx,table,lc_number(i));if(value.type==LC_SOCKET&&!value.as.socket->closed&&FD_ISSET(lc_socket_handle(value.as.socket),set))lc_table_add(ctx,result,index++,value);lc_release(ctx,value);}return result;}
static LcValue lc_socket_select(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){fd_set read_set,write_set;int maximum=-1,status;double timeout=lc_lib_number_arg(ctx,argc,argv,2,-1);struct timeval tv,*pointer=NULL;LcValue out[2];(void)self;FD_ZERO(&read_set);FD_ZERO(&write_set);if(argc>0&&lc_first_value(argv[0]).type==LC_TABLE)lc_socket_fill_set(ctx,argv[0],&read_set,&maximum);if(argc>1&&lc_first_value(argv[1]).type==LC_TABLE)lc_socket_fill_set(ctx,argv[1],&write_set,&maximum);if(timeout>=0){tv.tv_sec=(time_t)timeout;tv.tv_usec=(long)((timeout-(time_t)timeout)*1000000);pointer=&tv;}status=select(maximum+1,&read_set,&write_set,NULL,pointer);if(status<0)return lc_socket_error(ctx,"select");out[0]=argc>0&&lc_first_value(argv[0]).type==LC_TABLE?lc_socket_ready_set(ctx,argv[0],&read_set):lc_table(ctx);out[1]=argc>1&&lc_first_value(argv[1]).type==LC_TABLE?lc_socket_ready_set(ctx,argv[1],&write_set):lc_table(ctx);return lc_tuple_new(ctx,2,out,1);}
static LcValue lc_socket_quick(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue args[4],value;int server=!strcmp(lc_lib_name(self),"bind");args[0]=lc_socket_create(ctx,self,0,NULL);if(lc_first_value(args[0]).type!=LC_SOCKET)return args[0];args[1]=argv[0];args[2]=argv[1];value=server?lc_socket_bind(ctx,self,3,args):lc_socket_connect(ctx,self,3,args);if(lc_first_value(value).type==LC_NIL){lc_release(ctx,args[0]);return value;}lc_release(ctx,value);if(server){args[1]=lc_number(32);value=lc_socket_listen(ctx,self,2,args);lc_release(ctx,args[1]);if(lc_first_value(value).type==LC_NIL){lc_release(ctx,args[0]);return value;}lc_release(ctx,value);}return args[0];}

/* -------------------------------- HTTP ---------------------------------- */
#include "luac_tls.h"
static int lc_http_dechunk(LcContext *ctx,LcLibBuffer *body){LcLibBuffer decoded={0};size_t position=0;while(position<body->length){char *end;unsigned long size=strtoul(body->data+position,&end,16);const char *line=strstr(body->data+position,"\r\n");if(!line||end>line){free(decoded.data);return 0;}position=(size_t)(line-body->data)+2;if(size==0)break;if(size>body->length-position){free(decoded.data);return 0;}lc_lib_append(ctx,&decoded,body->data+position,size);position+=size;if(position+2>body->length||memcmp(body->data+position,"\r\n",2)){free(decoded.data);return 0;}position+=2;}free(body->data);*body=decoded;return 1;}
static LcValue lc_http_request(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *url = lc_lib_string_arg(ctx, argc, argv, 0);
    const char *method = "GET", *body = NULL, *host_start, *host_end, *path, *colon;
    size_t body_length = 0, host_length;
    int port, secure = 0, status = 0, chunked = 0;
    char port_text[16], host[256], buffer[8192];
    struct addrinfo *addresses = NULL, *at;
    LcOsSocket handle = LC_INVALID_SOCKET;
    LcLibBuffer request = {0}, response = {0}, content = {0}, headers_text = {0}, target = {0};
    LcValue headers, result, out[3];
    const char *header_end;
    LcTls tls;
    memset(&tls, 0, sizeof(tls));
    if (!url) return lc_nil();
    if (argc > 1 && lc_first_value(argv[1]).type == LC_STRING) method = lc_first_value(argv[1]).as.string->data;
    else if (!strcmp(lc_lib_name(self), "post")) method = "POST";
    if (argc > 2 && lc_first_value(argv[2]).type == LC_STRING) {
        body = lc_first_value(argv[2]).as.string->data;
        body_length = lc_first_value(argv[2]).as.string->length;
    }
    if (!strncmp(url->data, "https://", 8)) { secure = 1; host_start = url->data + 8; port = 443; }
    else if (!strncmp(url->data, "http://", 7)) { host_start = url->data + 7; port = 80; }
    else {
        LcValue error[2]; error[0] = lc_nil(); error[1] = lc_string(ctx, "URL must start with http:// or https://");
        return lc_tuple_new(ctx, 2, error, 1);
    }
    host_end = host_start + strcspn(host_start, "/?#");
    path = *host_end ? host_end : "/";
    colon = memchr(host_start, ':', (size_t)(host_end - host_start));
    if (colon) { port = atoi(colon + 1); host_length = (size_t)(colon - host_start); }
    else host_length = (size_t)(host_end - host_start);
    if (!host_length || host_length >= sizeof(host)) {
        snprintf(tls.error, sizeof(tls.error), "invalid HTTP host"); return lc_tls_failure(ctx, &tls);
    }
    memcpy(host, host_start, host_length); host[host_length] = 0;
    if (*path == '?') { lc_lib_putc(ctx, &target, '/'); }
    lc_lib_puts(ctx, &target, path);
    snprintf(port_text, sizeof(port_text), "%d", port);
    if (!lc_socket_startup(ctx)) { free(target.data); return lc_nil(); }
    { struct addrinfo hints; int lookup;
      memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
      lookup = getaddrinfo(host, port_text, &hints, &addresses);
      if (lookup != 0) { snprintf(tls.error, sizeof(tls.error), "HTTP address resolution: %s", gai_strerror(lookup));
        free(target.data); return lc_tls_failure(ctx, &tls); } }
    for (at = addresses; at; at = at->ai_next) {
        handle = socket(at->ai_family, at->ai_socktype, at->ai_protocol);
        if (handle != LC_INVALID_SOCKET && connect(handle, at->ai_addr, (int)at->ai_addrlen) == 0) break;
        if (handle != LC_INVALID_SOCKET) {
#ifdef _WIN32
            closesocket(handle);
#else
            close(handle);
#endif
            handle = LC_INVALID_SOCKET;
        }
    }
    freeaddrinfo(addresses);
    if (handle == LC_INVALID_SOCKET) { free(target.data); return lc_socket_error(ctx, "HTTP connect"); }
    if (secure && lc_tls_start(ctx, &tls, &handle, host) != 0) {
#ifdef _WIN32
        closesocket(handle);
#else
        close(handle);
#endif
        free(target.data); return lc_tls_failure(ctx, &tls);
    }
    lc_lib_printf(ctx, &request, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: LuaC/0.5\r\nAccept: */*\r\nConnection: close\r\n",
                  method, target.data ? target.data : "/", host);
    free(target.data);
    if (body) {
        lc_lib_printf(ctx, &request, "Content-Length: %d\r\n", (int)body_length);
        if (argc > 3 && lc_first_value(argv[3]).type == LC_STRING)
            lc_lib_printf(ctx, &request, "Content-Type: %s\r\n", lc_first_value(argv[3]).as.string->data);
    }
    lc_lib_puts(ctx, &request, "\r\n");
    if (body) lc_lib_append(ctx, &request, body, body_length);
    {
        size_t sent = 0; int failed = 0;
        while (sent < request.length) {
            int n = secure ? lc_tls_write(&tls, request.data + sent, request.length - sent)
                           : send(handle, request.data + sent, (int)(request.length - sent), 0);
            if (n <= 0) { failed = 1; break; }
            sent += (size_t)n;
        }
        free(request.data);
        if (failed) {
            if (secure) lc_tls_cleanup(&tls);
#ifdef _WIN32
            closesocket(handle);
#else
            close(handle);
#endif
            return secure ? lc_tls_failure(ctx, &tls) : lc_socket_error(ctx, "HTTP write");
        }
    }
    for (;;) {
        int n = secure ? lc_tls_read(&tls, buffer, sizeof(buffer)) : recv(handle, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        lc_lib_append(ctx, &response, buffer, (size_t)n);
    }
    if (secure) lc_tls_cleanup(&tls);
#ifdef _WIN32
    closesocket(handle);
#else
    close(handle);
#endif
    header_end = strstr(response.data ? response.data : "", "\r\n\r\n");
    if (!header_end || sscanf(response.data, "HTTP/%*s %d", &status) != 1) {
        free(response.data); return lc_socket_error(ctx, "invalid HTTP response");
    }
    lc_lib_append(ctx, &headers_text, response.data, (size_t)(header_end - response.data));
    lc_lib_append(ctx, &content, header_end + 4, response.length - (size_t)(header_end + 4 - response.data));
    {
        const char *line = strstr(headers_text.data, "\r\n");
        headers = lc_table(ctx);
        while (line) {
            const char *next = strstr(line + 2, "\r\n"), *end = next ? next : headers_text.data + headers_text.length;
            const char *separator = memchr(line + 2, ':', (size_t)(end - line - 2));
            if (separator) {
                LcValue name = lc_string_n(ctx, line + 2, (size_t)(separator - line - 2)), value;
                const char *v = separator + 1;
                while (v < end && *v == ' ') ++v;
                value = lc_string_n(ctx, v, (size_t)(end - v));
                if (name.type == LC_STRING) {
                    size_t i;
                    for (i = 0; i < name.as.string->length; ++i) name.as.string->data[i] = (char)tolower((unsigned char)name.as.string->data[i]);
                    if (!strcmp(name.as.string->data, "transfer-encoding") && value.type == LC_STRING && strstr(value.as.string->data, "chunked")) chunked = 1;
                }
                lc_table_raw_set(ctx, headers, name, value); lc_release(ctx, name); lc_release(ctx, value);
            }
            line = next;
        }
    }
    free(headers_text.data); free(response.data);
    if (chunked && !lc_http_dechunk(ctx, &content)) {
        lc_release(ctx, headers); free(content.data); lc_error(ctx, "invalid chunked HTTP response"); return lc_nil();
    }
    out[0] = lc_lib_buffer_value(ctx, &content); out[1] = lc_number(status); out[2] = headers;
    result = lc_tuple_new(ctx, 3, out, 1); return result;
}
static LcValue lc_http_tls_available(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    (void)self; (void)argc; (void)argv;
#ifdef LUAC_WITH_MBEDTLS
    return lc_bool(1);
#else
    return lc_bool(0);
#endif
}
static LcValue lc_http_setca(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcString *path = lc_lib_string_arg(ctx, argc, argv, 0); (void)self;
    if (!path) return lc_nil();
    if (path->length >= sizeof(ctx->tls_ca_file)) { lc_error(ctx, "CA path too long"); return lc_nil(); }
    memcpy(ctx->tls_ca_file, path->data, path->length + 1); return lc_bool(1);
}
static LcValue lc_http_setverify(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    (void)self; ctx->tls_verify = argc < 1 || lc_truthy(lc_first_value(argv[0])); return lc_bool(ctx->tls_verify);
}
static LcValue lc_http_get(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue args[4];args[0]=argv[0];args[1]=lc_string(ctx,"GET");args[2]=lc_nil();args[3]=lc_nil();{LcValue result=lc_http_request(ctx,self,4,args);lc_release(ctx,args[1]);return result;}}
static LcValue lc_http_post(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue args[4];args[0]=argv[0];args[1]=lc_string(ctx,"POST");args[2]=argc>1?argv[1]:lc_string(ctx,"");args[3]=argc>2?argv[2]:lc_string(ctx,"application/octet-stream");{LcValue result=lc_http_request(ctx,self,4,args);lc_release(ctx,args[1]);if(argc<=1)lc_release(ctx,args[2]);if(argc<=2)lc_release(ctx,args[3]);return result;}}

/* ------------------------------ vector math ----------------------------- */
static LcValue lc_vec_pair(LcContext *ctx,double x,double y){LcValue out[2];out[0]=lc_number(x);out[1]=lc_number(y);return lc_tuple_new(ctx,2,out,1);}
static LcValue lc_vec_binary(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){double x=lc_lib_number_arg(ctx,argc,argv,0,0),y=lc_lib_number_arg(ctx,argc,argv,1,0),a=lc_lib_number_arg(ctx,argc,argv,2,0),b=lc_lib_number_arg(ctx,argc,argv,3,0);const char *name=lc_lib_name(self);
    if(!strcmp(name,"add"))return lc_vec_pair(ctx,x+a,y+b);if(!strcmp(name,"sub"))return lc_vec_pair(ctx,x-a,y-b);if(!strcmp(name,"mul"))return lc_vec_pair(ctx,x*a,y*b);if(!strcmp(name,"dot"))return lc_number(x*a+y*b);if(!strcmp(name,"cross"))return lc_number(x*b-y*a);if(!strcmp(name,"distance"))return lc_number(hypot(x-a,y-b));if(!strcmp(name,"distance2")){double dx=x-a,dy=y-b;return lc_number(dx*dx+dy*dy);}if(!strcmp(name,"lerp"))return lc_vec_pair(ctx,x+(a-x)*b,y+(a-y)*b);return lc_nil();}
static LcValue lc_vec_unary(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){double x=lc_lib_number_arg(ctx,argc,argv,0,0),y=lc_lib_number_arg(ctx,argc,argv,1,0),p=lc_lib_number_arg(ctx,argc,argv,2,0),q=lc_lib_number_arg(ctx,argc,argv,3,0);const char *name=lc_lib_name(self);double length=hypot(x,y);
    if(!strcmp(name,"length"))return lc_number(length);if(!strcmp(name,"length2"))return lc_number(x*x+y*y);if(!strcmp(name,"normalize"))return length>0?lc_vec_pair(ctx,x/length,y/length):lc_vec_pair(ctx,0,0);if(!strcmp(name,"scale"))return lc_vec_pair(ctx,x*p,y*p);if(!strcmp(name,"perpendicular"))return lc_vec_pair(ctx,-y,x);if(!strcmp(name,"angle"))return lc_number(atan2(y,x));if(!strcmp(name,"rotate")){double c=cos(p),s=sin(p);return lc_vec_pair(ctx,x*c-y*s,x*s+y*c);}if(!strcmp(name,"clamp")){double max=p;if(length>max&&length>0)return lc_vec_pair(ctx,x/length*max,y/length*max);return lc_vec_pair(ctx,x,y);}if(!strcmp(name,"reflect")){double nlen=hypot(p,q);if(nlen==0)return lc_vec_pair(ctx,x,y);p/=nlen;q/=nlen;{double dot=x*p+y*q;return lc_vec_pair(ctx,x-2*dot*p,y-2*dot*q);}}return lc_nil();}

/* ------------------------------- engine --------------------------------- */
static LcValue lc_engine_get(LcContext *ctx,LcValue table,const char *name){LcValue key=lc_string(ctx,name),value=lc_table_raw_get(ctx,table,key);lc_release(ctx,key);return value;}
static double lc_engine_number(LcContext *ctx,LcValue table,const char *name,double fallback){LcValue value=lc_engine_get(ctx,table,name);double result=value.type==LC_NUMBER?value.as.number:fallback;lc_release(ctx,value);return result;}
static void lc_engine_set_number(LcContext *ctx,LcValue table,const char *name,double number){lc_set_field(ctx,table,name,lc_number(number));}
static int lc_engine_world(LcContext *ctx,LcValue value){value=lc_first_value(value);if(value.type!=LC_TABLE){lc_error(ctx,"engine world expected");return 0;}return 1;}
static LcValue lc_engine_world_new(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue world=lc_table(ctx),entities=lc_table(ctx),camera=lc_table(ctx);(void)self;(void)argc;(void)argv;lc_set_field(ctx,world,"entities",entities);lc_engine_set_number(ctx,world,"next_id",1);lc_engine_set_number(ctx,world,"time",0);lc_set_field(ctx,world,"running",lc_bool(1));lc_engine_set_number(ctx,camera,"x",0);lc_engine_set_number(ctx,camera,"y",0);lc_engine_set_number(ctx,camera,"zoom",1);lc_set_field(ctx,world,"camera",camera);return world;}
static LcValue lc_engine_find(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue entities,id,entity;(void)self;if(argc<2||!lc_engine_world(ctx,argv[0]))return lc_nil();entities=lc_engine_get(ctx,argv[0],"entities");id=lc_number(lc_lib_number_arg(ctx,argc,argv,1,0));entity=lc_table_raw_get(ctx,entities,id);lc_release(ctx,entities);return entity;}
static LcValue lc_engine_create(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcValue entities,entity,color,id;int number;if(argc<1||!lc_engine_world(ctx,argv[0]))return lc_nil();entities=lc_engine_get(ctx,argv[0],"entities");number=(int)lc_engine_number(ctx,argv[0],"next_id",1);id=lc_number(number);entity=lc_table(ctx);lc_engine_set_number(ctx,entity,"id",number);
    if(argc>1&&lc_first_value(argv[1]).type==LC_STRING)lc_set_field(ctx,entity,"name",lc_retain(lc_first_value(argv[1])));else lc_set_field(ctx,entity,"name",lc_string(ctx,"Entity"));
    lc_engine_set_number(ctx,entity,"x",lc_lib_number_arg(ctx,argc,argv,2,0));lc_engine_set_number(ctx,entity,"y",lc_lib_number_arg(ctx,argc,argv,3,0));lc_engine_set_number(ctx,entity,"vx",0);lc_engine_set_number(ctx,entity,"vy",0);lc_engine_set_number(ctx,entity,"rotation",0);lc_engine_set_number(ctx,entity,"angular_velocity",0);lc_engine_set_number(ctx,entity,"width",32);lc_engine_set_number(ctx,entity,"height",32);lc_engine_set_number(ctx,entity,"scale",1);lc_set_field(ctx,entity,"active",lc_bool(1));
    color=lc_table(ctx);lc_table_add(ctx,color,1,lc_number(0.3));lc_table_add(ctx,color,2,lc_number(0.8));lc_table_add(ctx,color,3,lc_number(1));lc_table_add(ctx,color,4,lc_number(1));lc_set_field(ctx,entity,"color",color);lc_set_field(ctx,entity,"components",lc_table(ctx));lc_set_field(ctx,entity,"tags",lc_table(ctx));
    lc_table_raw_set(ctx,entities,id,entity);lc_engine_set_number(ctx,argv[0],"next_id",number+1);lc_release(ctx,entities);return entity;
}
static LcValue lc_engine_destroy(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue entities,id,existing;if(argc<2||!lc_engine_world(ctx,argv[0]))return lc_nil();entities=lc_engine_get(ctx,argv[0],"entities");id=lc_number(lc_lib_number_arg(ctx,argc,argv,1,0));existing=lc_table_raw_get(ctx,entities,id);if(existing.type!=LC_NIL)lc_table_raw_set(ctx,entities,id,lc_nil());{int found=existing.type!=LC_NIL;lc_release(ctx,existing);lc_release(ctx,entities);return lc_bool(found);}}
static LcValue lc_engine_list(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue entities,result;int i,index=1;if(argc<1||!lc_engine_world(ctx,argv[0]))return lc_nil();entities=lc_engine_get(ctx,argv[0],"entities");result=lc_table(ctx);for(i=0;i<(int)entities.as.table->capacity;++i)if(entities.as.table->entries[i].used){LcValue value=!strcmp(lc_lib_name(self),"ids")?lc_retain(entities.as.table->entries[i].key):lc_retain(entities.as.table->entries[i].value);lc_table_add(ctx,result,index++,value);lc_release(ctx,value);}lc_release(ctx,entities);return result;}
static LcValue lc_engine_count(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue entities;double count;(void)self;if(argc<1||!lc_engine_world(ctx,argv[0]))return lc_nil();entities=lc_engine_get(ctx,argv[0],"entities");count=(double)entities.as.table->length;lc_release(ctx,entities);return lc_number(count);}
static LcValue lc_engine_clear(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue entities;(void)self;if(argc<1||!lc_engine_world(ctx,argv[0]))return lc_nil();entities=lc_table(ctx);lc_set_field(ctx,argv[0],"entities",entities);lc_engine_set_number(ctx,argv[0],"next_id",1);lc_engine_set_number(ctx,argv[0],"time",0);return lc_bool(1);}
static LcValue lc_engine_mutate(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue entity;const char *name=lc_lib_name(self);double x,y;if(argc<1||lc_first_value(argv[0]).type!=LC_TABLE){lc_error(ctx,"entity expected");return lc_nil();}entity=argv[0];x=lc_lib_number_arg(ctx,argc,argv,1,0);y=lc_lib_number_arg(ctx,argc,argv,2,0);
    if(!strcmp(name,"set_position")){lc_engine_set_number(ctx,entity,"x",x);lc_engine_set_number(ctx,entity,"y",y);}else if(!strcmp(name,"set_velocity")){lc_engine_set_number(ctx,entity,"vx",x);lc_engine_set_number(ctx,entity,"vy",y);}else if(!strcmp(name,"set_size")){lc_engine_set_number(ctx,entity,"width",x);lc_engine_set_number(ctx,entity,"height",y);}else if(!strcmp(name,"translate")){lc_engine_set_number(ctx,entity,"x",lc_engine_number(ctx,entity,"x",0)+x);lc_engine_set_number(ctx,entity,"y",lc_engine_number(ctx,entity,"y",0)+y);}else if(!strcmp(name,"set_rotation"))lc_engine_set_number(ctx,entity,"rotation",x);else if(!strcmp(name,"rotate"))lc_engine_set_number(ctx,entity,"rotation",lc_engine_number(ctx,entity,"rotation",0)+x);else if(!strcmp(name,"set_scale"))lc_engine_set_number(ctx,entity,"scale",x);else if(!strcmp(name,"set_angular_velocity"))lc_engine_set_number(ctx,entity,"angular_velocity",x);else if(!strcmp(name,"set_active"))lc_set_field(ctx,entity,"active",lc_bool(lc_truthy(lc_first_value(argv[1]))));return lc_retain(entity);}
static LcValue lc_engine_color(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue color;int i;if(argc<1||lc_first_value(argv[0]).type!=LC_TABLE){lc_error(ctx,"entity expected");return lc_nil();}color=lc_engine_get(ctx,argv[0],"color");if(color.type!=LC_TABLE){lc_release(ctx,color);color=lc_table(ctx);lc_set_field(ctx,argv[0],"color",lc_retain(color));}for(i=0;i<4;++i){double channel=lc_lib_number_arg(ctx,argc,argv,i+1,i==3?1:1);if(channel<0)channel=0;if(channel>1)channel=1;lc_table_add(ctx,color,i+1,lc_number(channel));}lc_release(ctx,color);return lc_retain(argv[0]);}
static LcValue lc_engine_update(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue entities,running;double dt=lc_lib_number_arg(ctx,argc,argv,1,0);int i;(void)self;if(argc<1||!lc_engine_world(ctx,argv[0]))return lc_nil();if(dt<0)dt=0;running=lc_engine_get(ctx,argv[0],"running");if(!lc_truthy(running)){lc_release(ctx,running);return lc_number(0);}lc_release(ctx,running);entities=lc_engine_get(ctx,argv[0],"entities");for(i=0;i<(int)entities.as.table->capacity;++i)if(entities.as.table->entries[i].used){LcValue entity=entities.as.table->entries[i].value,active=lc_engine_get(ctx,entity,"active");if(lc_truthy(active)){double x=lc_engine_number(ctx,entity,"x",0),y=lc_engine_number(ctx,entity,"y",0),vx=lc_engine_number(ctx,entity,"vx",0),vy=lc_engine_number(ctx,entity,"vy",0);lc_engine_set_number(ctx,entity,"x",x+vx*dt);lc_engine_set_number(ctx,entity,"y",y+vy*dt);lc_engine_set_number(ctx,entity,"rotation",lc_engine_number(ctx,entity,"rotation",0)+lc_engine_number(ctx,entity,"angular_velocity",0)*dt);}lc_release(ctx,active);}lc_release(ctx,entities);lc_engine_set_number(ctx,argv[0],"time",lc_engine_number(ctx,argv[0],"time",0)+dt);return lc_number(dt);}
static LcValue lc_engine_running(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){(void)self;if(argc<1||!lc_engine_world(ctx,argv[0]))return lc_nil();if(!strcmp(lc_lib_name(self),"pause"))lc_set_field(ctx,argv[0],"running",lc_bool(0));else lc_set_field(ctx,argv[0],"running",lc_bool(1));return lc_bool(1);}
static int lc_engine_overlap(LcContext *ctx,LcValue a,LcValue b){double ax=lc_engine_number(ctx,a,"x",0),ay=lc_engine_number(ctx,a,"y",0),bx=lc_engine_number(ctx,b,"x",0),by=lc_engine_number(ctx,b,"y",0),aw=lc_engine_number(ctx,a,"width",0)*lc_engine_number(ctx,a,"scale",1),ah=lc_engine_number(ctx,a,"height",0)*lc_engine_number(ctx,a,"scale",1),bw=lc_engine_number(ctx,b,"width",0)*lc_engine_number(ctx,b,"scale",1),bh=lc_engine_number(ctx,b,"height",0)*lc_engine_number(ctx,b,"scale",1);return fabs(ax-bx)*2<aw+bw&&fabs(ay-by)*2<ah+bh;}
static LcValue lc_engine_intersects(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){(void)self;if(argc<2||lc_first_value(argv[0]).type!=LC_TABLE||lc_first_value(argv[1]).type!=LC_TABLE){lc_error(ctx,"two entities expected");return lc_nil();}return lc_bool(lc_engine_overlap(ctx,argv[0],argv[1]));}
static LcValue lc_engine_collisions(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue target,entities,result;int i,index=1;(void)self;if(argc<2||!lc_engine_world(ctx,argv[0]))return lc_nil();if(lc_first_value(argv[1]).type==LC_TABLE)target=lc_retain(lc_first_value(argv[1]));else{target=lc_engine_find(ctx,self,argc,argv);}if(target.type==LC_NIL)return target;entities=lc_engine_get(ctx,argv[0],"entities");result=lc_table(ctx);for(i=0;i<(int)entities.as.table->capacity;++i)if(entities.as.table->entries[i].used){LcValue other=entities.as.table->entries[i].value;if(other.as.table!=target.as.table&&lc_engine_overlap(ctx,target,other))lc_table_add(ctx,result,index++,other);}lc_release(ctx,entities);lc_release(ctx,target);return result;}
static LcValue lc_engine_bounds(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue out[4];double scale;(void)self;if(argc<1||lc_first_value(argv[0]).type!=LC_TABLE){lc_error(ctx,"entity expected");return lc_nil();}scale=lc_engine_number(ctx,argv[0],"scale",1);out[0]=lc_number(lc_engine_number(ctx,argv[0],"x",0)-lc_engine_number(ctx,argv[0],"width",0)*scale/2);out[1]=lc_number(lc_engine_number(ctx,argv[0],"y",0)-lc_engine_number(ctx,argv[0],"height",0)*scale/2);out[2]=lc_number(lc_engine_number(ctx,argv[0],"width",0)*scale);out[3]=lc_number(lc_engine_number(ctx,argv[0],"height",0)*scale);return lc_tuple_new(ctx,4,out,1);}
static LcValue lc_engine_component(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue components,key,value;const char *name=lc_lib_name(self);if(argc<2||lc_first_value(argv[0]).type!=LC_TABLE){lc_error(ctx,"entity expected");return lc_nil();}components=lc_engine_get(ctx,argv[0],"components");key=argv[1];if(!strcmp(name,"get_component"))value=lc_table_raw_get(ctx,components,key);else if(!strcmp(name,"remove_component")){lc_table_raw_set(ctx,components,key,lc_nil());value=lc_bool(1);}else{if(argc<3)value=lc_nil();else value=lc_retain(argv[2]);lc_table_raw_set(ctx,components,key,value);}lc_release(ctx,components);return value;}
static LcValue lc_engine_tag(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue tags,key;const char *name=lc_lib_name(self);if(argc<2||lc_first_value(argv[0]).type!=LC_TABLE){lc_error(ctx,"entity expected");return lc_nil();}tags=lc_engine_get(ctx,argv[0],"tags");key=argv[1];if(!strcmp(name,"has_tag")){LcValue value=lc_table_raw_get(ctx,tags,key);int found=lc_truthy(value);lc_release(ctx,value);lc_release(ctx,tags);return lc_bool(found);}lc_table_raw_set(ctx,tags,key,!strcmp(name,"add_tag")?lc_bool(1):lc_nil());lc_release(ctx,tags);return lc_retain(argv[0]);}
static LcValue lc_engine_camera(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue camera;(void)self;if(argc<1||!lc_engine_world(ctx,argv[0]))return lc_nil();camera=lc_engine_get(ctx,argv[0],"camera");if(argc>1)lc_engine_set_number(ctx,camera,"x",lc_lib_number_arg(ctx,argc,argv,1,0));if(argc>2)lc_engine_set_number(ctx,camera,"y",lc_lib_number_arg(ctx,argc,argv,2,0));if(argc>3)lc_engine_set_number(ctx,camera,"zoom",lc_lib_number_arg(ctx,argc,argv,3,1));return camera;}
static LcValue lc_engine_project(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){LcValue camera;double x,y,zoom,cx,cy;(void)self;if(argc<1||!lc_engine_world(ctx,argv[0]))return lc_nil();x=lc_lib_number_arg(ctx,argc,argv,1,0);y=lc_lib_number_arg(ctx,argc,argv,2,0);camera=lc_engine_get(ctx,argv[0],"camera");cx=lc_engine_number(ctx,camera,"x",0);cy=lc_engine_number(ctx,camera,"y",0);zoom=lc_engine_number(ctx,camera,"zoom",1);lc_release(ctx,camera);if(zoom==0)zoom=1;return !strcmp(lc_lib_name(self),"world_to_screen")?lc_vec_pair(ctx,(x-cx)*zoom,(y-cy)*zoom):lc_vec_pair(ctx,x/zoom+cx,y/zoom+cy);}

/* --------------------------- library registration ----------------------- */
static void lc_lib_register(LcContext *ctx,const char *name,LcValue table){
    LcValue package,loaded;lc_set_global(ctx,name,lc_retain(table));package=lc_engine_get(ctx,ctx->globals,"package");loaded=lc_engine_get(ctx,package,"loaded");lc_set_field(ctx,loaded,name,table);lc_release(ctx,loaded);lc_release(ctx,package);
}
static void lc_register_libraries(LcContext *ctx) {
    LcValue text=lc_table(ctx),hex=lc_table(ctx),base64=lc_table(ctx),url=lc_table(ctx),json=lc_table(ctx),hash=lc_table(ctx),fs=lc_table(ctx),path=lc_table(ctx),clock=lc_table(ctx),vec=lc_table(ctx),net=lc_table(ctx),http=lc_table(ctx),engine=lc_table(ctx);
    const char *simple[]={"trim","ltrim","rtrim","reverse","capitalize","title","swapcase",NULL};
    const char *predicates[]={"startswith","endswith","contains","find","count",NULL};
    const char *binary_vec[]={"add","sub","mul","dot","cross","distance","distance2","lerp",NULL};
    const char *unary_vec[]={"length","length2","normalize","scale","perpendicular","angle","rotate","clamp","reflect",NULL};
    const char *hashes[]={"crc32","adler32","fnv1a","djb2",NULL};
    const char *pred_fs[]={"exists","isfile","isdir","size","modified",NULL};
    const char *dir_fs[]={"getcwd","mkdir","rmdir","chdir","remove",NULL};
    const char *names_path[]={"basename","dirname","extension","ext","stem",NULL};
    const char *times[]={"now","monotonic","ticks","millis",NULL};
    int i;
    for(i=0;simple[i];++i)lc_add_native(ctx,text,simple[i],lc_text_simple);for(i=0;predicates[i];++i)lc_add_native(ctx,text,predicates[i],lc_text_predicate);
    lc_add_native(ctx,text,"split",lc_text_split);lc_add_native(ctx,text,"join",lc_text_join);lc_add_native(ctx,text,"replace",lc_text_replace);lc_add_native(ctx,text,"lpad",lc_text_pad);lc_add_native(ctx,text,"rpad",lc_text_pad);lc_add_native(ctx,text,"center",lc_text_pad);lc_add_native(ctx,text,"repeat",lc_text_repeat);lc_lib_register(ctx,"text",text);
    lc_add_native(ctx,hex,"encode",lc_hex_encode);lc_add_native(ctx,hex,"decode",lc_hex_decode);lc_lib_register(ctx,"hex",hex);
    lc_add_native(ctx,base64,"encode",lc_base64_encode);lc_add_native(ctx,base64,"decode",lc_base64_decode);lc_lib_register(ctx,"base64",base64);
    lc_add_native(ctx,url,"encode",lc_url_encode);lc_add_native(ctx,url,"decode",lc_url_decode);lc_add_native(ctx,url,"queryencode",lc_url_encode);lc_add_native(ctx,url,"querydecode",lc_url_decode);lc_add_native(ctx,url,"parse",lc_url_parse);lc_add_native(ctx,url,"build",lc_url_build);lc_lib_register(ctx,"url",url);
    lc_add_native(ctx,json,"encode",lc_json_encode);lc_add_native(ctx,json,"decode",lc_json_decode);lc_lib_register(ctx,"json",json);
    for(i=0;hashes[i];++i)lc_add_native(ctx,hash,hashes[i],lc_hash32);lc_add_native(ctx,hash,"sha256",lc_hash_sha256);lc_add_native(ctx,hash,"sha256raw",lc_hash_sha256);lc_add_native(ctx,hash,"hmac_sha256",lc_hash_hmac);lc_lib_register(ctx,"hash",hash);
    lc_add_native(ctx,fs,"stat",lc_fs_stat);for(i=0;pred_fs[i];++i)lc_add_native(ctx,fs,pred_fs[i],lc_fs_predicate);lc_add_native(ctx,fs,"list",lc_fs_list);lc_add_native(ctx,fs,"listdir",lc_fs_list);for(i=0;dir_fs[i];++i)lc_add_native(ctx,fs,dir_fs[i],lc_fs_directory);lc_add_native(ctx,fs,"mkdirs",lc_fs_mkdirs);lc_add_native(ctx,fs,"rename",lc_fs_rename);lc_add_native(ctx,fs,"readfile",lc_fs_readfile);lc_add_native(ctx,fs,"writefile",lc_fs_writefile);lc_add_native(ctx,fs,"appendfile",lc_fs_writefile);lc_add_native(ctx,fs,"copyfile",lc_fs_copyfile);lc_add_native(ctx,fs,"touch",lc_fs_touch);lc_lib_register(ctx,"fs",fs);
    for(i=0;names_path[i];++i)lc_add_native(ctx,path,names_path[i],lc_path_name);lc_add_native(ctx,path,"join",lc_path_join);lc_add_native(ctx,path,"isabsolute",lc_path_isabsolute);lc_add_native(ctx,path,"normalize",lc_path_normalize);lc_add_native(ctx,path,"absolute",lc_path_absolute);lc_lib_register(ctx,"path",path);
    for(i=0;times[i];++i)lc_add_native(ctx,clock,times[i],lc_time_now);lc_add_native(ctx,clock,"sleep",lc_time_sleep);lc_add_native(ctx,clock,"iso",lc_time_iso);lc_add_native(ctx,clock,"localiso",lc_time_iso);lc_add_native(ctx,clock,"parts",lc_time_parts);lc_add_native(ctx,clock,"localparts",lc_time_parts);lc_lib_register(ctx,"time",clock);
    for(i=0;binary_vec[i];++i)lc_add_native(ctx,vec,binary_vec[i],lc_vec_binary);for(i=0;unary_vec[i];++i)lc_add_native(ctx,vec,unary_vec[i],lc_vec_unary);lc_lib_register(ctx,"vec",vec);
    lc_add_native(ctx,net,"tcp",lc_socket_create);lc_add_native(ctx,net,"udp",lc_socket_create);lc_add_native(ctx,net,"connect",lc_socket_quick);lc_add_native(ctx,net,"bind",lc_socket_quick);lc_add_native(ctx,net,"select",lc_socket_select);lc_add_native(ctx,net,"resolve",lc_socket_resolve);lc_add_native(ctx,net,"addresses",lc_socket_resolve);lc_add_native(ctx,net,"gethostname",lc_socket_hostname);
    ctx->socket_methods=lc_table(ctx);lc_add_native(ctx,ctx->socket_methods,"connect",lc_socket_connect);lc_add_native(ctx,ctx->socket_methods,"bind",lc_socket_bind);lc_add_native(ctx,ctx->socket_methods,"listen",lc_socket_listen);lc_add_native(ctx,ctx->socket_methods,"accept",lc_socket_accept);lc_add_native(ctx,ctx->socket_methods,"send",lc_socket_send);lc_add_native(ctx,ctx->socket_methods,"receive",lc_socket_receive);lc_add_native(ctx,ctx->socket_methods,"recv",lc_socket_receive);lc_add_native(ctx,ctx->socket_methods,"sendto",lc_socket_sendto);lc_add_native(ctx,ctx->socket_methods,"receivefrom",lc_socket_recvfrom);lc_add_native(ctx,ctx->socket_methods,"recvfrom",lc_socket_recvfrom);lc_add_native(ctx,ctx->socket_methods,"close",lc_socket_close);lc_add_native(ctx,ctx->socket_methods,"settimeout",lc_socket_timeout);lc_add_native(ctx,ctx->socket_methods,"setoption",lc_socket_option);lc_add_native(ctx,ctx->socket_methods,"getsockname",lc_socket_name);lc_add_native(ctx,ctx->socket_methods,"getpeername",lc_socket_name);lc_add_native(ctx,ctx->socket_methods,"shutdown",lc_socket_shutdown);lc_lib_register(ctx,"socket",lc_retain(net));lc_lib_register(ctx,"net",net);
    lc_add_native(ctx,http,"request",lc_http_request);lc_add_native(ctx,http,"get",lc_http_get);lc_add_native(ctx,http,"post",lc_http_post);lc_add_native(ctx,http,"tls_available",lc_http_tls_available);lc_add_native(ctx,http,"setca",lc_http_setca);lc_add_native(ctx,http,"setverify",lc_http_setverify);lc_lib_register(ctx,"http",http);
    lc_add_native(ctx,engine,"world",lc_engine_world_new);lc_add_native(ctx,engine,"create",lc_engine_create);lc_add_native(ctx,engine,"entity",lc_engine_create);lc_add_native(ctx,engine,"find",lc_engine_find);lc_add_native(ctx,engine,"destroy",lc_engine_destroy);lc_add_native(ctx,engine,"list",lc_engine_list);lc_add_native(ctx,engine,"ids",lc_engine_list);lc_add_native(ctx,engine,"count",lc_engine_count);lc_add_native(ctx,engine,"clear",lc_engine_clear);lc_add_native(ctx,engine,"update",lc_engine_update);lc_add_native(ctx,engine,"pause",lc_engine_running);lc_add_native(ctx,engine,"resume",lc_engine_running);lc_add_native(ctx,engine,"intersects",lc_engine_intersects);lc_add_native(ctx,engine,"collisions",lc_engine_collisions);lc_add_native(ctx,engine,"bounds",lc_engine_bounds);lc_add_native(ctx,engine,"set_color",lc_engine_color);lc_add_native(ctx,engine,"add_component",lc_engine_component);lc_add_native(ctx,engine,"get_component",lc_engine_component);lc_add_native(ctx,engine,"remove_component",lc_engine_component);lc_add_native(ctx,engine,"add_tag",lc_engine_tag);lc_add_native(ctx,engine,"remove_tag",lc_engine_tag);lc_add_native(ctx,engine,"has_tag",lc_engine_tag);lc_add_native(ctx,engine,"camera",lc_engine_camera);lc_add_native(ctx,engine,"world_to_screen",lc_engine_project);lc_add_native(ctx,engine,"screen_to_world",lc_engine_project);
    {const char *mutations[]={"set_position","set_velocity","set_size","translate","set_rotation","rotate","set_scale","set_angular_velocity","set_active",NULL};for(i=0;mutations[i];++i)lc_add_native(ctx,engine,mutations[i],lc_engine_mutate);}lc_lib_register(ctx,"engine",engine);
    lc_audio_init(ctx);
}
#endif
