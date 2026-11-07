#pragma once
#include "pages_assets.h"

inline const BitmapFont &bitmapFontForSize(uint8_t size) {
    if (size == 48) return kFont48;
    if (size == 30) return kFont30;
    if (size == 14) return kFont14;
    if (size == 13) return kFont13;
    return kFont10;
}

inline int bitmapTextWidth(const char *text, const BitmapFont &font) {
    int width = 0;
    while (*text) {
        const uint8_t c = static_cast<uint8_t>(*text++);
        if (c >= 32 && c <= 126) width += font.glyphs[c - 32].advance;
    }
    return width;
}
inline int bitmapInkBottom(const char *text, const BitmapFont &font) {
    int bottom = 0;
    while (*text) {
        const uint8_t c = static_cast<uint8_t>(*text++);
        if (c < 32 || c > 126) continue;
        const auto &glyph = font.glyphs[c - 32];
        for (int row = font.height - 1; row >= bottom; --row) {
            bool ink = false;
            for (int col = 0; col < glyph.width; ++col) {
                const uint32_t bit = row * glyph.width + col;
                ink |= pgm_read_byte(font.bits + glyph.offset + bit / 8) & (0x80 >> (bit % 8));
            }
            if (ink) { bottom = row + 1; break; }
        }
    }
    return bottom;
}
inline const BitmapFont *fittedFont(const char *text, int width, int height,
                                   const BitmapFont &preferred) {
    const BitmapFont *fonts[] = {&preferred, &kFont14, &kFont13, &kFont10};
    for (const auto *font : fonts)
        if (bitmapTextWidth(text, *font) <= width && bitmapInkBottom(text, *font) <= height)
            return font;
    return nullptr;
}
