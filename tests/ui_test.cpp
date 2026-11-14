#include "nuklear_canvas.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::string clipboard;
void copy(nk_handle, const char *text, int len) { clipboard.assign(text, len); }
void paste(nk_handle, nk_text_edit *edit) {
    nk_textedit_paste(edit, clipboard.data(), static_cast<int>(clipboard.size()));
}
bool frame(nk_context &ui, NuklearCanvas &canvas, bool &clicked, char *source, int &sourceLength, struct nk_rect &buttonBounds) {
    if (nk_begin(&ui, "test", nk_rect(0, 0, 480, 320), NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(&ui, 30, 1);
        buttonBounds = nk_widget_bounds(&ui);
        clicked = nk_button_label(&ui, "Run") != 0;
        nk_layout_row_dynamic(&ui, 180, 1);
        nk_edit_string(&ui, NK_EDIT_BOX, source, &sourceLength, 1024, nk_filter_default);
        struct nk_image image{};
        image.handle = nk_handle_id(7); image.w = image.h = 100;
        image.region[0] = 10; image.region[1] = 20;
        image.region[2] = 30; image.region[3] = 40;
        nk_draw_image(nk_window_get_canvas(&ui), nk_rect(350, 20, 50, 60), &image, nk_rgb(255, 255, 255));
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
    NuklearCanvas canvas;
    nk_user_font *font = canvas.atlas().font();
    assert(canvas.atlas().pixels() && canvas.atlas().width() > 0 && canvas.atlas().height() > 0);
    canvas.atlas().finish();
    assert(nk_init_fixed(&ui, arena.data(), arena.size(), font));
    ui.clip.copy = copy; ui.clip.paste = paste;
    char source[1024] = "print('Nuklear')";
    int sourceLength = static_cast<int>(std::strlen(source));
    bool clicked = false;
    struct nk_rect buttonBounds{};
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    assert(!clicked);
    bool imageDraw = false;
    for (const auto &draw : canvas.draws) if (draw.texture == 7) {
        imageDraw = true;
        assert(draw.count == 6);
        assert(std::abs(canvas.vertices[draw.first].u - 0.1f) < 1e-6f);
        assert(std::abs(canvas.vertices[draw.first].v - 0.2f) < 1e-6f);
        assert(std::abs(canvas.vertices[draw.first + 4].u - 0.4f) < 1e-6f);
        assert(std::abs(canvas.vertices[draw.first + 4].v - 0.6f) < 1e-6f);
    }
    assert(imageDraw);

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
    nk_input_begin(&ui);
    nk_input_unicode(&ui, 0x041f);
    nk_input_unicode(&ui, 0x0440);
    nk_input_unicode(&ui, 0x0456);
    nk_input_unicode(&ui, 0x04d9);
    nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    assert(std::string(source, sourceLength).find("Пріә") != std::string::npos);

    nk_input_begin(&ui); nk_input_key(&ui, NK_KEY_TEXT_SELECT_ALL, nk_true); nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    nk_input_begin(&ui); nk_input_key(&ui, NK_KEY_TEXT_SELECT_ALL, nk_false); nk_input_key(&ui, NK_KEY_COPY, nk_true); nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    assert(clipboard == std::string(source, sourceLength));
    clipboard = "Привіт, беларуская, қазақша — café";
    nk_input_begin(&ui); nk_input_key(&ui, NK_KEY_COPY, nk_false); nk_input_key(&ui, NK_KEY_PASTE, nk_true); nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    assert(std::string(source, sourceLength) == clipboard);
    nk_input_begin(&ui); nk_input_key(&ui, NK_KEY_PASTE, nk_false);
    nk_input_key(&ui, NK_KEY_SHIFT, nk_true); nk_input_key(&ui, NK_KEY_LEFT, nk_true); nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    nk_input_begin(&ui); nk_input_key(&ui, NK_KEY_SHIFT, nk_false); nk_input_key(&ui, NK_KEY_LEFT, nk_false);
    nk_input_key(&ui, NK_KEY_COPY, nk_true); nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    assert(clipboard == "é");
    clipboard = "Привіт, беларуская, қазақша — café";
    nk_input_begin(&ui); nk_input_key(&ui, NK_KEY_COPY, nk_false); nk_input_key(&ui, NK_KEY_BACKSPACE, nk_true); nk_input_end(&ui);
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    assert(std::string(source, sourceLength) == clipboard.substr(0, clipboard.size() - std::strlen("é")));
    assert(ui.memory.allocated <= arena.size());
    const nk_font *atlas = canvas.atlas().bakedFont();
    for (nk_rune cp : {0x041fu, 0x044fu, 0x0457u, 0x045eu, 0x049bu, 0x04d9u, 0x052fu, 0x00e9u, 0x2026u}) {
        const nk_font_glyph *g = nk_font_find_glyph(atlas, cp);
        assert(g != atlas->fallback && g->codepoint == cp && g->xadvance > 0 && g->u1 > g->u0);
    }
    assert(nk_font_find_glyph(atlas, 0x4e2d) == atlas->fallback);
    const char *sample = "Привіт, беларускі, қазақша — café";
    float w = font->width(font->userdata, 16, sample, static_cast<int>(std::strlen(sample)));
    assert(w > 0 && std::abs(font->width(font->userdata, 32, sample, static_cast<int>(std::strlen(sample))) - 2*w) < 0.01f);
    std::snprintf(source, sizeof(source), "print('Привет, мир')"); sourceLength = static_cast<int>(std::strlen(source));
    frame(ui, canvas, clicked, source, sourceLength, buttonBounds);
    bool atlasDraw = false; for (const auto &draw : canvas.draws) if (draw.texture == 1) atlasDraw = true;
    assert(atlasDraw);
    /* Engine text uses the same metrics at larger sizes. */
    nk_user_font scaled = *font;
    scaled.height = 32;
    nk_input_begin(&ui); nk_input_end(&ui);
    if (nk_begin(&ui, "geometry", nk_rect(0, 0, 480, 320), NK_WINDOW_NO_SCROLLBAR)) {
        nk_draw_text(nk_window_get_canvas(&ui), nk_rect(20, 20, 400, 40), "Пя", 4,
                     &scaled, nk_rgba(0,0,0,0), nk_rgb(17, 41, 83));
    }
    nk_end(&ui);
    canvas.render(ui, 480, 320);
    std::vector<LcVertex> glyphs;
    for (const auto &v : canvas.vertices)
        if (std::abs(v.r - 17.0f/255) < 1e-6f && std::abs(v.g - 41.0f/255) < 1e-6f)
            glyphs.push_back(v);
    assert(glyphs.size() == 12);
    const nk_font_glyph *first = nk_font_find_glyph(atlas, 0x41f);
    const nk_font_glyph *second = nk_font_find_glyph(atlas, 0x44f);
    assert(std::abs(glyphs[0].x - (20 + first->x0 * 2)) < 0.01f);
    assert(std::abs(glyphs[6].x - (20 + (first->xadvance + second->x0) * 2)) < 0.01f);
    nk_clear(&ui);
    nk_free(&ui);
    std::cout << "Nuklear fixed-arena input, editing and Vulkan geometry: OK\n";
}
