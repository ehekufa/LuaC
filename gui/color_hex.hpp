#pragma once
#include "nuklear_config.h"
#include <cstdio>
#include <cstring>

/* Accept exactly six RGB hex digits, with an optional leading '#'. */
inline bool parseColorHex(const char *text, int length, nk_color &color) {
    if (!text) return false;
    int start = length > 0 && text[0] == '#' ? 1 : 0;
    if (length - start != 6) return false;
    unsigned value = 0;
    for (int i = start; i < length; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        value = (value << 4) | digit;
    }
    color = nk_rgb((value >> 16) & 255, (value >> 8) & 255, value & 255);
    return true;
}
inline void formatColorHex(nk_color color, char (&buffer)[8]) {
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.r, color.g, color.b);
}
