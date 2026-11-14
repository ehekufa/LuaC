#ifndef LUAC_PACK_H
#define LUAC_PACK_H

/* Lua 5.4 binary format support. Included after the core runtime helpers.
 * Format parsing and integer encoding were informed by Lua 5.4's lstrlib.c.
 *
 * Copyright (C) 1994-2024 Lua.org, PUC-Rio.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <limits.h>
#include <stddef.h>

typedef enum {
    LC_PACK_NONE, LC_PACK_SIGNED, LC_PACK_UNSIGNED, LC_PACK_FLOAT,
    LC_PACK_DOUBLE, LC_PACK_FIXED, LC_PACK_LENGTH, LC_PACK_ZERO,
    LC_PACK_PADDING, LC_PACK_ALIGN
} LcPackKind;

typedef struct {
    LcContext *ctx;
    const char *cursor;
    int little;
    int max_align;
} LcPackFormat;

typedef struct {
    LcPackKind kind;
    size_t size;
    size_t padding;
} LcPackOption;

static int lc_pack_native_little(void) {
    const uint16_t one = 1;
    return *(const unsigned char *)&one == 1;
}

static int lc_pack_default_align(void) {
    struct MaxAlignment {
        char byte;
        union { double number; void *pointer; int64_t integer; long native; } value;
    };
    return (int)offsetof(struct MaxAlignment, value);
}

static size_t lc_pack_number(LcPackFormat *format, size_t fallback, int *present) {
    size_t value = 0;
    *present = 0;
    while (*format->cursor >= '0' && *format->cursor <= '9') {
        unsigned digit = (unsigned)(*format->cursor++ - '0');
        *present = 1;
        if (value > ((size_t)INT_MAX - digit) / 10) {
            lc_error(format->ctx, "format size is too large");
            return 0;
        }
        value = value * 10 + digit;
    }
    return *present ? value : fallback;
}

static LcPackOption lc_pack_option(LcPackFormat *format) {
    LcPackOption option = {LC_PACK_NONE, 0, 0};
    int present;
    char code = *format->cursor++;
    switch (code) {
        case 'b': option.kind = LC_PACK_SIGNED; option.size = sizeof(char); break;
        case 'B': option.kind = LC_PACK_UNSIGNED; option.size = sizeof(char); break;
        case 'h': option.kind = LC_PACK_SIGNED; option.size = sizeof(short); break;
        case 'H': option.kind = LC_PACK_UNSIGNED; option.size = sizeof(short); break;
        case 'l': option.kind = LC_PACK_SIGNED; option.size = sizeof(long); break;
        case 'L': option.kind = LC_PACK_UNSIGNED; option.size = sizeof(long); break;
        case 'j': option.kind = LC_PACK_SIGNED; option.size = sizeof(int64_t); break;
        case 'J': option.kind = LC_PACK_UNSIGNED; option.size = sizeof(int64_t); break;
        case 'T': option.kind = LC_PACK_UNSIGNED; option.size = sizeof(size_t); break;
        case 'i': case 'I': case 's':
            option.kind = code == 'i' ? LC_PACK_SIGNED : code == 'I' ? LC_PACK_UNSIGNED : LC_PACK_LENGTH;
            option.size = lc_pack_number(format, code == 's' ? sizeof(size_t) : sizeof(int), &present);
            if (!option.size || option.size > 16) lc_error(format->ctx, "integer size must be between 1 and 16");
            break;
        case 'f': option.kind = LC_PACK_FLOAT; option.size = sizeof(float); break;
        case 'd': case 'n': option.kind = LC_PACK_DOUBLE; option.size = sizeof(double); break;
        case 'c':
            option.kind = LC_PACK_FIXED;
            option.size = lc_pack_number(format, 0, &present);
            if (!present) lc_error(format->ctx, "missing size for format option 'c'");
            break;
        case 'z': option.kind = LC_PACK_ZERO; break;
        case 'x': option.kind = LC_PACK_PADDING; option.size = 1; break;
        case 'X': option.kind = LC_PACK_ALIGN; break;
        case '<': format->little = 1; break;
        case '>': format->little = 0; break;
        case '=': format->little = lc_pack_native_little(); break;
        case '!':
            option.size = lc_pack_number(format, (size_t)lc_pack_default_align(), &present);
            if (!option.size || option.size > 16) lc_error(format->ctx, "alignment must be between 1 and 16");
            else format->max_align = (int)option.size;
            option.size = 0;
            break;
        case ' ': break;
        default: lc_error(format->ctx, "invalid format option '%c'", code); break;
    }
    return option;
}

static LcPackOption lc_pack_next(LcPackFormat *format, size_t offset) {
    LcPackOption option = lc_pack_option(format);
    size_t align = option.size;
    if (option.kind == LC_PACK_ALIGN) {
        LcPackOption next;
        if (!*format->cursor) { lc_error(format->ctx, "missing format option after 'X'"); return option; }
        next = lc_pack_option(format);
        align = next.size;
        if (next.kind == LC_PACK_FIXED || !align) lc_error(format->ctx, "invalid format option after 'X'");
    }
    if (option.kind == LC_PACK_FIXED || align <= 1) return option;
    if (align > (size_t)format->max_align) align = (size_t)format->max_align;
    if (align & (align - 1)) {
        lc_error(format->ctx, "format alignment is not a power of two");
        return option;
    }
    option.padding = (align - (offset & (align - 1))) & (align - 1);
    return option;
}

static int lc_pack_format_init(LcContext *ctx, int argc, LcValue *argv, LcPackFormat *format) {
    LcValue value = argc ? lc_first_value(argv[0]) : lc_nil();
    if (value.type != LC_STRING) { lc_error(ctx, "format string expected"); return 0; }
    format->ctx = ctx;
    format->cursor = value.as.string->data;
    format->little = lc_pack_native_little();
    format->max_align = 1;
    return 1;
}

static void lc_pack_append(LcContext *ctx, LcTextBuffer *buffer, const char *bytes, size_t length) {
    if (ctx->error) return;
    if (length > SIZE_MAX - buffer->length - 1) { lc_error(ctx, "packed string too large"); return; }
    lc_text_add(ctx, buffer, bytes, length);
}

static void lc_pack_zeros(LcContext *ctx, LcTextBuffer *buffer, size_t length) {
    static const char zeros[256] = {0};
    while (length && !ctx->error) {
        size_t part = length < sizeof(zeros) ? length : sizeof(zeros);
        lc_pack_append(ctx, buffer, zeros, part);
        length -= part;
    }
}

static void lc_pack_integer(unsigned char *bytes, uint64_t value, size_t size, int little, int sign_extend) {
    size_t i;
    for (i = 0; i < size; ++i) {
        unsigned char byte = i < 8 ? (unsigned char)(value >> (i * CHAR_BIT)) : (sign_extend ? 0xff : 0);
        bytes[little ? i : size - i - 1] = byte;
    }
}

static int64_t lc_unpack_integer(LcContext *ctx, const unsigned char *bytes, size_t size, int little, int is_signed) {
    size_t i, count = size < 8 ? size : 8;
    uint64_t value = 0;
    for (i = 0; i < count; ++i) value |= (uint64_t)bytes[little ? i : size - i - 1] << (i * CHAR_BIT);
    if (is_signed && size < 8 && (value & ((uint64_t)1 << (size * CHAR_BIT - 1))))
        value |= UINT64_MAX << (size * CHAR_BIT);
    for (i = 8; i < size; ++i) {
        unsigned char extension = is_signed && (value >> 63) ? 0xff : 0;
        if (bytes[little ? i : size - i - 1] != extension) {
            lc_error(ctx, "packed integer does not fit into a Lua integer");
            return 0;
        }
    }
    return (int64_t)value;
}

static void lc_pack_float_bytes(unsigned char *destination, const unsigned char *source, size_t size, int little) {
    size_t i;
    if (little == lc_pack_native_little()) memcpy(destination, source, size);
    else for (i = 0; i < size; ++i) destination[i] = source[size - i - 1];
}

static LcValue lc_pack_string_arg(LcContext *ctx, int argc, LcValue *argv, int index) {
    LcValue value = index < argc ? lc_first_value(argv[index]) : lc_nil();
    if (value.type == LC_STRING) return lc_retain(value);
    if (value.type == LC_INTEGER || value.type == LC_NUMBER) return lc_to_string(ctx, value);
    lc_error(ctx, "string expected for pack argument %d", index + 1);
    return lc_nil();
}

static LcValue lc_string_packsize(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcPackFormat format;
    size_t total = 0;
    (void)self;
    if (!lc_pack_format_init(ctx, argc, argv, &format)) return lc_nil();
    while (*format.cursor && !ctx->error) {
        LcPackOption option = lc_pack_next(&format, total);
        if (option.kind == LC_PACK_LENGTH || option.kind == LC_PACK_ZERO) {
            lc_error(ctx, "variable-length format"); break;
        }
        if (option.size > (size_t)INT64_MAX - total - option.padding) {
            lc_error(ctx, "format result too large"); break;
        }
        total += option.padding + option.size;
    }
    return ctx->error ? lc_nil() : lc_integer((int64_t)total);
}

static LcValue lc_string_pack(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcPackFormat format;
    LcTextBuffer buffer = {0};
    LcValue result;
    int argument = 1;
    (void)self;
    if (!lc_pack_format_init(ctx, argc, argv, &format)) return lc_nil();
    while (*format.cursor && !ctx->error) {
        LcPackOption option = lc_pack_next(&format, buffer.length);
        unsigned char bytes[16];
        LcValue value = argument < argc ? lc_first_value(argv[argument]) : lc_nil();
        lc_pack_zeros(ctx, &buffer, option.padding);
        if (ctx->error) break;
        switch (option.kind) {
            case LC_PACK_SIGNED: case LC_PACK_UNSIGNED: {
                int64_t number = lc_to_integer(ctx, value);
                if (ctx->error) break;
                if (option.size < 8) {
                    unsigned bits = (unsigned)option.size * CHAR_BIT;
                    if (option.kind == LC_PACK_SIGNED) {
                        int64_t limit = (int64_t)1 << (bits - 1);
                        if (number < -limit || number >= limit) { lc_error(ctx, "integer overflow in pack"); break; }
                    } else if ((uint64_t)number >= ((uint64_t)1 << bits)) {
                        lc_error(ctx, "unsigned overflow in pack"); break;
                    }
                }
                lc_pack_integer(bytes, (uint64_t)number, option.size, format.little,
                                option.kind == LC_PACK_SIGNED && number < 0);
                lc_pack_append(ctx, &buffer, (const char *)bytes, option.size);
                ++argument;
                break;
            }
            case LC_PACK_FLOAT: case LC_PACK_DOUBLE: {
                int ok;
                double number = lc_to_number(ctx, value, &ok);
                if (!ok) break;
                if (option.kind == LC_PACK_FLOAT) {
                    float f = (float)number;
                    lc_pack_float_bytes(bytes, (const unsigned char *)&f, sizeof(f), format.little);
                } else lc_pack_float_bytes(bytes, (const unsigned char *)&number, sizeof(number), format.little);
                lc_pack_append(ctx, &buffer, (const char *)bytes, option.size);
                ++argument;
                break;
            }
            case LC_PACK_FIXED: case LC_PACK_LENGTH: case LC_PACK_ZERO: {
                LcValue text = lc_pack_string_arg(ctx, argc, argv, argument++);
                size_t length;
                if (ctx->error) { lc_release(ctx, text); break; }
                length = text.as.string->length;
                if (option.kind == LC_PACK_FIXED && length > option.size)
                    lc_error(ctx, "string longer than fixed size");
                else if (option.kind == LC_PACK_ZERO && memchr(text.as.string->data, 0, length))
                    lc_error(ctx, "string contains zero bytes");
                else if (option.kind == LC_PACK_LENGTH && option.size < sizeof(size_t) &&
                         length >= ((size_t)1 << (option.size * CHAR_BIT)))
                    lc_error(ctx, "string length does not fit");
                if (option.kind == LC_PACK_LENGTH && !ctx->error) {
                    lc_pack_integer(bytes, (uint64_t)length, option.size, format.little, 0);
                    lc_pack_append(ctx, &buffer, (const char *)bytes, option.size);
                }
                lc_pack_append(ctx, &buffer, text.as.string->data, length);
                if (option.kind == LC_PACK_FIXED && !ctx->error) lc_pack_zeros(ctx, &buffer, option.size - length);
                if (option.kind == LC_PACK_ZERO && !ctx->error) lc_pack_zeros(ctx, &buffer, 1);
                lc_release(ctx, text);
                break;
            }
            case LC_PACK_PADDING: lc_pack_zeros(ctx, &buffer, 1); break;
            case LC_PACK_ALIGN: case LC_PACK_NONE: break;
        }
    }
    result = ctx->error ? lc_nil() : lc_string_n(ctx, buffer.data ? buffer.data : "", buffer.length);
    lc_text_free(&buffer);
    return result;
}

static int lc_pack_push_result(LcContext *ctx, LcValue **values, size_t *count, size_t *capacity, LcValue value) {
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 8;
        LcValue *items;
        if (next > INT_MAX || next > SIZE_MAX / sizeof(**values)) {
            lc_release(ctx, value); lc_error(ctx, "too many unpack results"); return 0;
        }
        items = (LcValue *)realloc(*values, next * sizeof(**values));
        if (!items) { lc_release(ctx, value); lc_error(ctx, "out of memory in unpack"); return 0; }
        *values = items; *capacity = next;
    }
    (*values)[(*count)++] = value;
    return 1;
}

static LcValue lc_string_unpack(LcContext *ctx, LcFunction *self, int argc, LcValue *argv) {
    LcPackFormat format;
    LcValue data, result = lc_nil(), *values = NULL;
    size_t offset, count = 0, capacity = 0, i;
    int64_t position = 1;
    (void)self;
    if (!lc_pack_format_init(ctx, argc, argv, &format)) return result;
    data = argc > 1 ? lc_first_value(argv[1]) : lc_nil();
    if (data.type != LC_STRING) { lc_error(ctx, "data string expected"); return result; }
    if (argc > 2 && lc_first_value(argv[2]).type != LC_NIL) position = lc_to_integer(ctx, argv[2]);
    if (ctx->error) return result;
    if (position < 0) {
        uint64_t magnitude = (uint64_t)(-(position + 1)) + 1;
        offset = magnitude > data.as.string->length ? 0 : data.as.string->length - (size_t)magnitude;
    } else if (position == 0) {
        offset = 0;
    } else {
        if ((uint64_t)(position - 1) > data.as.string->length) { lc_error(ctx, "initial position out of string"); return result; }
        offset = (size_t)(position - 1);
    }
    while (*format.cursor && !ctx->error) {
        LcPackOption option = lc_pack_next(&format, offset);
        const unsigned char *bytes;
        LcValue value = lc_nil();
        size_t length;
        int has_value = 1;
        if (ctx->error) break;
        if (option.padding > data.as.string->length - offset ||
            option.size > data.as.string->length - offset - option.padding) {
            lc_error(ctx, "data string too short"); break;
        }
        offset += option.padding;
        bytes = (const unsigned char *)data.as.string->data + offset;
        switch (option.kind) {
            case LC_PACK_SIGNED: case LC_PACK_UNSIGNED:
                value = lc_integer(lc_unpack_integer(ctx, bytes, option.size, format.little, option.kind == LC_PACK_SIGNED));
                break;
            case LC_PACK_FLOAT: {
                float f;
                lc_pack_float_bytes((unsigned char *)&f, bytes, sizeof(f), format.little);
                value = lc_number(f); break;
            }
            case LC_PACK_DOUBLE: {
                double d;
                lc_pack_float_bytes((unsigned char *)&d, bytes, sizeof(d), format.little);
                value = lc_number(d); break;
            }
            case LC_PACK_FIXED: value = lc_string_n(ctx, (const char *)bytes, option.size); break;
            case LC_PACK_LENGTH: {
                uint64_t encoded = (uint64_t)lc_unpack_integer(ctx, bytes, option.size, format.little, 0);
                if (ctx->error) break;
                if (encoded > data.as.string->length - offset - option.size) { lc_error(ctx, "data string too short"); break; }
                length = (size_t)encoded;
                value = lc_string_n(ctx, (const char *)bytes + option.size, length);
                offset += length;
                break;
            }
            case LC_PACK_ZERO: {
                const unsigned char *end = memchr(bytes, 0, data.as.string->length - offset);
                if (!end) { lc_error(ctx, "unfinished zero-terminated string"); break; }
                length = (size_t)(end - bytes);
                value = lc_string_n(ctx, (const char *)bytes, length);
                offset += length + 1;
                break;
            }
            case LC_PACK_PADDING: case LC_PACK_ALIGN: case LC_PACK_NONE: has_value = 0; break;
        }
        offset += option.size;
        if (ctx->error) { lc_release(ctx, value); break; }
        if (has_value && !lc_pack_push_result(ctx, &values, &count, &capacity, value)) break;
    }
    if (!ctx->error && lc_pack_push_result(ctx, &values, &count, &capacity, lc_integer((int64_t)offset + 1)))
        result = lc_tuple_new(ctx, (int)count, values, 1);
    if (ctx->error) for (i = 0; i < count; ++i) lc_release(ctx, values[i]);
    free(values);
    return result;
}

#endif
