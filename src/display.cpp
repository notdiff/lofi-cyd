#include "display.h"
#include "background_rle.h"
#include "display_text.h"
#include "rabbit_sprite.h"

void DisplayRenderer::begin() {
    tft.init();
}
void DisplayRenderer::configure(uint8_t rotation, bool inverted, bool swap) {
    swapRedBlue = swap;
    tft.setRotation(rotation);
    tft.invertDisplay(inverted);
}
void DisplayRenderer::drawSettingsPanel() {
    tft.fillRect(23, 31, 278, 186, color(0x0028));
}
void DisplayRenderer::drawIndicator(uint16_t value) {
    tft.fillCircle(32, 211, 2, color(value));
}
uint16_t DisplayRenderer::color(uint16_t value) const {
    if (!swapRedBlue)
        return value;
    return (value & 0x07E0) | ((value & 0x001F) << 11) | ((value & 0xF800) >> 11);
}

void DisplayRenderer::drawBackgroundRect(Page page, int x, int y, int w, int h) {
    const uint16_t* background = kPageBackgrounds[static_cast<uint8_t>(page)];
    tft.setSwapBytes(true);
    for (int row = y; row < y + h; ++row) {
        if (page == Page::Details)
            detailsBackgroundRow(row, scanline);
        for (int col = 0; col < w; ++col)
            scanline[col] =
                color(page == Page::Details ? scanline[x + col]
                                            : pgm_read_word(background + row * 320 + x + col));
        tft.pushImage(x, row, w, 1, scanline);
    }
}
void DisplayRenderer::drawBackground(Page page) {
    drawBackgroundRect(page, 0, 0, 320, 240);
}

void DisplayRenderer::drawText(const char* text, int x, int y, const BitmapFont& font,
                               uint16_t value) {
    value = color(value);
    while (*text) {
        uint8_t c = static_cast<uint8_t>(*text++);
        if (c < 32 || c > 126)
            continue;
        const Glyph& glyph = font.glyphs[c - 32];
        for (int row = 0; row < font.height; ++row) {
            int runStart = -1;
            for (int col = 0; col <= glyph.width; ++col) {
                bool filled = false;
                if (col < glyph.width) {
                    const uint32_t bit = row * glyph.width + col;
                    filled =
                        pgm_read_byte(font.bits + glyph.offset + bit / 8) & (0x80 >> (bit % 8));
                }
                if (filled && runStart < 0)
                    runStart = col;
                if (!filled && runStart >= 0) {
                    tft.drawFastHLine(x + runStart, y + row, col - runStart, value);
                    runStart = -1;
                }
            }
        }
        x += glyph.advance;
    }
}

void DisplayRenderer::drawField(Page page, const ScreenField& field, bool settingsPanel) {

    if (settingsPanel)
        tft.fillRect(field.x, field.y, field.w, field.h, color(0x0028));
    else
        drawBackgroundRect(page, field.x, field.y, field.w, field.h);
    if (field.button) {
        tft.fillRect(field.x, field.y, field.w, field.h, color(0x01CF));
        tft.drawRect(field.x, field.y, field.w, field.h, color(field.color));
    }
    const char* text = field.text;
    const BitmapFont* font = fittedFont(text, field.w, field.h, bitmapFontForSize(field.fontSize));
    if (!font) {
        text = "--";
        font = &kFont10;
    }
    const int x = field.x + (field.centered ? (field.w - bitmapTextWidth(text, *font)) / 2 : 0);
    drawText(text, x, field.y + (field.button ? (field.h - font->height) / 2 : 0), *font,
             field.color);
}
void DisplayRenderer::drawRabbit(Page page, const RabbitSprite& sprite, const RabbitPose& pose) {
    const auto region = rabbitRegion(static_cast<uint8_t>(page));
    tft.setSwapBytes(true);
    for (int row = 0; row < region.h; ++row) {

        rabbitCompositeRow(sprite, region, pose.lift, row,
                           kPageBackgrounds[static_cast<uint8_t>(page)], scanline);
        for (int x = 0; x < region.w; ++x)
            scanline[x] = color(scanline[x]);
        tft.pushImage(region.x, region.y + row, region.w, 1, scanline);
    }
}
void DisplayRenderer::drawBtcChart(const BtcChart& chart) {
    tft.setSwapBytes(true);
    for (int row = 0; row < kChartH; ++row) {
        for (int x = 0; x < kChartW; ++x) {
            uint16_t pixel =
                pgm_read_word(kNetworkBackground + (kChartY + row) * 320 + kChartX + x);
            btcChartPixel(chart, x, row, pixel);
            scanline[x] = color(pixel);
        }
        tft.pushImage(kChartX, kChartY + row, kChartW, 1, scanline);
    }
}
