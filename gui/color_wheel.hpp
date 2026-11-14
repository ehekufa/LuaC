#pragma once
#include "nuklear_config.h"
#include <algorithm>
#include <cmath>

/* A hue ring with saturation/value in the centre. */
class ColorWheel {
public:
    explicit ColorWheel(nk_color initial = nk_rgb(75, 220, 181)) {
        nk_color_hsv_f(&hue_, &saturation_, &value_, initial);
    }
    nk_color color() const { return nk_hsv_f(hue_, saturation_, value_); }
    void setColor(nk_color color) { nk_color_hsv_f(&hue_, &saturation_, &value_, color); }

    bool draw(nk_context *ui) {
        struct nk_rect area;
        nk_widget_layout_states state = nk_widget(&area, ui);
        if (state == NK_WIDGET_INVALID) return false;
        const float pi = 3.14159265359f;
        const float cx = area.x + area.w / 2, cy = area.y + area.h / 2;
        const float radius = std::min(area.w, area.h) * 0.47f;
        const float inner = radius * 0.76f, square = inner * 1.30f;
        const float left = cx - square / 2, top = cy - square / 2;
        const nk_input &input = ui->input;
        bool changed = false;
        if (state == NK_WIDGET_VALID && nk_input_is_mouse_pressed(&input, NK_BUTTON_LEFT)) {
            float dx = input.mouse.pos.x - cx, dy = input.mouse.pos.y - cy;
            float distance = std::sqrt(dx * dx + dy * dy);
            if (distance >= inner && distance <= radius) dragging_ = Ring;
            else if (input.mouse.pos.x >= left && input.mouse.pos.x <= left + square &&
                     input.mouse.pos.y >= top && input.mouse.pos.y <= top + square) dragging_ = Square;
        }
        if (state != NK_WIDGET_VALID || !input.mouse.buttons[NK_BUTTON_LEFT].down) dragging_ = None;
        if (dragging_ == Ring) {
            float h = std::atan2(input.mouse.pos.y - cy, input.mouse.pos.x - cx) / (2 * pi);
            if (h < 0) h += 1;
            changed = hue_ != h; hue_ = h;
        } else if (dragging_ == Square) {
            float s = std::clamp((input.mouse.pos.x - left) / square, 0.0f, 1.0f);
            float v = std::clamp(1.0f - (input.mouse.pos.y - top) / square, 0.0f, 1.0f);
            changed = saturation_ != s || value_ != v; saturation_ = s; value_ = v;
        }
        nk_command_buffer *out = nk_window_get_canvas(ui);
        const int segments = 96;
        for (int i = 0; i < segments; ++i) {
            float a = 2 * pi * i / segments, b = 2 * pi * (i + 1) / segments;
            float ax = std::cos(a), ay = std::sin(a), bx = std::cos(b), by = std::sin(b);
            nk_color color = nk_hsv_f((i + 0.5f) / segments, 1, 1);
            nk_fill_triangle(out, cx + radius*ax, cy + radius*ay, cx + radius*bx, cy + radius*by,
                             cx + inner*ax, cy + inner*ay, color);
            nk_fill_triangle(out, cx + radius*bx, cy + radius*by, cx + inner*bx, cy + inner*by,
                             cx + inner*ax, cy + inner*ay, color);
        }
        const int cells = 24;
        for (int y = 0; y < cells; ++y) for (int x = 0; x < cells; ++x) {
            nk_color color = nk_hsv_f(hue_, (x + 0.5f) / cells, 1.0f - (y + 0.5f) / cells);
            nk_fill_rect(out, nk_rect(left + square*x/cells, top + square*y/cells,
                                     square/cells + 0.5f, square/cells + 0.5f), 0, color);
        }
        float angle = hue_ * 2 * pi;
        float marker = (radius + inner) / 2;
        nk_stroke_circle(out, nk_rect(cx + marker*std::cos(angle) - 5, cy + marker*std::sin(angle) - 5, 10, 10), 2, nk_rgb(255,255,255));
        nk_stroke_circle(out, nk_rect(left + saturation_*square - 5, top + (1-value_)*square - 5, 10, 10), 2, nk_rgb(255,255,255));
        return changed;
    }
private:
    enum Drag { None, Ring, Square } dragging_ = None;
    float hue_ = 0, saturation_ = 1, value_ = 1;
};
