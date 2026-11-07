#include "network_internal.h"
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <atomic>
#include <esp_system.h>

namespace network_detail {
constexpr char kSetupName[] = "Lofi-Miner-Setup";
constexpr char kSetupPassword[] = "lofiminer";
WebServer portal(80);
Preferences wifiPreferences;
SemaphoreHandle_t snapshotMutex = nullptr;
LiveSnapshot snapshot;
BtcHistory history;
ControlMailbox controlMailbox;
bool setupActive = false;
bool closeSetupWhenConnected = true;
bool hasCredentials = false;
bool pendingConnect = false;
String pendingSsid, pendingPassword, setupToken;
uint32_t reconnectAt = 0, connectedAt = 0;
bool previouslyConnected = false;
std::atomic<uint8_t> disconnectReason{0};
std::atomic<uint32_t> pollingHeartbeat{0};
std::atomic<uint32_t> btcHeartbeat{0}, btcUpdates{0}, chainHeartbeat{0};
std::atomic<int> lastMinerHttp{0}, lastHeightHttp{0}, lastFeesHttp{0};
std::atomic<int> lastDifficultyHttp{0}, lastHistoryHttp{0};
std::atomic<bool> btcSocketConnected{false};

const char kForm[] PROGMEM = R"HTML(<!doctype html><html lang="ru"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Lofi Miner — Wi-Fi</title>
<style>body{background:#071b35;color:#eaf5ff;font:16px system-ui;max-width:420px;margin:40px auto;padding:20px}
input,button{box-sizing:border-box;width:100%;padding:12px;margin:8px 0 20px;border-radius:8px;border:1px solid #37618b}
button{background:#72d7ef;font-weight:700}small{color:#b0c6df}</style>
<h1>Lofi Miner</h1><p>Подключите экран к сети Wi-Fi 2,4 ГГц, в которой работает майнер.</p>
<form method="post" action="/save"><input type="hidden" name="token" value="%TOKEN%">
<label>Имя сети (SSID)<input name="ssid" maxlength="32" required autocomplete="off"></label>
<label>Пароль<input name="password" type="password" maxlength="63" autocomplete="off"></label>
<button>Сохранить и подключиться</button></form><small>Настройки сохраняются только на CYD.
Майнер: %MINERHOST%. Для сети без пароля оставьте поле пустым.</small></html>)HTML";

void connectSaved() {
    const String ssid = wifiPreferences.getString("ssid", "");
    const String password = wifiPreferences.getString("password", "");
    hasCredentials = !ssid.isEmpty();
    if (!hasCredentials)
        return;
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.printf("Wi-Fi connection requested (saved credentials; status=%d)\n",
                  int(WiFi.status()));
    reconnectAt = millis();
}
}

using namespace network_detail;

void networkOpenSetup() {
    if (setupActive)
        return;
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(kSetupName, kSetupPassword)) {
        Serial.println("Could not start Wi-Fi setup AP");
        return;
    }
    char token[33];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());
    setupToken = token;
    closeSetupWhenConnected = !networkConnected();
    setupActive = true;
    portal.begin();
    Serial.println("Wi-Fi setup: Lofi-Miner-Setup / lofiminer; http://192.168.4.1");
}

