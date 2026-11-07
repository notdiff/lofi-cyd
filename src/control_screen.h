#pragma once
#include "screen_frame.h"
inline ScreenFrame makeControlFrame(const ControlEditor& editor, const ControlReport& report,
                                    bool fresh) {
    ScreenFrame frame;
    const bool pending = report.phase == ControlPhase::Pending;
    const bool enabled = fresh && !pending;
    char text[32];
    screenField(frame, 30, 34, 260, 18, 14, kScreenWhite,
                editor.view == ControlView::Settings ? "LV07 / SETTINGS" : "RESTART LV07?");
    if (editor.view == ControlView::Restart) {
        screenField(frame, 30, 79, 260, 16, 13, kScreenWhite, "Mining will pause briefly.", true);
        screenField(frame, 30, 102, 260, 16, 13, kScreenMuted, "Restart only the miner.", true);
        screenButton(frame, kCancelRestart, "Cancel");
        screenButton(frame, kConfirmRestart, "Restart", enabled);
    } else {
        screenField(frame, 30, 58, 134, 17, 13, kScreenWhite, "Fan speed");
        screenButton(frame, kAutoButton, editor.draft.automatic ? "AUTO" : "MANUAL",
                     enabled && editor.ready);
        const char* labels[] = {"Manual fan %", "Frequency MHz", "Core Voltage mV"};
        const int values[] = {editor.draft.fan, editor.draft.frequency, editor.draft.voltage};
        for (int i = 0; i < 3; ++i) {
            const int y = 84 + i * 32;
            const bool editable = enabled && editor.ready && (i != 0 || !editor.draft.automatic);
            screenField(frame, 30, y + 5, 134, 17, 13,
                        i == 0 && editor.draft.automatic ? kScreenMuted : kScreenWhite, labels[i]);
            screenButton(frame, kMinusButtons[i], "-", editable);
            screenButton(frame, kPlusButtons[i], "+", editable);
            if (editor.ready)
                snprintf(text, sizeof(text), "%d", values[i]);
            else
                snprintf(text, sizeof(text), "--");
            screenField(frame, 209, y + 5, 47, 17, 13, editable ? kScreenCyan : kScreenMuted, text,
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
            else if (report.phase == ControlPhase::Unknown)
                status = "NO REPLY / check console";
            else if (report.phase == ControlPhase::Failed) {
                snprintf(text, sizeof(text), "SAVE FAILED / HTTP %d", report.http);
                status = text;
            }
        }
        screenField(frame, 30, 179, 260, 13, 10, kScreenMuted, status);
        screenButton(frame, kBackButton, "Back");
        screenButton(frame, kSaveButton, "Save",
                     enabled && editor.ready && minerChanges(editor.base, editor.draft));
    }
    return frame;
}
