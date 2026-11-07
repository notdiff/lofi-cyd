#include "config.h"
#include "control_screen.h"
#include "demo_data.h"
#include "display.h"
#include "network.h"
#include "rabbit_scene.h"
#include "screens.h"
#include "touch.h"
#include <Arduino.h>
#include <Preferences.h>

DisplayRenderer display;
Preferences preferences;
enum class ViewMode { Live, Background, Demo };
ViewMode viewMode = ViewMode::Live;
bool inverted = DEFAULT_INVERT;
uint8_t rotation = 1;
bool swapRedBlue = false;
constexpr uint8_t kButton = 0;
constexpr uint32_t kLongPressMs = 1500;
constexpr uint8_t kColorConfigVersion = 1;
Page activePage = Page::Overview;
ScreenFrame lastFrame;
Page lastDisplayPage = Page::Overview;
bool lastSetup = false;
bool liveNeedsRedraw = true;
RabbitAnimator rabbit;
RabbitPose lastRabbitPose = {255, 0, RabbitAction::Idle};
uint8_t lastRabbitPage = 255;
uint32_t rabbitDrawAt = 0, rabbitFrames = 0, rabbitWorstDrawUs = 0;
BtcChart lastChart;
ControlEditor controlEditor;
ControlView lastControlView = ControlView::Closed;
DisplaySettings displaySettings;
DisplaySettings displayDraft;
bool displayDraftSaved = false;
BacklightSchedule backlightSchedule;
PageSchedule pageSchedule;
bool backlightOn = true;
constexpr uint8_t kBacklightPin = 21, kBacklightChannel = 0;
uint8_t backlightDuty = 0;

