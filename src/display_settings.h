#pragma once
#include <cstdint>
#include <cstdio>

struct DisplaySettings {
    uint8_t brightness = 100;
    uint8_t pageMinutes = 5;
    bool night = true;
    uint16_t offMinute = 23 * 60;
    uint16_t onMinute = 8 * 60;
    int16_t utcMinutes = 3 * 60;
};

inline DisplaySettings validatedDisplaySettings(DisplaySettings settings) {
    const DisplaySettings defaults;
    if (settings.brightness < 5 || settings.brightness > 100)
        settings.brightness = defaults.brightness;
    if (settings.pageMinutes > 60)
        settings.pageMinutes = defaults.pageMinutes;
    if (settings.offMinute >= 1440)
        settings.offMinute = defaults.offMinute;
    if (settings.onMinute >= 1440)
        settings.onMinute = defaults.onMinute;
    if (settings.utcMinutes < -720 || settings.utcMinutes > 840 || settings.utcMinutes % 15)
        settings.utcMinutes = defaults.utcMinutes;
    return settings;
}

inline bool displayNightTime(const DisplaySettings& settings, int64_t epoch) {
    if (!settings.night || epoch < 1700000000 || settings.offMinute == settings.onMinute)
        return false;
    const int minute = int(((epoch / 60 + settings.utcMinutes) % 1440 + 1440) % 1440);
    return settings.offMinute < settings.onMinute
               ? minute >= settings.offMinute && minute < settings.onMinute
               : minute >= settings.offMinute || minute < settings.onMinute;
}

inline void formatUtcOffset(char* text, size_t size, int minutes) {
    const int magnitude = minutes < 0 ? -minutes : minutes;
    snprintf(text, size, "%c%02d:%02d", minutes < 0 ? '-' : '+', magnitude / 60, magnitude % 60);
}

inline uint8_t brightnessDuty(uint8_t percent) { return (uint16_t(percent) * 255 + 50) / 100; }

inline void stepDisplaySetting(DisplaySettings& settings, int row, int direction) {
    if (direction != -1 && direction != 1)
        return;
    if (row < 2 && row >= 0 && settings.night) {
        auto& minute = row == 0 ? settings.offMinute : settings.onMinute;
        minute = (minute + direction * 15 + 1440) % 1440;
    } else if (row == 2) {
        const int offset = settings.utcMinutes + direction * 15;
        if (offset >= -720 && offset <= 840)
            settings.utcMinutes = offset;
    } else if (row == 3) {
        const int brightness = settings.brightness + direction * 5;
        settings.brightness = brightness < 5 ? 5 : (brightness > 100 ? 100 : brightness);
    } else if (row == 4) {
        const int minutes = settings.pageMinutes + direction;
        settings.pageMinutes = minutes < 0 ? 0 : (minutes > 60 ? 60 : minutes);
    }
}

class PageSchedule {
  public:
    void reset(uint32_t now) { since_ = now; }
    bool due(uint8_t minutes, uint32_t now, bool allowed) {
        if (!allowed || !minutes) {
            reset(now);
            return false;
        }
        if (uint32_t(now - since_) < uint32_t(minutes) * 60000u)
            return false;
        reset(now);
        return true;
    }

  private:
    uint32_t since_ = 0;
};

class BacklightSchedule {
  public:
    static constexpr uint32_t kWakeMs = 60000;
    void wake(uint32_t now) {
        waking_ = true;
        wakeAt_ = now;
    }
    bool shouldLight(const DisplaySettings& settings, int64_t epoch, uint32_t now) {
        if (waking_ && uint32_t(now - wakeAt_) >= kWakeMs)
            waking_ = false;
        return !displayNightTime(settings, epoch) || waking_;
    }

  private:
    bool waking_ = false;
    uint32_t wakeAt_ = 0;
};
