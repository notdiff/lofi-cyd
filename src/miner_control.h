#pragma once
#include "config.h"
#include "metrics.h"

constexpr int kMinerFrequencies[] = {400, 425, 450, 475, 485, 500, 525, 550, 575};
constexpr int kMinerVoltages[] = {1100, 1150, 1200, 1250, 1300};
struct MinerSettings {
    int frequency = 0, voltage = 0, fan = 100;
    bool automatic = true;
};
enum ControlMask : uint8_t { ChangeFrequency = 1, ChangeVoltage = 2, ChangeFan = 4 };
struct MinerCommand {
    MinerSettings settings;
    uint8_t mask = 0;
    bool restart = false;
};
enum class ControlPhase : uint8_t { Idle, Pending, Saved, Restarted, Failed, Unknown };
struct ControlReport {
    ControlPhase phase = ControlPhase::Idle;
    int http = 0;
    uint32_t id = 0, at = 0;
};

struct ControlMailbox {
    ControlReport report;
    MinerCommand command;
    bool queued = false;
    uint32_t submittedAt = 0;
    bool submit(const MinerCommand& next, uint32_t now, uint32_t& id) {
        if (!valid(next) || report.phase == ControlPhase::Pending ||
            (command.restart && report.id && uint32_t(now - submittedAt) < 15000))
            return false;
        command = next;
        queued = true;
        submittedAt = now;
        report.phase = ControlPhase::Pending;
        report.http = 0;
        report.at = now;
        id = ++report.id;
        return true;
    }
    bool take(MinerCommand& out) {
        if (!queued)
            return false;
        out = command;
        queued = false;
        return true;
    }
    void finish(int code) {
        if (report.phase != ControlPhase::Pending || queued)
            return;
        report.http = code;
        report.phase = code >= 200 && code < 300
                           ? (command.restart ? ControlPhase::Restarted : ControlPhase::Saved)
                           : (code <= 0 ? ControlPhase::Unknown : ControlPhase::Failed);
    }

  private:
    static bool valid(const MinerCommand& next);
};
enum class ControlView : uint8_t { Closed, Miner, Restart, Display };
struct ControlEditor {
    ControlView view = ControlView::Closed;
    MinerSettings base, draft;
    bool ready = false;
    uint32_t requestId = 0;
};
template <size_t N> inline bool minerChoice(int value, const int (&choices)[N]) {
    for (int choice : choices)
        if (choice == value)
            return true;
    return false;
}
template <size_t N> inline int stepMinerChoice(int value, int direction, const int (&choices)[N]) {
    if (direction > 0) {
        for (int choice : choices)
            if (choice > value)
                return choice;
        return choices[N - 1];
    }
    for (size_t i = N; i > 0; --i)
        if (choices[i - 1] < value)
            return choices[i - 1];
    return choices[0];
}
inline uint8_t minerChanges(const MinerSettings& base, const MinerSettings& draft) {
    return (base.frequency != draft.frequency ? ChangeFrequency : 0) |
           (base.voltage != draft.voltage ? ChangeVoltage : 0) |
           (base.automatic != draft.automatic || (!draft.automatic && base.fan != draft.fan)
                ? ChangeFan
                : 0);
}
inline bool validMinerCommand(const MinerCommand& command) {
    if (command.restart)
        return command.mask == 0;
    if (!command.mask || (command.mask & ~7))
        return false;
    const auto& s = command.settings;
    return (!(command.mask & ChangeFrequency) || minerChoice(s.frequency, kMinerFrequencies)) &&
           (!(command.mask & ChangeVoltage) || minerChoice(s.voltage, kMinerVoltages)) &&
           (!(command.mask & ChangeFan) || (s.fan >= 0 && s.fan <= 100));
}
inline bool ControlMailbox::valid(const MinerCommand& next) { return validMinerCommand(next); }
inline const char* minerCommandMethod(const MinerCommand& command) {
    return command.restart ? "POST" : "PATCH";
}
inline const char* minerCommandUrl(const MinerCommand& command) {
    return command.restart ? config::kMinerRestartUrl : config::kMinerSystemUrl;
}