bool fieldChanged(const ScreenField& a, const ScreenField& b) {
    return a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h || a.fontSize != b.fontSize ||
           a.color != b.color || a.centered != b.centered || a.button != b.button ||
           strcmp(a.text, b.text);
}
void drawRabbit(Page page, uint32_t now, bool force = false) {
    if (!backlightOn || controlEditor.view != ControlView::Closed)
        return;
    if (!force && uint32_t(now - rabbitDrawAt) < config::kRabbitRefreshMs)
        return;
    rabbitDrawAt = now;
    const auto number = static_cast<uint8_t>(page);
    if (!rabbitEnabled(number)) {
        lastRabbitPage = number;
        lastRabbitPose = {255, 0, RabbitAction::Idle};
        return;
    }
    const auto pose = rabbit.update(number, now);
    if (pose.action != lastRabbitPose.action)
        Serial.printf("RABBIT action=%s page=%u\n", rabbitActionName(pose.action),
                      unsigned(number + 1));
    if (!force && number == lastRabbitPage && pose.frame == lastRabbitPose.frame &&
        pose.lift == lastRabbitPose.lift && pose.action == lastRabbitPose.action &&
        pose.shift == lastRabbitPose.shift && pose.phase == lastRabbitPose.phase) {
        lastRabbitPose = pose;
        return;
    }
    const auto& sprite = rabbitSpriteForPose(number, pose);
    const uint32_t startedAt = micros();
    display.drawRabbit(page, sprite, pose);
    rabbitWorstDrawUs = max(rabbitWorstDrawUs, uint32_t(micros() - startedAt));
    ++rabbitFrames;
    lastRabbitPose = pose;
    lastRabbitPage = number;
}
void printRabbitStatus() {
    Serial.printf("RABBIT: page=%u action=%s frame=%u draws=%lu blink=%lu wave=%lu helmet=%lu "
                  "sleepActions=%lu falls=%lu maxDraw=%lu us\n",
                  unsigned(lastRabbitPage + 1),
                  rabbitEnabled(lastRabbitPage) ? rabbitActionName(lastRabbitPose.action)
                                                : "hidden",
                  lastRabbitPose.frame, (unsigned long)rabbitFrames, (unsigned long)rabbit.blinks,
                  (unsigned long)rabbit.waves, (unsigned long)rabbit.helmets,
                  (unsigned long)rabbit.sleepActions, (unsigned long)rabbit.falls,
                  (unsigned long)rabbitWorstDrawUs);
}
void reactRabbit() {
    if (viewMode == ViewMode::Background || networkSetupActive() ||
        controlEditor.view != ControlView::Closed)
        return;
    const uint32_t now = millis();
    if (rabbit.react(static_cast<uint8_t>(lastDisplayPage), now)) {
        Serial.printf("RABBIT reaction accepted page=%u\n",
                      unsigned(static_cast<uint8_t>(lastDisplayPage) + 1));
        drawRabbit(lastDisplayPage, now, true);
    }
}
void drawLive(bool force = false) {
    const bool demo = viewMode == ViewMode::Demo;
    const bool setup = !demo && networkSetupActive();
    const Page displayPage = setup ? Page::Overview : activePage;
    const uint32_t now = millis();
    const bool connected = demo || networkConnected();
    const LiveSnapshot data = demo ? demoSnapshot(now) : networkSnapshot();
    const auto report = networkControlReport();
    if (setup || demo || displayPage != Page::Clock)
        controlEditor.view = ControlView::Closed;
    if (controlEditor.view == ControlView::Miner && !controlEditor.ready &&
        minerFresh(data.miner, now, connected)) {
        controlEditor.ready = readMinerSettings(data.miner, controlEditor.base);
        controlEditor.draft = controlEditor.base;
    }
    if (controlEditor.view != ControlView::Closed && controlEditor.requestId == report.id &&
        report.phase == ControlPhase::Saved)
        controlEditor.base = controlEditor.draft;
    const int64_t epoch = demo ? 1791244800LL + 43200 : time(nullptr);
    const BtcHistory history =
        displayPage == Page::Bitcoin
            ? (demo ? demoHistory(now, epoch, data.btc.priceUSD) : networkHistory())
            : BtcHistory{};
    ScreenFrame frame =
        setup
            ? makeSetupFrame()
            : (controlEditor.view == ControlView::Display
                   ? makeDisplaySettingsFrame(displayDraft, epoch >= 1700000000, displayDraftSaved)
                   : (controlEditor.view != ControlView::Closed
                          ? makeControlFrame(controlEditor, report,
                                             minerFresh(data.miner, now, connected))
                          : makeScreenFrame(displayPage, data, now, epoch, connected, demo,
                                            &history, displaySettings.utcMinutes)));
    if (displayPage == Page::Details && controlEditor.view == ControlView::Closed && !demo &&
        !setup) {
        if (report.phase == ControlPhase::Pending)
            snprintf(frame.fields[2].text, sizeof(frame.fields[2].text), "SENDING...");
        else if (report.phase == ControlPhase::Restarted && uint32_t(now - report.at) < 15000)
            snprintf(frame.fields[2].text, sizeof(frame.fields[2].text), "RESTART SENT");
        else if ((report.phase == ControlPhase::Failed || report.phase == ControlPhase::Unknown) &&
                 uint32_t(now - report.at) < 15000)
            snprintf(frame.fields[2].text, sizeof(frame.fields[2].text), "CMD %s",
                     report.phase == ControlPhase::Unknown ? "NO REPLY" : "FAILED");
    }
    if (!demo && !data.miner.valid && (setup || displayPage == Page::Overview)) {
        if (setup) {
            char status[32];
            networkStatusText(status, sizeof(status));
            screenField(frame, 26, 151, 137, 17, 10, kScreenMuted, status);
        } else
            networkStatusText(frame.fields[3].text, sizeof(frame.fields[3].text));
    }
    force = force || displayPage != lastDisplayPage || setup != lastSetup ||
            frame.count != lastFrame.count || controlEditor.view != lastControlView;
    if (force) {
        if (controlEditor.view != ControlView::Closed)
            display.drawSettingsPanel();
        else
            display.drawBackground(displayPage);
    }
    if (displayPage == Page::Bitcoin && !setup && controlEditor.view == ControlView::Closed) {
        const auto chart = makeBtcChart(history, now, epoch, connected);
        if (force || chart.valid != lastChart.valid || chart.color != lastChart.color ||
            memcmp(chart.y, lastChart.y, sizeof(chart.y))) {
            display.drawBtcChart(chart);
            lastChart = chart;
        }
    }
    for (uint8_t i = 0; i < frame.count; ++i)
        if (force || fieldChanged(frame.fields[i], lastFrame.fields[i]))
            display.drawField(displayPage, frame.fields[i],
                              controlEditor.view != ControlView::Closed);
    if (displayPage == Page::Overview && !setup) {
        const uint16_t indicator = demo ? 0xFD20
                                        : (minerFresh(data.miner, now, connected)
                                               ? kScreenCyan
                                               : (data.miner.valid ? 0xFD20 : kScreenMuted));
        static uint16_t lastIndicator = 0;
        if (force || indicator != lastIndicator)
            display.drawIndicator(indicator);
        lastIndicator = indicator;
    }
    lastFrame = frame;
    lastDisplayPage = displayPage;
    lastSetup = setup;
    lastControlView = controlEditor.view;
    drawRabbit(displayPage, now, force);
}
void render() {
    display.configure(rotation, inverted, swapRedBlue);
    touchSetRotation(rotation);
    if (viewMode == ViewMode::Background)
        display.drawBackground(activePage);
    else
        drawLive(true);
}

