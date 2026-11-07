#pragma once
#include <cstdint>

enum class RabbitAction : uint8_t {
    Idle,
    Blink,
    Wave,
    Helmet,
    Sleep,
    Ear,
    Nuzzle,
    Fall,
    Hop,
    Dance,
    Coin,
    Dream
};
struct RabbitPose {
    uint8_t frame;
    int8_t lift;
    RabbitAction action;
    int8_t shift;
    uint8_t phase;
    constexpr RabbitPose(uint8_t frame, int8_t lift, RabbitAction action, int8_t shift = 0,
                         uint8_t phase = 0)
        : frame(frame), lift(lift), action(action), shift(shift), phase(phase) {}
};
struct RabbitRegion {
    int x, y, w, h;
};

inline bool rabbitEnabled(uint8_t page) { return page != 2; }
inline bool rabbitReactive(uint8_t page) { return page == 1 || page == 3; }

inline RabbitRegion rabbitRegion(uint8_t page) {
    if (page == 0)
        return {140, 132, 180, 107};
    if (!rabbitEnabled(page))
        return {0, 0, 0, 0};
    return {170, 23, 150, 212};
}
inline int rabbitWidth(uint8_t page) { return page == 0 ? 178 : 140; }
inline int rabbitHeight(uint8_t page) { return page == 0 ? 104 : 187; }
inline int rabbitLeft(uint8_t page) { return page == 0 ? 1 : 9; }
inline int rabbitTop(uint8_t page) { return page == 0 ? 2 : 14; }
inline const char* rabbitActionName(RabbitAction action) {
    switch (action) {
    case RabbitAction::Blink:
        return "blink";
    case RabbitAction::Wave:
        return "wave";
    case RabbitAction::Helmet:
        return "helmet";
    case RabbitAction::Sleep:
        return "sleep";
    case RabbitAction::Ear:
        return "ear";
    case RabbitAction::Nuzzle:
        return "nuzzle";
    case RabbitAction::Fall:
        return "fall";
    case RabbitAction::Hop:
        return "hop";
    case RabbitAction::Dance:
        return "dance";
    case RabbitAction::Coin:
        return "bitcoin";
    case RabbitAction::Dream:
        return "dream";
    default:
        return "idle";
    }
}

