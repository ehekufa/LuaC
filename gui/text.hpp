#pragma once
#include "nuklear_config.h"
#include "font_studio_mono.hpp"
#include <stdexcept>

/* The atlas owns the baked glyphs until the UI and command list are finished. */
class LcTextAtlas {
public:
    LcTextAtlas() {
        nk_font_atlas_init_default(&atlas_);
        nk_font_atlas_begin(&atlas_);
        struct nk_font_config config = nk_font_config(16.0f);
        config.range = lc_studio_glyph_ranges;
        config.fallback_glyph = '?';
        font_ = nk_font_atlas_add_from_memory(&atlas_,
            const_cast<unsigned char *>(lc_studio_mono_ttf), lc_studio_mono_ttf_size, 16.0f, &config);
        if (font_) pixels_ = nk_font_atlas_bake(&atlas_, &width_, &height_, NK_FONT_ATLAS_RGBA32);
        if (!font_ || !pixels_) {
            nk_font_atlas_clear(&atlas_);
            throw std::runtime_error("Could not bake the Studio font atlas");
        }
    }
    ~LcTextAtlas() { nk_font_atlas_clear(&atlas_); }
    LcTextAtlas(const LcTextAtlas &) = delete;
    LcTextAtlas &operator=(const LcTextAtlas &) = delete;
    nk_user_font *font() { return &font_->handle; }
    const nk_font *bakedFont() const { return font_; }
    const unsigned char *pixels() const { return static_cast<const unsigned char *>(pixels_); }
    int width() const { return width_; }
    int height() const { return height_; }
    void finish() {
        if (pixels_) {
            nk_font_atlas_end(&atlas_, nk_handle_id(1), nullptr);
            pixels_ = nullptr;
        }
    }
private:
    nk_font_atlas atlas_{};
    nk_font *font_ = nullptr;
    const void *pixels_ = nullptr;
    int width_ = 0, height_ = 0;
};