void applyBrightness() {
    const auto& settings =
        controlEditor.view != ControlView::Closed ? displayDraft : displaySettings;
    const uint8_t duty = backlightOn ? brightnessDuty(settings.brightness) : 0;
    if (duty != backlightDuty) {
        ledcWrite(kBacklightChannel, duty);
        backlightDuty = duty;
    }
}

void updateBacklight() {
    const bool light = backlightSchedule.shouldLight(displaySettings, time(nullptr), millis());
    if (light != backlightOn) {
        backlightOn = light;
        if (!light) {
            controlEditor.view = ControlView::Closed;
            applyBrightness();
        } else
            render();
        Serial.printf("DISPLAY: %s\n", light ? "awake" : "night / backlight off");
    }
    applyBrightness();
}
bool wakeDisplay() {
    pageSchedule.reset(millis());
    const bool wasOff = !backlightOn;
    backlightSchedule.wake(millis());
    updateBacklight();
    return wasOff;
}

void changePage(Swipe direction, bool automatic = false) {
    pageSchedule.reset(millis());
    controlEditor.view = ControlView::Closed;
    activePage = static_cast<Page>(adjacentPage(static_cast<uint8_t>(activePage), direction));
    if (!automatic)
        preferences.putUChar("page", static_cast<uint8_t>(activePage));
    viewMode = ViewMode::Live;
    render();
}
void nextPage() { changePage(Swipe::Next); }

void selectSettingsTab(ControlView view) {
    controlEditor.view = view;
    if (view == ControlView::Miner && !controlEditor.ready) {
        controlEditor.ready = readMinerSettings(networkSnapshot().miner, controlEditor.base);
        controlEditor.draft = controlEditor.base;
    }
    liveNeedsRedraw = true;
}

void openSettings(bool miner = false) {
    pageSchedule.reset(millis());
    activePage = Page::Clock;
    controlEditor = ControlEditor{};
    displayDraft = displaySettings;
    displayDraftSaved = false;
    selectSettingsTab(miner ? ControlView::Miner : ControlView::Display);
}

void saveDisplayPreferences() {
    preferences.putUChar("brightness", displaySettings.brightness);
    preferences.putUChar("pagemin", displaySettings.pageMinutes);
    preferences.putBool("night", displaySettings.night);
    preferences.putUShort("offmin", displaySettings.offMinute);
    preferences.putUShort("onmin", displaySettings.onMinute);
    preferences.putShort("utcmin", displaySettings.utcMinutes);
}