void networkBegin() {
    snapshotMutex = xSemaphoreCreateMutex();
    wifiPreferences.begin("lofi-wifi", false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("lofi-miner");
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            disconnectReason.store(info.wifi_sta_disconnected.reason);
            Serial.printf("Wi-Fi disconnected: reason=%u\n",
                          unsigned(info.wifi_sta_disconnected.reason));
        } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
            disconnectReason.store(0);
            Serial.println("Wi-Fi associated; waiting for DHCP");
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            disconnectReason.store(0);
            Serial.println("Wi-Fi got IP");
        }
    });
    portal.on("/", HTTP_GET, []() {
        String form = FPSTR(kForm);
        form.replace("%TOKEN%", setupToken);
        form.replace("%MINERHOST%", config::kMinerHost);
        portal.sendHeader("Cache-Control", "no-store");
        portal.send(200, "text/html; charset=utf-8", form);
    });
    portal.on("/save", HTTP_POST, []() {
        if (!setupActive || portal.arg("token") != setupToken) {
            portal.send(403, "text/plain; charset=utf-8", "Откройте страницу настройки заново.");
            return;
        }
        String ssid = portal.arg("ssid"), password = portal.arg("password");
        if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
            (password.length() > 0 && password.length() < 8)) {
            portal.send(400, "text/plain; charset=utf-8",
                        "Проверьте имя сети и пароль (8–63 символа или пустой).");
            return;
        }
        pendingSsid = ssid;
        pendingPassword = password;
        pendingConnect = true;
        closeSetupWhenConnected = true;
        portal.send(200, "text/html; charset=utf-8",
                    "<meta charset='utf-8'><meta name='viewport' "
                    "content='width=device-width,initial-scale=1'>"
                    "<h2>Настройки приняты</h2><p>CYD подключается к Wi-Fi. Следите за экраном. "
                    "После подключения вернитесь в домашнюю сеть. Если данные не появятся, "
                    "снова откройте 192.168.4.1 и проверьте пароль.</p>");
    });
    portal.onNotFound([]() {
        portal.sendHeader("Location", "/");
        portal.send(302);
    });
    connectSaved();
    if (!hasCredentials)
        networkOpenSetup();
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
    if (!snapshotMutex) {
        Serial.println("ERROR: cannot create data mutex");
        return;
    }
    if (xTaskCreate(pollingTask, "lofi-miner", 12288, nullptr, 1, nullptr) != pdPASS)
        Serial.println("ERROR: miner task");
    if (xTaskCreate(btcTask, "lofi-btc", 16384, nullptr, 1, nullptr) != pdPASS)
        Serial.println("ERROR: BTC task");
    if (xTaskCreate(chainTask, "lofi-chain", 12288, nullptr, 1, nullptr) != pdPASS)
        Serial.println("ERROR: chain task");
}

void networkLoop() {
    if (setupActive)
        portal.handleClient();
    if (pendingConnect) {
        pendingConnect = false;
        wifiPreferences.putString("ssid", pendingSsid);
        wifiPreferences.putString("password", pendingPassword);
        pendingPassword = "";
        WiFi.disconnect();
        connectSaved();
        previouslyConnected = false;
    }
    const bool connected = networkConnected();
    const uint32_t now = millis();
    if (connected && !previouslyConnected) {
        connectedAt = now;
        Serial.printf("Wi-Fi connected; CYD IP: %s\n", WiFi.localIP().toString().c_str());
    }
    if (!connected && hasCredentials && uint32_t(now - reconnectAt) >= 30000) {
        networkOpenSetup();
        WiFi.reconnect();
        reconnectAt = now;
    }
    if (setupActive && closeSetupWhenConnected && connected &&
        uint32_t(now - connectedAt) > 10000) {
        portal.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        setupActive = false;
    }
    previouslyConnected = connected;
}

ControlReport networkControlReport() {
    ControlReport copy;
    if (!snapshotMutex)
        return copy;
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    copy = controlMailbox.report;
    xSemaphoreGive(snapshotMutex);
    return copy;
}
bool networkMinerCommand(const MinerCommand& command, uint32_t& id) {
    if (!snapshotMutex || !validMinerCommand(command) || !networkConnected() ||
        networkSetupActive())
        return false;
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    const bool allowed =
        minerFresh(snapshot.miner, millis(), true) && (command.restart || snapshot.miner.bm1366);
    const bool accepted = allowed && controlMailbox.submit(command, millis(), id);
    xSemaphoreGive(snapshotMutex);
    return accepted;
}

