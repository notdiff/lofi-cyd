#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "pages_assets.h"
#include "network.h"
#include "screens.h"
#include "display_text.h"
#include "touch.h"
#include "rabbit_scene.h"

TFT_eSPI tft;
Preferences preferences;
enum class ViewMode { Live, Background, Demo };
ViewMode viewMode = ViewMode::Live;
bool inverted = DEFAULT_INVERT;
uint8_t rotation = 1;
bool swapRedBlue = false;
constexpr uint8_t kButton = 0;
constexpr uint32_t kLongPressMs = 1500;
constexpr uint8_t kColorConfigVersion = 1;
constexpr uint16_t kNavy = 0x0044;
constexpr uint16_t kWhite = 0xEFFF;
constexpr uint16_t kCyan = 0x07FD;
uint16_t scanline[320];
Page activePage = Page::Overview;
ScreenFrame lastFrame;
Page lastDisplayPage = Page::Overview;
bool lastSetup = false;
bool liveNeedsRedraw = true;
RabbitAnimator rabbit;
RabbitPose lastRabbitPose = {255,0,RabbitAction::Idle};
uint8_t lastRabbitPage = 255;
uint32_t rabbitDrawAt = 0, rabbitFrames = 0, rabbitWorstDrawUs = 0;

uint16_t displayColor(uint16_t color) {
    if (!swapRedBlue) return color;
    return (color & 0x07E0) | ((color & 0x001F) << 11) | ((color & 0xF800) >> 11);
}

void drawBackgroundRect(Page page, int x, int y, int w, int h) {
    const uint16_t *background = kPageBackgrounds[static_cast<uint8_t>(page)];
    tft.setSwapBytes(true);
    for (int row = y; row < y + h; ++row) {
        for (int col = 0; col < w; ++col)
            scanline[col] = displayColor(pgm_read_word(background + row * 320 + x + col));
        tft.pushImage(x, row, w, 1, scanline);
    }
}
void drawBackground(Page page) { drawBackgroundRect(page, 0, 0, 320, 240); }

int textWidth(const char *text, const BitmapFont &font) {
    return bitmapTextWidth(text, font);
}

void drawText(const char *text, int x, int y, const BitmapFont &font, uint16_t color) {
    color = displayColor(color);
    while (*text) {
        uint8_t c = static_cast<uint8_t>(*text++);
        if (c < 32 || c > 126) continue;
        const Glyph &glyph = font.glyphs[c - 32];
        for (int row = 0; row < font.height; ++row) {
            int runStart = -1;
            for (int col = 0; col <= glyph.width; ++col) {
                bool filled = false;
                if (col < glyph.width) {
                    const uint32_t bit = row * glyph.width + col;
                    filled = pgm_read_byte(font.bits + glyph.offset + bit / 8) & (0x80 >> (bit % 8));
                }
                if (filled && runStart < 0) runStart = col;
                if (!filled && runStart >= 0) {
                    tft.drawFastHLine(x + runStart, y + row, col - runStart, color);
                    runStart = -1;
                }
            }
        }
        x += glyph.advance;
    }
}

