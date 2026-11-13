#pragma once
#include <cstdint>

struct LcVertex {
    float x, y;
    float r, g, b, a;
    float u = 0, v = 0;
};

struct LcDraw {
    uint32_t first = 0;
    uint32_t count = 0;
    int x = 0, y = 0;
    uint32_t width = 0, height = 0;
    uint32_t texture = 0;
};
