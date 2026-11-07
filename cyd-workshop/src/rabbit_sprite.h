#pragma once
#include <Arduino.h>
#include "rabbit_animation.h"

struct RabbitSprite {
    const uint16_t *palette;
    const uint8_t *opacity;
    const uint16_t *rows;
    const uint8_t *data;
    uint8_t width, height;
};
// High bit = transparent skip, 1..127 = literals, 0/count/index = repeated color.
// Decode only one scanline, keeping both the sprites and row indexes in flash.
inline void rabbitRow(const RabbitSprite &sprite, int row, uint8_t *colors) {
    uint32_t at = pgm_read_word(sprite.rows+row);
    const uint32_t end = pgm_read_word(sprite.rows+row+1);
    int x = 0;
    while (at < end && x < sprite.width) {
        const uint8_t token = pgm_read_byte(sprite.data+at++);
        if (!token) {
            const uint8_t count = pgm_read_byte(sprite.data+at++);
            const uint8_t color = pgm_read_byte(sprite.data+at++);
            for (int i = 0; i < count && x < sprite.width; ++i) colors[x++] = color;
            continue;
        }
        const int count = token & 127;
        for (int i = 0; i < count && x < sprite.width; ++i,++x) {
            if (token & 128) colors[x] = 0;
            else {
                colors[x] = pgm_read_byte(sprite.data+at++);
            }
        }
    }
    while (x < sprite.width) colors[x++] = 0;
}
inline uint16_t rabbitBlend(uint16_t foreground, uint16_t background, uint8_t alpha) {
    if (!alpha) return background;
    if (alpha >= 15) return foreground;
    const uint32_t inv = 15-alpha;
    const uint32_t r = (((foreground>>11)&31)*alpha+((background>>11)&31)*inv+7)/15;
    const uint32_t g = (((foreground>>5)&63)*alpha+((background>>5)&63)*inv+7)/15;
    const uint32_t b = ((foreground&31)*alpha+(background&31)*inv+7)/15;
    return uint16_t((r<<11)|(g<<5)|b);
}

inline void rabbitCompositeRow(const RabbitSprite &sprite, const RabbitRegion &region,
                               int lift, int row, const uint16_t *background, uint16_t *output) {
    const int width = region.w-2, height = region.h-3, top = 2-lift;
    const bool visible = row >= top && row < top+height;
    uint8_t indexes[144];
    if (visible) rabbitRow(sprite,(row-top)*sprite.height/height,indexes);
    for (int x = 0; x < region.w; ++x) {
        uint16_t color = pgm_read_word(background+(region.y+row)*320+region.x+x);
        if (visible && x >= 1 && x < 1+width) {
            const uint8_t index = indexes[(x-1)*sprite.width/width];
            color = rabbitBlend(pgm_read_word(sprite.palette+index),color,pgm_read_byte(sprite.opacity+index));
        }
        output[x] = color;
    }
}