const BitmapFont &preferredFont(uint8_t size) {
    return bitmapFontForSize(size);
}
void drawField(Page page, const ScreenField &field) {
    // Restore the actual scene pixels under the old glyphs, preserving gradients.
    drawBackgroundRect(page, field.x, field.y, field.w, field.h);
    const char *text = field.text;
    const BitmapFont *font = fittedFont(text,field.w,field.h,preferredFont(field.fontSize));
    if (!font) { text = "--"; font = &kFont10; }
    const int x = field.x + (field.centered ? (field.w - textWidth(text,*font)) / 2 : 0);
    drawText(text,x,field.y,*font,field.color);
}
bool fieldChanged(const ScreenField &a, const ScreenField &b) {
    return a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h ||
           a.fontSize != b.fontSize || a.color != b.color || a.centered != b.centered || strcmp(a.text,b.text);
}
void drawRabbit(Page page, uint32_t now, bool force = false) {
    if (!force && uint32_t(now-rabbitDrawAt) < 25) return;
    rabbitDrawAt = now;
    const auto number = static_cast<uint8_t>(page);
    if (!rabbitEnabled(number)) {
        lastRabbitPage = number;
        lastRabbitPose = {255,0,RabbitAction::Idle};
        return;
    }
    const auto pose = rabbit.update(number,now);
    if (pose.action != lastRabbitPose.action)
        Serial.printf("RABBIT action=%s page=%u\n",rabbitActionName(pose.action),unsigned(number+1));
    if (!force && number == lastRabbitPage && pose.frame == lastRabbitPose.frame && pose.lift == lastRabbitPose.lift && pose.action == lastRabbitPose.action) {
        lastRabbitPose = pose;
        return;
    }
    const auto region = rabbitRegion(number);
    const auto &sprite = rabbitSpriteForPose(number,pose);
    const uint32_t startedAt = micros();
    tft.setSwapBytes(true);
    for (int row = 0; row < region.h; ++row) {
        // Compose alpha + scene in one line before writing: no blank erase phase.
        rabbitCompositeRow(sprite,region,pose.lift,row,kPageBackgrounds[number],scanline);
        for (int x = 0; x < region.w; ++x) scanline[x] = displayColor(scanline[x]);
        tft.pushImage(region.x,region.y+row,region.w,1,scanline);
    }
    rabbitWorstDrawUs = max(rabbitWorstDrawUs,uint32_t(micros()-startedAt));
    ++rabbitFrames;
    lastRabbitPose = pose; lastRabbitPage = number;
}
void printRabbitStatus() {
    Serial.printf("RABBIT: page=%u action=%s frame=%u draws=%lu blink=%lu wave=%lu helmet=%lu sleepActions=%lu falls=%lu maxDraw=%lu us\n",
                  unsigned(lastRabbitPage+1),rabbitEnabled(lastRabbitPage) ? rabbitActionName(lastRabbitPose.action) : "hidden",lastRabbitPose.frame,
                  (unsigned long)rabbitFrames,(unsigned long)rabbit.blinks,(unsigned long)rabbit.waves,
                  (unsigned long)rabbit.helmets,(unsigned long)rabbit.sleepActions,
                  (unsigned long)rabbit.falls,(unsigned long)rabbitWorstDrawUs);
}
void reactRabbit() {
    if (viewMode == ViewMode::Background || networkSetupActive()) return;
    const uint32_t now = millis();
    if (rabbit.react(static_cast<uint8_t>(lastDisplayPage),now)) {
        Serial.printf("RABBIT reaction accepted page=%u\n",unsigned(static_cast<uint8_t>(lastDisplayPage)+1));
        drawRabbit(lastDisplayPage,now,true);
    }
}
void drawLive(bool force = false) {
    const bool demo = viewMode == ViewMode::Demo;
    const bool setup = !demo && networkSetupActive();
    const Page displayPage = setup ? Page::Overview : activePage;
    const uint32_t now = millis();
    const bool connected = demo || networkConnected();
    const LiveSnapshot data = demo ? demoSnapshot(now) : networkSnapshot();
    const int64_t epoch = demo ? 1791244800LL + 43200 : time(nullptr);
    ScreenFrame frame = setup ? makeSetupFrame() : makeScreenFrame(displayPage,data,now,epoch,connected,demo);
    if (!demo && !data.miner.valid && (setup || displayPage == Page::Overview)) {
        if (setup) {
            char status[32]; networkStatusText(status,sizeof(status));
            screenField(frame,26,151,137,17,10,kScreenMuted,status);
        } else networkStatusText(frame.fields[3].text,sizeof(frame.fields[3].text));
    }
    force = force || displayPage != lastDisplayPage || setup != lastSetup || frame.count != lastFrame.count;
    if (force) drawBackground(displayPage);
    for (uint8_t i = 0; i < frame.count; ++i)
        if (force || fieldChanged(frame.fields[i],lastFrame.fields[i])) drawField(displayPage,frame.fields[i]);
    if (displayPage == Page::Overview && !setup) {
        const uint16_t indicator = demo ? 0xFD20 : (minerFresh(data.miner,now,connected) ? kScreenCyan : (data.miner.valid ? 0xFD20 : kScreenMuted));
        static uint16_t lastIndicator = 0;
        if (force || indicator != lastIndicator) tft.fillCircle(32,211,2,displayColor(indicator));
        lastIndicator = indicator;
    }
    lastFrame = frame;
    lastDisplayPage = displayPage;
    lastSetup = setup;
    drawRabbit(displayPage,now,force);
}
void render() {
    tft.setRotation(rotation);
    touchSetRotation(rotation);
    tft.invertDisplay(inverted);
    if (viewMode == ViewMode::Background) drawBackground(activePage);
    else drawLive(true);
}
void changePage(Swipe direction) {
    activePage = static_cast<Page>(adjacentPage(static_cast<uint8_t>(activePage),direction));
    preferences.putUChar("page",static_cast<uint8_t>(activePage));
    viewMode = ViewMode::Live;
    render();
}
void nextPage() { changePage(Swipe::Next); }

void saveDisplaySettings() {
    preferences.putUChar("rotation", rotation);
    preferences.putBool("inverted", inverted);
    preferences.putBool("swapRB", swapRedBlue);
}