LiveSnapshot networkSnapshot() {
    LiveSnapshot copy;
    if (snapshotMutex) {
        xSemaphoreTake(snapshotMutex, portMAX_DELAY);
        copy = snapshot;
        xSemaphoreGive(snapshotMutex);
    }
    return copy;
}
BtcHistory networkHistory() {
    BtcHistory copy;
    if (snapshotMutex) {
        xSemaphoreTake(snapshotMutex, portMAX_DELAY);
        copy = history;
        xSemaphoreGive(snapshotMutex);
    }
    return copy;
}
bool networkSetupActive() {
    return setupActive;
}
bool networkConnected() {
    return WiFi.status() == WL_CONNECTED;
}
void networkPrintStatus() {
    const auto data = networkSnapshot();
    Serial.printf("NET: wifi=%d reason=%u credentials=%d setup=%d IP=%s gateway=%s RSSI=%d\n",
                  int(WiFi.status()), unsigned(disconnectReason.load()), int(hasCredentials),
                  int(setupActive), WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
    Serial.printf("DATA: minerHTTP=%d BTCsocket=%d BTCupdates=%lu heightHTTP=%d feeHTTP=%d "
                  "NTP=%lld heap=%u\n",
                  lastMinerHttp.load(), int(btcSocketConnected.load()),
                  (unsigned long)btcUpdates.load(), lastHeightHttp.load(), lastFeesHttp.load(),
                  (long long)time(nullptr), ESP.getFreeHeap());
    Serial.printf("TASK ages: miner=%lu BTC=%lu chain=%lu ms\n",
                  (unsigned long)(millis() - pollingHeartbeat.load()),
                  (unsigned long)(millis() - btcHeartbeat.load()),
                  (unsigned long)(millis() - chainHeartbeat.load()));
    Serial.printf("DETAILS: ASIC=%.0f C VRM=%.0f C FAN=%.0f rpm FREQ=%.0f MHz CORE=%.3f V "
                  "uptime=%lu mask=%u\n",
                  data.miner.temperatureC, data.miner.vrmTemperatureC, data.miner.fanRPM,
                  data.miner.frequencyMHz, data.miner.coreVoltageV,
                  (unsigned long)data.miner.uptimeSeconds, unsigned(data.miner.detailsMask));
    Serial.printf("QUOTE: %.2f USD %+.2f%% LOW=%.2f HIGH=%.2f fresh=%d\n", data.btc.priceUSD,
                  data.btc.change24h, data.btc.lowUSD, data.btc.highUSD,
                  int(btcFresh(data.btc, millis(), networkConnected())));
    const auto chart = networkHistory();
    Serial.printf("EXTRA: mask=%u BEST=%s SESSION=%s FANpct=%.0f HEAPminer=%.0f "
                  "STRATUMfallback=%.0f OVERHEAT=%.0f\n",
                  unsigned(data.miner.extraMask), data.miner.bestDiff, data.miner.bestSessionDiff,
                  data.miner.extra[FanPercent], data.miner.extra[MinerHeap],
                  data.miner.extra[Fallback], data.miner.extra[Overheat]);
    Serial.printf(
        "BLOCK10m: difficultyHTTP=%d D=%.0f fresh=%d P=%.9f%% HISTORYhttp=%d points=%u fresh=%d\n",
        lastDifficultyHttp.load(), data.chain.difficulty,
        int(difficultyFresh(data.chain, millis(), networkConnected())),
        blockProbability10m(data.miner.hashRateGH, data.chain.difficulty) * 100,
        lastHistoryHttp.load(), unsigned(chart.count),
        int(historyFresh(chart, millis(), networkConnected())));
    Serial.printf("STACK: loopWatermark=%u\n", unsigned(uxTaskGetStackHighWaterMark(nullptr)));
}
void networkStatusText(char* out, size_t size) {
    const auto reason = disconnectReason.load();
    if (networkConnected()) {
        const int status = lastMinerHttp.load();
        if (status == 200)
            snprintf(out, size, "MINER: BAD DATA");
        else if (status)
            snprintf(out, size, "MINER HTTP %d", status);
        else
            snprintf(out, size, "MINER: WAIT");
    } else if (reason == 201)
        snprintf(out, size, "WIFI: NO AP (201)");
    else if (reason == 202 || reason == 15 || reason == 204)
        snprintf(out, size, "WIFI: AUTH (%u)", unsigned(reason));
    else if (reason)
        snprintf(out, size, "WIFI: ERROR %u", unsigned(reason));
    else
        snprintf(out, size, "WIFI: CONNECTING");
}
