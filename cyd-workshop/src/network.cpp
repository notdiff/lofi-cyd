#include "network.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_system.h>
#include <atomic>
#include <WebSocketsClient.h>
#include "metrics_json.h"
#include "tls_roots.h"

namespace {
constexpr char kMinerUrl[] = "http://192.168.0.249/api/system/info";
constexpr char kHeightUrl[] = "https://mempool.space/api/blocks/tip/height";
constexpr char kFeesUrl[] = "https://mempool.space/api/v1/fees/recommended";
constexpr char kSetupName[] = "Lofi-Miner-Setup";
constexpr char kSetupPassword[] = "lofiminer";
WebServer portal(80);
Preferences wifiPreferences;
SemaphoreHandle_t snapshotMutex = nullptr;
LiveSnapshot snapshot;
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
Майнер: 192.168.0.249. Для сети без пароля оставьте поле пустым.</small></html>)HTML";

// HTTPClient decodes chunked transfer before writing here. Reject oversized bodies.
class BoundedBody : public Stream {
public:
    char data[4097] = {};
    size_t length = 0;
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t *buffer, size_t size) override {
        if (size > sizeof(data) - 1 - length) return 0;
        memcpy(data + length, buffer, size);
        length += size;
        data[length] = '\0';
        return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};

bool getJson(WiFiClient &client, const char *url, JsonDocument &doc, JsonDocument &filter) {
    HTTPClient http;
    http.setConnectTimeout(2500);
    http.setTimeout(3000);
    http.setReuse(false);
    http.setUserAgent("Lofi-Miner/0.4");
    if (!http.begin(client, url)) return false;
    const int status = http.GET();
    (url == kMinerUrl ? lastMinerHttp : (url == kHeightUrl ? lastHeightHttp : lastFeesHttp)).store(status);
    bool ok = false;
    if (status == HTTP_CODE_OK && http.getSize() <= 4096) {
        BoundedBody body;
        const int result = http.writeToStream(&body);
        if (result >= 0 && body.length > 0) {
            const auto error = deserializeJson(doc, body.data, body.length,
                                              DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(8));
            ok = !error;
            if (error) Serial.printf("JSON decode error: %s (%u bytes)\n", error.c_str(), unsigned(body.length));
        } else Serial.printf("HTTP body read failed: %d (%u bytes)\n", result, unsigned(body.length));
    }
    if (!ok) Serial.printf("%s request failed (HTTP %d or invalid JSON)\n",
                           url == kMinerUrl ? "Miner" : "Chain", status);
    http.end();
    return ok;
}

void pollMiner() {
    WiFiClient client;
    StaticJsonDocument<512> filter;
    for (const char *field : {"hashRate", "temp", "power", "sharesAccepted", "vrTemp", "fanrpm",
                              "fanSpeed", "frequency", "coreVoltageActual", "uptimeSeconds", "sharesRejected"}) filter[field] = true;
    StaticJsonDocument<1024> doc;
    MinerSample sample;
    const bool ok = getJson(client, kMinerUrl, doc, filter) &&
                    parseMiner(doc.as<JsonVariantConst>(), millis(), sample);
    if (ok) Serial.printf("Miner OK: %.2f GH/s, %.0f C, %.1f W, %llu shares; details=%u\n",
                          sample.hashRateGH, sample.temperatureC, sample.powerW,
                          (unsigned long long)sample.sharesAccepted, unsigned(sample.detailsMask));
    else if (lastMinerHttp.load() == 200) Serial.println("Miner: required fields invalid or missing");
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    if (ok) snapshot.miner = sample;
    else snapshot.miner.requestOk = false;
    xSemaphoreGive(snapshotMutex);
}