bool settingsTap(int x, int y) {
    if (viewMode != ViewMode::Live || activePage != Page::Clock || networkSetupActive())
        return false;
    if (controlEditor.view == ControlView::Closed) {
        if (!kClockSettingsButton.contains(x, y))
            return false;
        openSettings();
        return true;
    }
    if (kDisplayTab.contains(x, y)) {
        selectSettingsTab(ControlView::Display);
        return true;
    }
    if (kMinerTab.contains(x, y)) {
        selectSettingsTab(ControlView::Miner);
        return true;
    }
    if (controlEditor.view != ControlView::Restart && kBackButton.contains(x, y)) {
        controlEditor.view = ControlView::Closed;
        return true;
    }
    if (controlEditor.view == ControlView::Display) {
        if (kSaveButton.contains(x, y)) {
            displaySettings = validatedDisplaySettings(displayDraft);
            saveDisplayPreferences();
            displayDraftSaved = true;
            backlightSchedule.wake(millis());
        } else {
            if (kNightButton.contains(x, y))
                displayDraft.night = !displayDraft.night;
            else if (kBrightnessMinus.contains(x, y))
                stepDisplaySetting(displayDraft, 3, -1);
            else if (kBrightnessPlus.contains(x, y))
                stepDisplaySetting(displayDraft, 3, 1);
            else
                for (int row = 0; row < 4; ++row) {
                    const int direction = kDisplayMinusButtons[row].contains(x, y)
                                              ? -1
                                              : (kDisplayPlusButtons[row].contains(x, y) ? 1 : 0);
                    stepDisplaySetting(displayDraft, row == 3 ? 4 : row, direction);
                }
            displayDraftSaved = false;
        }
        return true;
    }
    const auto report = networkControlReport();
    const bool enabled = report.phase != ControlPhase::Pending &&
                         minerFresh(networkSnapshot().miner, millis(), networkConnected());
    if (controlEditor.view == ControlView::Restart) {
        if (kCancelRestart.contains(x, y))
            controlEditor.view = ControlView::Miner;
        else if (kConfirmRestart.contains(x, y) && enabled) {
            MinerCommand command;
            command.restart = true;
            if (networkMinerCommand(command, controlEditor.requestId))
                controlEditor.view = ControlView::Miner;
        }
    } else if (kRestartButton.contains(x, y) && enabled)
        controlEditor.view = ControlView::Restart;
    else if (enabled && controlEditor.ready) {
        if (kSaveButton.contains(x, y)) {
            MinerCommand command;
            command.settings = controlEditor.draft;
            command.mask = minerChanges(controlEditor.base, controlEditor.draft);
            if (command.mask)
                networkMinerCommand(command, controlEditor.requestId);
        } else {
            editMinerSetting(controlEditor, x, y);
            controlEditor.requestId = 0;
        }
    }
    return true;
}

void saveDisplaySettings() {
    preferences.putUChar("rotation", rotation);
    preferences.putBool("inverted", inverted);
    preferences.putBool("swapRB", swapRedBlue);
}

void setup() {
    Serial.begin(115200);
    rabbit = RabbitAnimator(esp_random());
    pinMode(kButton, INPUT_PULLUP);

    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    preferences.begin("workshop", false);
    const uint8_t savedPage = preferences.getUChar("page", 1);
    activePage = static_cast<Page>(savedPage < 4 ? savedPage : 1);
    rotation = preferences.getUChar("rotation", 1);
    if (rotation != 1 && rotation != 3)
        rotation = 1;
    inverted = preferences.getBool("inverted", DEFAULT_INVERT);
    swapRedBlue = preferences.getBool("swapRB", false);
    displaySettings.brightness = preferences.getUChar("brightness", displaySettings.brightness);
    displaySettings.pageMinutes = preferences.getUChar("pagemin", displaySettings.pageMinutes);
    displaySettings.night = preferences.getBool("night", displaySettings.night);
    displaySettings.offMinute = preferences.getUShort("offmin", displaySettings.offMinute);
    displaySettings.onMinute = preferences.getUShort("onmin", displaySettings.onMinute);
    displaySettings.utcMinutes = preferences.getShort("utcmin", displaySettings.utcMinutes);
    displaySettings = validatedDisplaySettings(displaySettings);

    if (preferences.getUChar("colorcfg", 0) < kColorConfigVersion) {
        inverted = DEFAULT_INVERT;
        preferences.putBool("inverted", inverted);
        preferences.putUChar("colorcfg", kColorConfigVersion);
    }
    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, LOW);
    display.begin();
    ledcSetup(kBacklightChannel, 20000, 8);
    backlightDuty = 0;
    ledcWrite(kBacklightChannel, 0);
    ledcAttachPin(kBacklightPin, kBacklightChannel);
    touchBegin(rotation);
    render();
    applyBrightness();
    networkBegin();
    liveNeedsRedraw = true;
    Serial.printf("Lofi Miner v%s\n", config::kVersion);
    Serial.println("Serial keys: 1-4 = page, n = next, e = miner settings, t = display settings, x "
                   "= close settings, a "
                   "= bunny reaction, q = diagnostics, l = LIVE, d = DEMO, b = background, w = "
                   "Wi-Fi setup, r = rotate 180, i = invert, s = red/blue swap.");
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
}

