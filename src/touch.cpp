#include "touch.h"
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

namespace {

constexpr uint8_t kClock = 25, kMiso = 39, kMosi = 32, kCs = 33, kIrq = 36;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen panel(kCs, kIrq);
TouchGesture gesture;
bool ready = false, pressed = false;
uint8_t currentRotation = 1;
uint32_t polledAt = 0, points = 0, nextSwipes = 0, previousSwipes = 0;
uint32_t taps = 0;
int16_t lastX = 0, lastY = 0, lastZ = 0;

int screenCoordinate(int raw, int extent) {
    const int pixel = (raw - 200) * (extent - 1) / 3700;
    return constrain(pixel, 0, extent - 1);
}
}

void touchBegin(uint8_t rotation) {
    touchSpi.begin(kClock, kMiso, kMosi, kCs);
    ready = panel.begin(touchSpi);
    touchSetRotation(rotation);
    Serial.printf("TOUCH init: XPT2046 VSPI CLK=%u MISO=%u MOSI=%u CS=%u IRQ=%u ready=%d\n", kClock,
                  kMiso, kMosi, kCs, kIrq, int(ready));
}
void touchSetRotation(uint8_t rotation) {
    currentRotation = rotation;
    panel.setRotation(rotation);
    gesture.reset();
}
TouchEvent touchPoll(uint32_t now, bool enabled) {
    if (!ready || uint32_t(now - polledAt) < 12)
        return {};
    polledAt = now;
    const auto point = panel.getPoint();

    const bool down = point.z >= 300 && point.z < 8190 && point.x > 50 && point.x < 4045 &&
                      point.y > 50 && point.y < 4045;
    if (down) {
        lastX = point.x;
        lastY = point.y;
        lastZ = point.z;
        ++points;
    }
    if (down != pressed) {
        pressed = down;
        if (down)
            Serial.printf("TOUCH down: x=%d y=%d pressure=%d\n", point.x, point.y, point.z);
    }
    if (!enabled) {
        gesture.reset();
        return {};
    }
    const auto event =
        gesture.update(down, screenCoordinate(point.x, 320), screenCoordinate(point.y, 240), now);
    if (event.swipe == Swipe::Next)
        ++nextSwipes;
    else if (event.swipe == Swipe::Previous)
        ++previousSwipes;
    if (event.tap) {
        ++taps;
        Serial.printf("TOUCH tap: x=%d y=%d\n", event.x, event.y);
    }
    return event;
}
void touchPrintStatus() {
    Serial.printf("TOUCH: ready=%d IRQ=%d down=%d points=%lu left=%lu right=%lu taps=%lu "
                  "rotation=%u raw=%d,%d pressure=%d\n",
                  int(ready), digitalRead(kIrq), int(pressed), (unsigned long)points,
                  (unsigned long)nextSwipes, (unsigned long)previousSwipes, (unsigned long)taps,
                  currentRotation, lastX, lastY, lastZ);
}