void pollingTask(void *) {
    uint32_t minerAt = 0;
    bool due = true, wasConnected = false;
    while (true) {
        pollingHeartbeat.store(millis());
        const bool connected = networkConnected();
        if (connected && !wasConnected) due = true;
        wasConnected = connected;
        if (connected && (due || uint32_t(millis()-minerAt) >= kMinerPollMs)) {
            pollMiner(); due = false; minerAt = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void btcTask(void *) {
    WebSocketsClient socket;
    uint32_t reconnectMs = kBtcPollMs;
    uint32_t lastLog = 0;
    bool initialized = false, wasConnected = false;
    socket.onEvent([&](WStype_t type, uint8_t *payload, size_t length) {
        if (type == WStype_CONNECTED) {
            btcSocketConnected.store(true);
            socket.sendTXT(R"({"method":"subscribe","params":{"channel":"ticker","symbol":["BTC/USD"],"event_trigger":"trades","snapshot":true},"req_id":1})");
            Serial.println("BTC WebSocket connected; subscribing to Kraken BTC/USD");
        } else if (type == WStype_DISCONNECTED || type == WStype_ERROR) {
            btcSocketConnected.store(false);
            xSemaphoreTake(snapshotMutex,portMAX_DELAY);
            snapshot.btc.requestOk = false;
            xSemaphoreGive(snapshotMutex);
            reconnectMs = min(reconnectMs * 2, uint32_t(300000));
            socket.setReconnectInterval(reconnectMs);
            Serial.printf("BTC WebSocket disconnected; retry interval %lu sec\n",(unsigned long)(reconnectMs/1000));
        } else if (type == WStype_TEXT && length > 0 && length <= 4096) {
            StaticJsonDocument<2048> doc;
            const auto error = deserializeJson(doc,static_cast<const uint8_t *>(payload),length,
                                               DeserializationOption::NestingLimit(8));
            if (error) { Serial.printf("BTC WebSocket JSON: %s\n",error.c_str()); return; }
            const char *channel = doc["channel"] | "";
            if (strcmp(channel,"ticker") == 0) {
                BtcSample next;
                if (!parseKrakenTicker(doc.as<JsonVariantConst>(),millis(),time(nullptr),next)) {
                    xSemaphoreTake(snapshotMutex,portMAX_DELAY);
                    snapshot.btc.requestOk = false;
                    xSemaphoreGive(snapshotMutex);
                    Serial.println("BTC ticker invalid/stale");
                    return;
                }
                xSemaphoreTake(snapshotMutex,portMAX_DELAY);
                snapshot.btc = next;
                xSemaphoreGive(snapshotMutex);
                btcUpdates.fetch_add(1);
                reconnectMs = kBtcPollMs;
                socket.setReconnectInterval(reconnectMs);
                if (uint32_t(millis()-lastLog) >= 15000 || btcUpdates.load() == 1) {
                    Serial.printf("BTC LIVE: %.2f USD, %+.2f%%; updates=%lu\n",next.priceUSD,next.change24h,(unsigned long)btcUpdates.load());
                    lastLog = millis();
                }
            } else if (doc["method"] == "subscribe" && doc["success"] == false) {
                Serial.println("BTC subscription rejected; backing off");
                socket.disconnect();
            }
        }
    });
    while (true) {
        btcHeartbeat.store(millis());
        const bool connected = networkConnected();
        if (!connected && wasConnected) {
            socket.disconnect();
            btcSocketConnected.store(false);
            xSemaphoreTake(snapshotMutex,portMAX_DELAY);
            snapshot.btc.requestOk = false;
            xSemaphoreGive(snapshotMutex);
        }
        wasConnected = connected;
        if (connected && time(nullptr) >= 1700000000) {
            if (!initialized) {
                // WebSockets 2.7.3 leaves client-cert/key pointers uninitialized
                // in beginSslWithCA. Its explicit API initializes both safely.
                socket.beginSslWithClientKey("ws.kraken.com",443,"/v2",kPublicApiRoots,nullptr,nullptr,"");
                socket.setReconnectInterval(kBtcPollMs);
                socket.enableHeartbeat(15000,5000,2);
                initialized = true;
            }
            socket.loop();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void pollChain() {
    WiFiClientSecure client;
    client.setCACert(kPublicApiRoots);
    client.setHandshakeTimeout(8);
    StaticJsonDocument<64> heightFilter;
    heightFilter.set(true);
    StaticJsonDocument<64> heightDoc;
    uint32_t height = 0;
    const bool heightOk = getJson(client,kHeightUrl,heightDoc,heightFilter) &&
                          parseBlockHeight(heightDoc.as<JsonVariantConst>(),height);
    xSemaphoreTake(snapshotMutex,portMAX_DELAY);
    if (heightOk) { snapshot.chain.blockHeight = height; snapshot.chain.heightAt = millis(); snapshot.chain.heightValid = true; }
    snapshot.chain.heightOk = heightOk;
    xSemaphoreGive(snapshotMutex);

    StaticJsonDocument<64> feeFilter;
    feeFilter["hourFee"] = true;
    StaticJsonDocument<128> feeDoc;
    double fee = 0;
    const bool feeOk = getJson(client,kFeesUrl,feeDoc,feeFilter) && parseHourFee(feeDoc.as<JsonVariantConst>(),fee);
    xSemaphoreTake(snapshotMutex,portMAX_DELAY);
    if (feeOk) { snapshot.chain.hourFee = fee; snapshot.chain.feeAt = millis(); snapshot.chain.feeValid = true; }
    snapshot.chain.feeOk = feeOk;
    xSemaphoreGive(snapshotMutex);
    if (heightOk || feeOk) Serial.printf("CHAIN: height=%lu fee1h=%.0f sat/vB (OK=%d/%d)\n",(unsigned long)height,fee,int(heightOk),int(feeOk));
}

void chainTask(void *) {
    uint32_t requestedAt = 0;
    bool due = true, wasConnected = false;
    while (true) {
        chainHeartbeat.store(millis());
        const bool connected = networkConnected();
        if (connected && !wasConnected) due = true;
        wasConnected = connected;
        if (connected && time(nullptr) >= 1700000000 &&
            (due || uint32_t(millis()-requestedAt) >= kChainPollMs)) {
            pollChain(); due = false; requestedAt = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void connectSaved() {
    const String ssid = wifiPreferences.getString("ssid", "");
    const String password = wifiPreferences.getString("password", "");
    hasCredentials = !ssid.isEmpty();
    if (!hasCredentials) return;
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.printf("Wi-Fi connection requested (saved credentials; status=%d)\n", int(WiFi.status()));
    reconnectAt = millis();
}
}  // namespace

void networkOpenSetup() {
    if (setupActive) return;
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(kSetupName, kSetupPassword)) {
        Serial.println("Could not start Wi-Fi setup AP");
        return;
    }
    char token[33];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random());
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
            Serial.printf("Wi-Fi disconnected: reason=%u\n", unsigned(info.wifi_sta_disconnected.reason));
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
            portal.send(400, "text/plain; charset=utf-8", "Проверьте имя сети и пароль (8–63 символа или пустой).");
            return;
        }
        pendingSsid = ssid;
        pendingPassword = password;
        pendingConnect = true;
        closeSetupWhenConnected = true;
        portal.send(200, "text/html; charset=utf-8",
                    "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<h2>Настройки приняты</h2><p>CYD подключается к Wi-Fi. Следите за экраном. "
                    "После подключения вернитесь в домашнюю сеть. Если данные не появятся, "
                    "снова откройте 192.168.4.1 и проверьте пароль.</p>");
    });
    portal.onNotFound([]() { portal.sendHeader("Location", "/"); portal.send(302); });
    connectSaved();
    if (!hasCredentials) networkOpenSetup();
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
    if (!snapshotMutex) { Serial.println("ERROR: cannot create data mutex"); return; }
    if (xTaskCreate(pollingTask,"lofi-miner",12288,nullptr,1,nullptr) != pdPASS) Serial.println("ERROR: miner task");
    if (xTaskCreate(btcTask,"lofi-btc",16384,nullptr,1,nullptr) != pdPASS) Serial.println("ERROR: BTC task");
    if (xTaskCreate(chainTask,"lofi-chain",12288,nullptr,1,nullptr) != pdPASS) Serial.println("ERROR: chain task");
}

void networkLoop() {
    if (setupActive) portal.handleClient();
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
    if (setupActive && closeSetupWhenConnected && connected && uint32_t(now - connectedAt) > 10000) {
        portal.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        setupActive = false;
    }
    previouslyConnected = connected;
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
bool networkSetupActive() { return setupActive; }
bool networkConnected() { return WiFi.status() == WL_CONNECTED; }
void networkPrintStatus() {
    const auto data = networkSnapshot();
    Serial.printf("NET: wifi=%d reason=%u credentials=%d setup=%d IP=%s gateway=%s RSSI=%d\n",
                  int(WiFi.status()), unsigned(disconnectReason.load()), int(hasCredentials), int(setupActive),
                  WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
    Serial.printf("DATA: minerHTTP=%d BTCsocket=%d BTCupdates=%lu heightHTTP=%d feeHTTP=%d NTP=%lld heap=%u\n",
                  lastMinerHttp.load(),int(btcSocketConnected.load()),(unsigned long)btcUpdates.load(),
                  lastHeightHttp.load(),lastFeesHttp.load(),(long long)time(nullptr),ESP.getFreeHeap());
    Serial.printf("TASK ages: miner=%lu BTC=%lu chain=%lu ms\n",
                  (unsigned long)(millis()-pollingHeartbeat.load()),(unsigned long)(millis()-btcHeartbeat.load()),
                  (unsigned long)(millis()-chainHeartbeat.load()));
    Serial.printf("DETAILS: ASIC=%.0f C VRM=%.0f C FAN=%.0f rpm FREQ=%.0f MHz CORE=%.3f V uptime=%lu mask=%u\n",
                  data.miner.temperatureC,data.miner.vrmTemperatureC,data.miner.fanRPM,
                  data.miner.frequencyMHz,data.miner.coreVoltageV,(unsigned long)data.miner.uptimeSeconds,
                  unsigned(data.miner.detailsMask));
    Serial.printf("QUOTE: %.2f USD %+.2f%% LOW=%.2f HIGH=%.2f fresh=%d\n",
                  data.btc.priceUSD,data.btc.change24h,data.btc.lowUSD,data.btc.highUSD,
                  int(btcFresh(data.btc,millis(),networkConnected())));
}
void networkStatusText(char *out, size_t size) {
    const auto reason = disconnectReason.load();
    if (networkConnected()) {
        const int status = lastMinerHttp.load();
        if (status == 200) snprintf(out, size, "MINER: BAD DATA");
        else if (status) snprintf(out, size, "MINER HTTP %d", status);
        else snprintf(out, size, "MINER: WAIT");
    } else if (reason == 201) snprintf(out, size, "WIFI: NO AP (201)");
    else if (reason == 202 || reason == 15 || reason == 204) snprintf(out, size, "WIFI: AUTH (%u)", unsigned(reason));
    else if (reason) snprintf(out, size, "WIFI: ERROR %u", unsigned(reason));
    else snprintf(out, size, "WIFI: CONNECTING");
}
