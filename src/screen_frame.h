#pragma once
#include "miner_control.h"
constexpr uint16_t kScreenWhite = 0xEFFF, kScreenCyan = 0x07FD, kScreenMuted = 0x8C71,
                   kScreenRed = 0xFB6D;
enum class Page : uint8_t { Clock, Overview, Details, Bitcoin };
struct ScreenField {
    int x, y, w, h;
    uint8_t fontSize;
    uint16_t color;
    bool centered;
    char text[32];
    bool button = false;
};
struct ScreenFrame {
    ScreenField fields[50] = {};
    uint8_t count = 0;
};
inline void screenField(ScreenFrame& frame, int x, int y, int w, int h, uint8_t font,
                        uint16_t color, const char* text, bool centered = false) {
    if (frame.count >= 50)
        return;
    auto& field = frame.fields[frame.count++];
    field.x = x;
    field.y = y;
    field.w = w;
    field.h = h;
    field.fontSize = font;
    field.color = color;
    field.centered = centered;
    snprintf(field.text, sizeof(field.text), "%s", text);
}
inline void screenButton(ScreenFrame& frame, const ControlRect& rect, const char* text,
                         bool enabled = true) {
    screenField(frame, rect.x, rect.y, rect.w, rect.h, 13, enabled ? kScreenCyan : kScreenMuted,
                text, true);
    frame.fields[frame.count - 1].button = true;
}
