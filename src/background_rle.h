#pragma once
#include "generated/pages_assets.h"

inline void detailsBackgroundRow(int row, uint16_t* out) {
    uint32_t at = pgm_read_dword(kDetailsRows + row);
    int x = 0;
    while (x < 320) {
        const uint8_t token = pgm_read_byte(kDetailsData + at++);
        const int count = (token & 127) + 1;
        uint16_t value = 0;
        for (int i = 0; i < count; ++i) {
            if (!(token & 128) || i == 0) {
                value = pgm_read_byte(kDetailsData + at++);
                value |= uint16_t(pgm_read_byte(kDetailsData + at++)) << 8;
            }
            out[x++] = value;
        }
    }
}