bool updateBootButton() {

    static bool lastReading = HIGH;
    static bool stableReading = HIGH;
    static uint32_t changedAt = 0;
    static uint32_t pressedAt = 0;
    static bool longPressHandled = false;
    const bool reading = digitalRead(kButton);
    if (reading != lastReading) {
        changedAt = millis();
        lastReading = reading;
    }
    if (millis() - changedAt >= 40 && reading != stableReading) {
        stableReading = reading;
        if (stableReading == LOW) {
            pressedAt = millis();
            longPressHandled = wakeDisplay();
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
    return stableReading;
}

void handleSerialInput() {
    if (Serial.available()) {
        const char c = Serial.read();
        if (c != '\n' && c != '\r')
            wakeDisplay();
        bool redraw = true;
        bool changedSettings = false;
        switch (c) {
        case 'l':
            controlEditor.view = ControlView::Closed;
            viewMode = ViewMode::Live;
            break;
        case 'd':
            controlEditor.view = ControlView::Closed;
            viewMode = ViewMode::Demo;
            break;
        case 'b':
            controlEditor.view = ControlView::Closed;
            viewMode = ViewMode::Background;
            break;
        case 'e':
        case 't':
            if (viewMode == ViewMode::Live && !networkSetupActive())
                openSettings(c == 'e');
            break;
        case 'x':
            controlEditor.view = ControlView::Closed;
            break;
        case 'w':
            networkOpenSetup();
            viewMode = ViewMode::Live;
            break;
        case 'q':
            networkPrintStatus();
            touchPrintStatus();
            printRabbitStatus();
            Serial.printf("PAGE: %u\n", unsigned(static_cast<uint8_t>(activePage) + 1));
            Serial.printf("DISPLAY: light=%d brightness=%u%% PWM=%u night=%d off=%02u:%02u "
                          "on=%02u:%02u UTC=%d min\n",
                          backlightOn, unsigned(displaySettings.brightness),
                          unsigned(ledcRead(kBacklightChannel)), displaySettings.night,
                          displaySettings.offMinute / 60, displaySettings.offMinute % 60,
                          displaySettings.onMinute / 60, displaySettings.onMinute % 60,
                          displaySettings.utcMinutes);
            redraw = false;
            break;
        case 'a':
            reactRabbit();
            redraw = false;
            break;
        case 'n':
            nextPage();
            redraw = false;
            break;
        case '1':
        case '2':
        case '3':
        case '4':
            controlEditor.view = ControlView::Closed;
            activePage = static_cast<Page>(c - '1');
            preferences.putUChar("page", static_cast<uint8_t>(activePage));
            viewMode = ViewMode::Live;
            break;
        case 'r':
            rotation = rotation == 1 ? 3 : 1;
            changedSettings = true;
            break;
        case 'i':
            inverted = !inverted;
            changedSettings = true;
            break;
        case 's':
            swapRedBlue = !swapRedBlue;
            changedSettings = true;
            break;
        default:
            redraw = false;
        }
        if (changedSettings)
            saveDisplaySettings();
        if (redraw)
            render();
    }
}

void handleTouchInput(bool buttonReleased) {
    const auto event =
        touchPoll(millis(), buttonReleased && (!backlightOn || !networkSetupActive()));
    if ((event.tap || event.swipe != Swipe::None) && wakeDisplay())
        return;
    const auto swipe = event.swipe;
    if (swipe != Swipe::None) {
        changePage(swipe);
        Serial.printf("SWIPE %s -> PAGE: %u\n", swipe == Swipe::Next ? "LEFT" : "RIGHT",
                      unsigned(static_cast<uint8_t>(activePage) + 1));
    } else if (event.tap && settingsTap(event.x, event.y)) {
        liveNeedsRedraw = true;
    } else if (event.tap && viewMode != ViewMode::Background &&
               rabbitHit(static_cast<uint8_t>(lastDisplayPage), lastRabbitPose, event.x, event.y)) {
        reactRabbit();
    }
}

void loop() {
    updateBacklight();
    const bool buttonReleased = updateBootButton();
    handleSerialInput();
    networkLoop();
    handleTouchInput(buttonReleased);
    if (pageSchedule.due(displaySettings.pageMinutes, millis(),
                         backlightOn && viewMode == ViewMode::Live &&
                             controlEditor.view == ControlView::Closed && !networkSetupActive()))
        changePage(Swipe::Next, true);
    static uint32_t liveDrawAt = 0;
    if (backlightOn && viewMode != ViewMode::Background &&
        (liveNeedsRedraw || uint32_t(millis() - liveDrawAt) >= config::kScreenRefreshMs)) {
        drawLive(liveNeedsRedraw);
        liveNeedsRedraw = false;
        liveDrawAt = millis();
    }
    if (viewMode != ViewMode::Background)
        drawRabbit(lastDisplayPage, millis());
    delay(5);
}
