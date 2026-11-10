#include "nuklear_canvas.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
float width(nk_handle, float height, const char *text, int length) {
    return NuklearCanvas::textWidth(height, text, length);
}

bool frame(nk_context &ui, NuklearCanvas &canvas, bool &clicked, char *source, int &sourceLength, struct nk_rect &buttonBounds) {
    if (nk_begin(&ui, "test", nk_rect(0, 0, 480, 320), NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(&ui, 30, 1);
        buttonBounds = nk_widget_bounds(&ui);
        clicked = nk_button_label(&ui, "Run") != 0;
        nk_layout_row_dynamic(&ui, 180, 1);
        nk_edit_string(&ui, NK_EDIT_BOX, source, &sourceLength, 1024, nk_filter_default);
    }
    nk_end(&ui);
    canvas.render(ui, 480, 320);
    assert(!canvas.vertices.empty());
    assert(!canvas.draws.empty());
    size_t vertices = 0;
    for (const auto &draw : canvas.draws) {
        assert(draw.count > 0);
        assert(draw.first == vertices);
        assert(draw.width <= 480 && draw.height <= 320);
        vertices += draw.count;
    }
    assert(vertices == canvas.vertices.size());
    nk_clear(&ui);
    return clicked;
}
}

int main() {
    std::vector<nk_byte> arena(512 * 1024);
    nk_context ui{};
    nk_user_font font{};
    font.height = 16;
    font.width = width;
    assert(nk_init_fixed(&ui, arena.data(), arena.size(), &font));
    NuklearCanvas canvas;
    char source[1024] = "print('Nuklear')";
    int sourceLength = static_cast<int>(std::strlen(source));
    bool clicked = false;
    struct nk_rect buttonBounds{};
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    assert(!clicked);

    /* Feed a complete click through Nuklear's public input API. */
    nk_input_begin(&ui);
    int clickX = static_cast<int>(buttonBounds.x + buttonBounds.w / 2);
    int clickY = static_cast<int>(buttonBounds.y + buttonBounds.h / 2);
    nk_input_motion(&ui, clickX, clickY);
    nk_input_button(&ui, NK_BUTTON_LEFT, clickX, clickY, nk_true);
    nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    assert(clicked); /* Nuklear's default button trigger is mouse press. */
    nk_input_begin(&ui);
    nk_input_motion(&ui, clickX, clickY);
    nk_input_button(&ui, NK_BUTTON_LEFT, clickX, clickY, nk_false);
    nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    assert(!clicked);

    /* Focus the multiline editor and enter text through the public input API. */
    nk_input_begin(&ui);
    nk_input_motion(&ui, 30, 80);
    nk_input_button(&ui, NK_BUTTON_LEFT, 30, 80, nk_true);
    nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    nk_input_begin(&ui);
    nk_input_button(&ui, NK_BUTTON_LEFT, 30, 80, nk_false);
    nk_input_unicode(&ui, '!');
    nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    assert(sourceLength == 17);
    assert(std::strstr(source, "!") != nullptr);
    assert(ui.memory.allocated <= arena.size());
    nk_free(&ui);
    std::cout << "Nuklear fixed-arena input, editing and Vulkan geometry: OK\n";
}
