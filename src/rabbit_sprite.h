#pragma once
#include "rabbit_animation.h"
#include <Arduino.h>

struct RabbitSprite {
    const uint16_t* palette;
    const uint8_t* opacity;
    const uint16_t* rows;
    const uint8_t* data;
    uint8_t width, height;
};

inline void rabbitRow(const RabbitSprite& sprite, int row, uint8_t* colors) {
    uint32_t at = pgm_read_word(sprite.rows + row);
    const uint32_t end = pgm_read_word(sprite.rows + row + 1);
    int x = 0;
    while (at < end && x < sprite.width) {
        const uint8_t token = pgm_read_byte(sprite.data + at++);
        if (!token) {
            const uint8_t count = pgm_read_byte(sprite.data + at++);
            const uint8_t color = pgm_read_byte(sprite.data + at++);
            for (int i = 0; i < count && x < sprite.width; ++i)
                colors[x++] = color;
            continue;
        }
        const int count = token & 127;
        for (int i = 0; i < count && x < sprite.width; ++i, ++x) {
            if (token & 128)
                colors[x] = 0;
            else {
                colors[x] = pgm_read_byte(sprite.data + at++);
            }
        }
    }
    while (x < sprite.width)
        colors[x++] = 0;
}
inline uint16_t rabbitBlend(uint16_t foreground, uint16_t background, uint8_t alpha) {
    if (!alpha)
        return background;
    if (alpha >= 15)
        return foreground;
    const uint32_t inv = 15 - alpha;
    const uint32_t r =
        (((foreground >> 11) & 31) * alpha + ((background >> 11) & 31) * inv + 7) / 15;
    const uint32_t g = (((foreground >> 5) & 63) * alpha + ((background >> 5) & 63) * inv + 7) / 15;
    const uint32_t b = ((foreground & 31) * alpha + (background & 31) * inv + 7) / 15;
    return uint16_t((r << 11) | (g << 5) | b);
}

inline void rabbitCompositeRow(const RabbitSprite& sprite, const RabbitRegion& region, uint8_t page,
                               const RabbitPose& pose, int row, const uint16_t* background,
                               uint16_t* output) {
    const int width = rabbitWidth(page), height = rabbitHeight(page);
    const int top = rabbitTop(page) - pose.lift, left = rabbitLeft(page) + pose.shift;
    const bool visible = row >= top && row < top + height;
    uint8_t indexes[144];
    if (visible)
        rabbitRow(sprite, (row - top) * sprite.height / height, indexes);
    for (int x = 0; x < region.w; ++x) {
        uint16_t color = pgm_read_word(background + (region.y + row) * 320 + region.x + x);
        if (visible && x >= left && x < left + width) {
            const uint8_t index = indexes[(x - left) * sprite.width / width];
            color = rabbitBlend(pgm_read_word(sprite.palette + index), color,
                                pgm_read_byte(sprite.opacity + index));
        }
        output[x] = color;
    }
}

inline void rabbitEffectRow(const RabbitRegion& region, const RabbitPose& pose, int row,
                            uint16_t* output) {
    auto dot = [&](int x, int y, uint16_t color) {
        if (row == y && x >= 0 && x < region.w)
            output[x] = color;
    };
    if (pose.action == RabbitAction::Coin) {
        const int cycle = pose.phase % 64;
        const int cy = 18 + (cycle < 32 ? 32 - cycle : cycle - 32);
        const int cx = 25;
        const int dy = row - cy;
        if (dy >= -9 && dy <= 9) {
            for (int dx = -9; dx <= 9; ++dx) {
                const int radius = dx * dx + dy * dy;
                if (radius <= 81)
                    dot(cx + dx, row, radius > 55 ? 0xC440 : 0xFEA0);
            }
            const uint8_t glyph[] = {14, 9, 9, 14, 9, 9, 14};
            if (dy >= -3 && dy <= 3)
                for (int x = 0; x < 4; ++x)
                    if (glyph[dy + 3] & (8 >> x))
                        dot(cx - 2 + x, row, 0xFFFF);
            if (dy == -5 || dy == -4 || dy == 4 || dy == 5) {
                dot(cx - 1, row, 0xFFFF);
                dot(cx + 1, row, 0xFFFF);
            }
        }
    }
    if (pose.action == RabbitAction::Coin || pose.action == RabbitAction::Dance) {
        for (int i = 0; i < 4; ++i) {
            const int x = 16 + (i * 37) % (region.w - 24);
            const int y = 5 + (i * 29 + pose.phase * 2) % 70;
            const int size = 1 + (pose.phase / 4 + i) % 3;
            for (int d = -size; d <= size; ++d) {
                dot(x + d, y, 0xFFE0);
                dot(x, y + d, 0xFFE0);
            }
        }
    } else if (pose.action == RabbitAction::Dream) {
        for (int i = 0; i < 3; ++i) {
            const int t = (pose.phase + i * 27) % 100;
            const int cx = 137 + t / 5, cy = 50 - t / 3, r = 2 + i;
            if (row < cy - r || row > cy + r)
                continue;
            for (int dx = -r; dx <= r; ++dx) {
                const int d = dx * dx + (row - cy) * (row - cy);
                if (d <= r * r && d >= (r - 1) * (r - 1))
                    dot(cx + dx, row, 0xAFFD);
            }
        }
    }
}
