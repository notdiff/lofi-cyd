#pragma once
#include "display_settings.h"
#include "screen_frame.h"

inline void settingsHeader(ScreenFrame& frame, ControlView view) {
    screenField(frame, 10, 10, 220, 20, 14, kScreenWhite,
                view == ControlView::Restart ? "RESTART MINER?" : "SETTINGS");
    screenButton(frame, kDisplayTab, "Display");
    frame.fields[frame.count - 1].color = view == ControlView::Display ? kScreenWhite : kScreenCyan;
    screenButton(frame, kMinerTab, "Miner");
    frame.fields[frame.count - 1].color = view != ControlView::Display ? kScreenWhite : kScreenCyan;
}

inline ScreenFrame makeDisplaySettingsFrame(const DisplaySettings& settings, bool timeValid,
                                            bool saved = false) {
    ScreenFrame frame;
    settingsHeader(frame, ControlView::Display);
    char text[32];
    screenField(frame, 10, 74, 154, 17, 13, kScreenWhite, "Brightness");
    screenButton(frame, kBrightnessMinus, "-");
    screenButton(frame, kBrightnessPlus, "+");
    snprintf(text, sizeof(text), "%u%%", unsigned(settings.brightness));
    screenField(frame, 209, 74, 47, 17, 13, kScreenCyan, text, true);
    screenField(frame, 10, 95, 154, 17, 13, kScreenWhite, "Night mode");
    screenButton(frame, kNightButton, settings.night ? "ON" : "OFF");
    const char* labels[] = {"Screen off", "Screen on", "UTC offset", "Auto pages"};
    for (int row = 0; row < 4; ++row) {
        const int y = kDisplayMinusButtons[row].y + 2;
        const bool enabled = row >= 2 || settings.night;
        screenField(frame, 10, y, 154, 17, 13, enabled ? kScreenWhite : kScreenMuted, labels[row]);
        screenButton(frame, kDisplayMinusButtons[row], "-", enabled);
        screenButton(frame, kDisplayPlusButtons[row], "+", enabled);
        if (row == 2)
            formatUtcOffset(text, sizeof(text), settings.utcMinutes);
        else if (row == 3) {
            if (settings.pageMinutes)
                snprintf(text, sizeof(text), "%um", unsigned(settings.pageMinutes));
            else
                snprintf(text, sizeof(text), "OFF");
        } else {
            const int minute = row == 0 ? settings.offMinute : settings.onMinute;
            snprintf(text, sizeof(text), "%02d:%02d", minute / 60, minute % 60);
        }
        screenField(frame, 209, y, 47, 17, 13, enabled ? kScreenCyan : kScreenMuted, text, true);
    }
    const char* status =
        saved
            ? "SAVED"
            : (!timeValid ? "NTP WAIT / screen stays on"
                          : (settings.offMinute == settings.onMinute ? "Equal times: always on"
                                                                     : "Time / UTC step: 15 min"));
    screenField(frame, 10, 198, 300, 13, 10, saved ? kScreenCyan : kScreenMuted, status);
    screenButton(frame, kBackButton, "Close");
    screenButton(frame, kSaveButton, "Save");
    return frame;
}

inline ScreenFrame makeControlFrame(const ControlEditor& editor, const ControlReport& report,
                                    bool fresh) {
    ScreenFrame frame;
    settingsHeader(frame, editor.view);
    const bool pending = report.phase == ControlPhase::Pending;
    const bool enabled = fresh && !pending;
    char text[32];
    if (editor.view == ControlView::Restart) {
        screenField(frame, 10, 91, 300, 17, 13, kScreenWhite, "Mining will pause briefly.", true);
        screenField(frame, 10, 118, 300, 17, 13, kScreenMuted, "Restart only the miner.", true);
        screenButton(frame, kCancelRestart, "Cancel");
        screenButton(frame, kConfirmRestart, "Restart", enabled);
    } else {
        screenButton(frame, kRestartButton, "Restart", enabled);
        screenField(frame, 10, 81, 154, 17, 13, kScreenWhite, "Fan speed");
        screenButton(frame, kAutoButton, editor.draft.automatic ? "AUTO" : "MANUAL",
                     enabled && editor.ready);
        const char* labels[] = {"Manual fan %", "Frequency MHz", "Core Voltage mV"};
        const int values[] = {editor.draft.fan, editor.draft.frequency, editor.draft.voltage};
        for (int i = 0; i < 3; ++i) {
            const int y = kMinusButtons[i].y + 5;
            const bool editable = enabled && editor.ready && (i != 0 || !editor.draft.automatic);
            screenField(frame, 10, y, 154, 17, 13,
                        i == 0 && editor.draft.automatic ? kScreenMuted : kScreenWhite, labels[i]);
            screenButton(frame, kMinusButtons[i], "-", editable);
            screenButton(frame, kPlusButtons[i], "+", editable);
            if (editor.ready)
                snprintf(text, sizeof(text), "%d", values[i]);
            else
                snprintf(text, sizeof(text), "--");
            screenField(frame, 209, y, 47, 17, 13, editable ? kScreenCyan : kScreenMuted, text,
                        true);
        }
        const char* status = !fresh
                                 ? "MINER OFFLINE"
                                 : (!editor.ready ? "SETTINGS UNAVAILABLE" : "Tap Save to apply");
        if (pending)
            status = "SENDING...";
        else if (editor.requestId && editor.requestId == report.id) {
            if (report.phase == ControlPhase::Saved)
                status = "SAVED / restart if needed";
            else if (report.phase == ControlPhase::Restarted)
                status = "RESTART SENT";
            else if (report.phase == ControlPhase::Unknown)
                status = "NO REPLY / check console";
            else if (report.phase == ControlPhase::Failed) {
                snprintf(text, sizeof(text), "COMMAND FAILED / HTTP %d", report.http);
                status = text;
            }
        }
        screenField(frame, 10, 197, 300, 13, 10, kScreenMuted, status);
        screenButton(frame, kBackButton, "Close");
        screenButton(frame, kSaveButton, "Save",
                     enabled && editor.ready && minerChanges(editor.base, editor.draft));
    }
    return frame;
}