void setup() {
    Serial.begin(115200);
    rabbit = RabbitAnimator(esp_random());
    pinMode(kButton, INPUT_PULLUP);
    // Deselect the unused SD card before assigning VSPI to touch.
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    preferences.begin("workshop", false);
    const uint8_t savedPage = preferences.getUChar("page",1);
    activePage = static_cast<Page>(savedPage < 4 ? savedPage : 1);
    rotation = preferences.getUChar("rotation", 1);
    if (rotation != 1 && rotation != 3) rotation = 1;
    inverted = preferences.getBool("inverted", DEFAULT_INVERT);
    swapRedBlue = preferences.getBool("swapRB", false);
    // Apply the corrected inversion once, including when v0.1 saved the old mode.
    // Subsequent boots retain the user's chosen mode.
    if (preferences.getUChar("colorcfg", 0) < kColorConfigVersion) {
        inverted = DEFAULT_INVERT;
        preferences.putBool("inverted", inverted);
        preferences.putUChar("colorcfg", kColorConfigVersion);
    }
    pinMode(21, OUTPUT);
    digitalWrite(21, LOW);
    tft.init();
    touchBegin(rotation);
    render();
    digitalWrite(21, HIGH);
    networkBegin();
    liveNeedsRedraw = true;
    Serial.println("Workshop v0.6.1: clock stays asleep; 12-frame compact falls with one fixed scale on overview/BTC.");
    Serial.println("Serial keys: 1-4 = page, n = next, a = bunny reaction, q = diagnostics, l = LIVE, d = DEMO, b = background, w = Wi-Fi setup, r = rotate 180, i = invert, s = red/blue swap.");
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
}

void loop() {
    // Debounce BOOT without waiting for the button to be released.
    static bool lastReading = HIGH;
    static bool stableReading = HIGH;
    static uint32_t changedAt = 0;
    static uint32_t pressedAt = 0;
    static bool longPressHandled = false;
    const bool reading = digitalRead(kButton);
    if (reading != lastReading) { changedAt = millis(); lastReading = reading; }
    if (millis() - changedAt >= 40 && reading != stableReading) {
        stableReading = reading;
        if (stableReading == LOW) {
            pressedAt = millis();
            longPressHandled = false;
        } else if (!longPressHandled) {
            nextPage();
        }
    }
    if (stableReading == LOW && !longPressHandled && millis() - pressedAt >= kLongPressMs) {
        longPressHandled = true;
        inverted = !inverted;
        saveDisplaySettings();
        render();
    }
    if (Serial.available()) {
        const char c = Serial.read();
        bool redraw = true;
        bool changedSettings = false;
        switch (c) {
            case 'l': viewMode = ViewMode::Live; break;
            case 'd': viewMode = ViewMode::Demo; break;
            case 'b': viewMode = ViewMode::Background; break;
            case 'w': networkOpenSetup(); viewMode = ViewMode::Live; break;
            case 'q': networkPrintStatus(); touchPrintStatus(); printRabbitStatus(); Serial.printf("PAGE: %u\n",unsigned(static_cast<uint8_t>(activePage)+1)); redraw = false; break;
            case 'a': reactRabbit(); redraw = false; break;
            case 'n': nextPage(); redraw = false; break;
            case '1': case '2': case '3': case '4':
                activePage = static_cast<Page>(c - '1');
                preferences.putUChar("page",static_cast<uint8_t>(activePage));
                viewMode = ViewMode::Live;
                break;
            case 'r': rotation = rotation == 1 ? 3 : 1; changedSettings = true; break;
            case 'i': inverted = !inverted; changedSettings = true; break;
            case 's': swapRedBlue = !swapRedBlue; changedSettings = true; break;
            default: redraw = false;
        }
        if (changedSettings) saveDisplaySettings();
        if (redraw) render();
    }
    networkLoop();
    const auto event = touchPoll(millis(),stableReading == HIGH && !networkSetupActive());
    const auto swipe = event.swipe;
    if (swipe != Swipe::None) {
        changePage(swipe);
        Serial.printf("SWIPE %s -> PAGE: %u\n",swipe == Swipe::Next ? "LEFT" : "RIGHT",
                      unsigned(static_cast<uint8_t>(activePage)+1));
    } else if (event.tap && viewMode != ViewMode::Background &&
               rabbitHit(static_cast<uint8_t>(lastDisplayPage),lastRabbitPose,event.x,event.y)) {
        reactRabbit();
    }
    static uint32_t liveDrawAt = 0;
    if (viewMode != ViewMode::Background && (liveNeedsRedraw || uint32_t(millis() - liveDrawAt) >= 250)) {
        drawLive(liveNeedsRedraw);
        liveNeedsRedraw = false;
        liveDrawAt = millis();
    }
    if (viewMode != ViewMode::Background) drawRabbit(lastDisplayPage,millis());
    delay(5);
}