class RabbitAnimator {
  public:
    explicit RabbitAnimator(uint32_t seed = 1) : random_(seed ? seed : 1) {}
    bool react(uint8_t page, uint32_t now) {
        if (!rabbitReactive(page))
            return false;
        update(page, now);
        if (action_ == RabbitAction::Fall)
            return false;
        reactionPage_ = page;
        action_ = RabbitAction::Fall;
        actionAt_ = now;
        ++falls;
        return true;
    }
    RabbitPose update(uint8_t page, uint32_t now) {
        if (!rabbitEnabled(page)) {
            if (action_ == RabbitAction::Fall) {
                action_ = RabbitAction::Idle;
                started_ = false;
            }
            return {255, 0, RabbitAction::Idle};
        }
        const bool sleeping = page == 0;
        if (action_ == RabbitAction::Fall && page != reactionPage_)
            started_ = false;
        if (!started_ || sleeping != sleeping_) {
            started_ = true;
            sleeping_ = sleeping;
            action_ = sleeping ? RabbitAction::Sleep : RabbitAction::Idle;
            actionAt_ = phaseAt_ = blinkAt_ = activityAt_ = now;
            blinkWait_ = between(3000, 6500);
            activityWait_ = sleeping ? between(12000, 24000) : between(8000, 18000);
        }
        uint32_t elapsed = now - actionAt_;
        if (action_ == RabbitAction::Fall) {
            if (elapsed < 2000u) {
                static const uint16_t ends[] = {100, 220,  340,  460,  600,  800,
                                                960, 1140, 1360, 1600, 1800, 2000};
                uint8_t frame = 0;
                while (frame < 11 && elapsed >= ends[frame])
                    ++frame;
                return {frame, 0, action_};
            }
            action_ = sleeping ? RabbitAction::Sleep : RabbitAction::Idle;
            actionAt_ = phaseAt_ = activityAt_ = blinkAt_ = now;
            blinkWait_ = between(3000, 6500);
            activityWait_ = sleeping ? between(12000, 24000) : between(8000, 18000);
            elapsed = 0;
        }
        const uint32_t duration =
            action_ == RabbitAction::Hop     ? 1200
            : action_ == RabbitAction::Dance ? 2400
            : action_ == RabbitAction::Coin  ? 3200
            : action_ == RabbitAction::Dream ? 2800
            : action_ == RabbitAction::Blink
                ? 210
                : (action_ == RabbitAction::Wave
                       ? 1320
                       : (action_ == RabbitAction::Helmet
                              ? 680
                              : (action_ == RabbitAction::Ear || action_ == RabbitAction::Nuzzle
                                     ? 800
                                     : 0)));
        if (duration && elapsed >= duration) {
            if (action_ == RabbitAction::Blink) {
                blinkAt_ = now;
                blinkWait_ = between(3000, 6500);
            } else {
                activityAt_ = now;
                activityWait_ = sleeping ? between(12000, 24000) : between(8000, 18000);
            }
            action_ = sleeping ? RabbitAction::Sleep : RabbitAction::Idle;
        }
        if (action_ == RabbitAction::Idle && uint32_t(now - blinkAt_) >= blinkWait_) {
            action_ = RabbitAction::Blink;
            actionAt_ = now;
            ++blinks;
        } else if ((action_ == RabbitAction::Idle || action_ == RabbitAction::Sleep) &&
                   uint32_t(now - activityAt_) >= activityWait_) {
            if (sleeping) {
                const auto choice = next() % 3;
                action_ = choice == 0   ? RabbitAction::Ear
                          : choice == 1 ? RabbitAction::Nuzzle
                                        : RabbitAction::Dream;
                ++sleepActions;
            } else {

                const RabbitAction actions[] = {RabbitAction::Wave, RabbitAction::Helmet,
                                                RabbitAction::Hop, RabbitAction::Dance};
                ++activities_;
                action_ = activities_ % 7 == 0 ? RabbitAction::Coin : actions[next() % 4];
                if (action_ == RabbitAction::Wave)
                    ++waves;
                else if (action_ == RabbitAction::Helmet)
                    ++helmets;
            }
            actionAt_ = now;
        }
        elapsed = now - actionAt_;
        if (action_ == RabbitAction::Hop || action_ == RabbitAction::Dance ||
            action_ == RabbitAction::Coin || action_ == RabbitAction::Dream) {
            const uint8_t phase = uint8_t(elapsed / 25);
            if (action_ == RabbitAction::Dream)
                return {uint8_t((uint32_t(now - phaseAt_) / 1600) % 2), 0, action_, 0, phase};
            const int period = action_ == RabbitAction::Hop ? 600 : 800;
            const int t = elapsed % period;
            const int triangle = t < period / 2 ? t : period - t;
            const int lift = action_ == RabbitAction::Hop ? triangle * 12 / (period / 2)
                                                          : triangle * 3 / (period / 2);
            const int shift = action_ == RabbitAction::Dance ? -triangle * 8 / (period / 2) : 0;
            const uint8_t frame = action_ == RabbitAction::Coin ? (elapsed % 900 < 180 ? 2 : 6)
                                                                : (elapsed % 600 < 300 ? 4 : 5);
            return {frame, int8_t(lift), action_, int8_t(shift), phase};
        }
        uint8_t frame = 0;
        if (action_ == RabbitAction::Blink)
            frame = elapsed < 60 || elapsed >= 150 ? 1 : 2;
        else if (action_ == RabbitAction::Wave) {
            if (elapsed < 160 || elapsed >= 1160)
                frame = 3;
            else
                frame = 4 + uint8_t((elapsed - 160) / 125 % 2);
        } else if (action_ == RabbitAction::Helmet)
            frame = elapsed < 200 || elapsed >= 480 ? 6 : 7;
        else if (action_ == RabbitAction::Ear)
            frame = elapsed < 400 ? 2 : 0;
        else if (action_ == RabbitAction::Nuzzle)
            frame = elapsed < 600 ? 3 : 0;
        else if (sleeping)
            frame = uint8_t((uint32_t(now - phaseAt_) / 1600) % 2);
        const int8_t lift = sleeping ? 0 : (uint32_t(now - phaseAt_) % 3200 >= 1600 ? 1 : 0);
        return {frame, lift, action_};
    }
    uint32_t blinks = 0, waves = 0, helmets = 0, sleepActions = 0;
    uint32_t falls = 0;

  private:
    uint32_t next() {
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        return random_;
    }
    uint32_t between(uint32_t low, uint32_t high) { return low + next() % (high - low + 1); }
    uint32_t random_, actionAt_ = 0, phaseAt_ = 0, blinkAt_ = 0, activityAt_ = 0;
    uint32_t blinkWait_ = 0, activityWait_ = 0;
    uint32_t activities_ = 0;
    bool started_ = false, sleeping_ = false;
    uint8_t reactionPage_ = 255;
    RabbitAction action_ = RabbitAction::Idle;
};
