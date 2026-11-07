#pragma once
#include <cstdint>
#include <cstdlib>

enum class Swipe : int8_t { None = 0, Next = 1, Previous = -1 };

// Coordinates are approximate screen pixels. Relative motion needs no target
// calibration; release filtering tolerates a resistive panel's brief dropouts.
class SwipeRecognizer {
public:
    static constexpr uint32_t kReleaseMs = 80, kMinMs = 50, kMaxMs = 1500;
    static constexpr int kMinDistance = 60, kMaxVertical = 55;

    void reset() { active_ = false; }
    Swipe update(bool down, int x, int y, uint32_t now) {
        if (down) {
            if (!active_) {
                active_ = true; eligible_ = true; samples_ = 0;
                startAt_ = now; startX_ = x; minY_ = maxY_ = y;
            }
            lastAt_ = now; lastX_ = x;
            if (y < minY_) minY_ = y;
            if (y > maxY_) maxY_ = y;
            if (samples_ < 3) ++samples_;
            if (uint32_t(now-startAt_) > kMaxMs) eligible_ = false;
            return Swipe::None;
        }
        if (!active_ || uint32_t(now-lastAt_) < kReleaseMs) return Swipe::None;
        active_ = false;
        const uint32_t duration = lastAt_ - startAt_;
        const int dx = lastX_ - startX_, vertical = maxY_ - minY_;
        if (!eligible_ || samples_ < 3 || duration < kMinMs || duration > kMaxMs ||
            std::abs(dx) < kMinDistance || vertical > kMaxVertical ||
            std::abs(dx)*2 < vertical*3) return Swipe::None;
        return dx < 0 ? Swipe::Next : Swipe::Previous;
    }
private:
    bool active_ = false, eligible_ = true;
    uint8_t samples_ = 0;
    int startX_ = 0, lastX_ = 0, minY_ = 0, maxY_ = 0;
    uint32_t startAt_ = 0, lastAt_ = 0;
};

inline uint8_t adjacentPage(uint8_t page, Swipe swipe) {
    if (swipe == Swipe::None) return page;
    return uint8_t((page + (swipe == Swipe::Previous ? 3 : 1)) % 4);
}
