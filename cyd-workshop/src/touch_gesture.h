#pragma once
#include "swipe.h"

struct TouchEvent {
    Swipe swipe = Swipe::None;
    bool tap = false;
    int x = 0, y = 0;
};

// A tap fires on filtered release, never on a swipe's initial contact.
class TouchGesture {
public:
    void reset() { swipe_.reset(); active_ = false; }
    TouchEvent update(bool down, int x, int y, uint32_t now) {
        TouchEvent event;
        event.swipe = swipe_.update(down,x,y,now);
        if (down) {
            if (!active_) {
                active_ = true; startAt_ = now; samples_ = 0;
                minX_ = maxX_ = x; minY_ = maxY_ = y; sumX_ = sumY_ = 0;
            }
            lastAt_ = now;
            if (x < minX_) minX_ = x;
            if (x > maxX_) maxX_ = x;
            if (y < minY_) minY_ = y;
            if (y > maxY_) maxY_ = y;
            if (samples_ < 1000) { ++samples_; sumX_ += x; sumY_ += y; }
        } else if (active_ && uint32_t(now-lastAt_) >= SwipeRecognizer::kReleaseMs) {
            active_ = false;
            const uint32_t duration = lastAt_-startAt_;
            event.tap = event.swipe == Swipe::None && samples_ >= 2 &&
                        duration >= 30 && duration <= 1000 &&
                        maxX_-minX_ <= 22 && maxY_-minY_ <= 22;
            if (event.tap) { event.x = sumX_/samples_; event.y = sumY_/samples_; }
        }
        return event;
    }
private:
    SwipeRecognizer swipe_;
    bool active_ = false;
    uint32_t startAt_ = 0, lastAt_ = 0;
    uint16_t samples_ = 0;
    int minX_ = 0, maxX_ = 0, minY_ = 0, maxY_ = 0, sumX_ = 0, sumY_ = 0;
};
