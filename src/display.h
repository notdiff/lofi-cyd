#pragma once
#include "rabbit_animation.h"
#include "screens.h"
#include <TFT_eSPI.h>

struct BitmapFont;
struct RabbitSprite;

class DisplayRenderer {
  public:
    void begin();
    void configure(uint8_t rotation, bool inverted, bool swapRedBlue);
    void drawBackground(Page page);
    void drawField(Page page, const ScreenField& field, bool settingsPanel);
    void drawSettingsPanel();
    void drawBtcChart(const BtcChart& chart);
    void drawRabbit(Page page, const RabbitSprite& sprite, const RabbitPose& pose);
    void drawIndicator(uint16_t value);

  private:
    TFT_eSPI tft;
    uint16_t scanline[320];
    bool swapRedBlue = false;
    uint16_t color(uint16_t value) const;
    void drawBackgroundRect(Page page, int x, int y, int w, int h);
    void drawText(const char* text, int x, int y, const BitmapFont& font, uint16_t value);
};