inline bool minerCommandBody(const MinerCommand& command, char* out, size_t size) {
    if (!validMinerCommand(command) || size < 128)
        return false;
    if (command.restart) {
        snprintf(out, size, "{}");
        return true;
    }
    size_t at = 0;
    out[at++] = '{';
    if (command.mask & ChangeFrequency)
        at += snprintf(out + at, size - at, "\"frequency\":%d,", command.settings.frequency);
    if (command.mask & ChangeVoltage)
        at += snprintf(out + at, size - at, "\"coreVoltage\":%d,", command.settings.voltage);
    if (command.mask & ChangeFan)
        at += snprintf(out + at, size - at, "\"autofanspeed\":%s,\"fanspeed\":%d,",
                       command.settings.automatic ? "true" : "false", command.settings.fan);
    out[at - 1] = '}';
    out[at] = 0;
    return true;
}
inline bool readMinerSettings(const MinerSample& m, MinerSettings& out) {
    constexpr uint16_t required = (1u << CoreSet) | (1u << FanPercent) | (1u << AutoFan);
    if (!m.bm1366 || !m.valid || !(m.detailsMask & 4) || (m.extraMask & required) != required ||
        std::floor(m.frequencyMHz) != m.frequencyMHz ||
        std::floor(m.extra[CoreSet]) != m.extra[CoreSet])
        return false;
    out.frequency = int(m.frequencyMHz);
    out.voltage = int(m.extra[CoreSet]);
    out.fan = int(std::lround(m.extra[FanPercent]));
    out.automatic = m.extra[AutoFan] != 0;
    return true;
}
struct ControlRect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
constexpr ControlRect kClockSettingsButton{30, 194, 100, 22};
constexpr ControlRect kDisplayTab{10, 40, 145, 28}, kMinerTab{165, 40, 145, 28};
constexpr ControlRect kRestartButton{236, 8, 74, 24};
constexpr ControlRect kAutoButton{174, 76, 116, 27};
constexpr ControlRect kBrightnessMinus{174, 72, 32, 20}, kBrightnessPlus{258, 72, 32, 20};
constexpr ControlRect kNightButton{174, 93, 116, 20};
constexpr ControlRect kDisplayMinusButtons[] = {
    {174, 114, 32, 20}, {174, 135, 32, 20}, {174, 156, 32, 20}, {174, 177, 32, 20}};
constexpr ControlRect kDisplayPlusButtons[] = {
    {258, 114, 32, 20}, {258, 135, 32, 20}, {258, 156, 32, 20}, {258, 177, 32, 20}};
constexpr ControlRect kMinusButtons[] = {
    {174, 106, 32, 28}, {174, 136, 32, 28}, {174, 166, 32, 28}};
constexpr ControlRect kPlusButtons[] = {{258, 106, 32, 28}, {258, 136, 32, 28}, {258, 166, 32, 28}};
constexpr ControlRect kBackButton{10, 212, 136, 24}, kSaveButton{174, 212, 136, 24};
constexpr ControlRect kCancelRestart{30, 170, 116, 32}, kConfirmRestart{174, 170, 116, 32};
inline void editMinerSetting(ControlEditor& editor, int x, int y) {
    if (!editor.ready || editor.view != ControlView::Miner)
        return;
    auto& s = editor.draft;
    if (kAutoButton.contains(x, y)) {
        s.automatic = !s.automatic;
        return;
    }
    for (int row = 0; row < 3; ++row) {
        const int direction =
            kMinusButtons[row].contains(x, y) ? -1 : (kPlusButtons[row].contains(x, y) ? 1 : 0);
        if (!direction)
            continue;
        if (row == 0 && !s.automatic)
            s.fan = int(std::fmax(0, std::fmin(100, s.fan + direction * 5)));
        if (row == 1)
            s.frequency = stepMinerChoice(s.frequency, direction, kMinerFrequencies);
        if (row == 2)
            s.voltage = stepMinerChoice(s.voltage, direction, kMinerVoltages);
    }
}
