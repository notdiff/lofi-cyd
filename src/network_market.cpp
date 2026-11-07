#include "generated/tls_roots.h"
#include "metrics_json.h"
#include "network_internal.h"
#include <WebSocketsClient.h>
#include <WiFiClientSecure.h>

namespace network_detail {
void btcTask(void*) {
    WebSocketsClient socket;
    uint32_t reconnectMs = kBtcPollMs;
    uint32_t lastLog = 0;
    bool initialized = false, wasConnected = false;
    socket.onEvent([&](WStype_t type, uint8_t* payload, size_t length) {
        if (type == WStype_CONNECTED) {
            btcSocketConnected.store(true);
            socket.sendTXT(
                R"({"method":"subscribe","params":{"channel":"ticker","symbol":["BTC/USD"],"event_trigger":"trades","snapshot":true},"req_id":1})");
            Serial.println("BTC WebSocket connected; subscribing to Kraken BTC/USD");
        } else if (type == WStype_DISCONNECTED || type == WStype_ERROR) {
            btcSocketConnected.store(false);
            xSemaphoreTake(snapshotMutex, portMAX_DELAY);
            snapshot.btc.requestOk = false;
            xSemaphoreGive(snapshotMutex);
            reconnectMs = min(reconnectMs * 2, uint32_t(300000));
            socket.setReconnectInterval(reconnectMs);
            Serial.printf("BTC WebSocket disconnected; retry interval %lu sec\n",
                          (unsigned long)(reconnectMs / 1000));
        } else if (type == WStype_TEXT && length > 0 && length <= 4096) {
            StaticJsonDocument<2048> doc;
            const auto error = deserializeJson(doc, static_cast<const uint8_t*>(payload), length,
                                               DeserializationOption::NestingLimit(8));
            if (error) {
                Serial.printf("BTC WebSocket JSON: %s\n", error.c_str());
                return;
            }
            const char* channel = doc["channel"] | "";
            if (strcmp(channel, "ticker") == 0) {
                BtcSample next;
                if (!parseKrakenTicker(doc.as<JsonVariantConst>(), millis(), time(nullptr), next)) {
                    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
                    snapshot.btc.requestOk = false;
                    xSemaphoreGive(snapshotMutex);
                    Serial.println("BTC ticker invalid/stale");
                    return;
                }
                xSemaphoreTake(snapshotMutex, portMAX_DELAY);
                snapshot.btc = next;
                updateHistoryQuote(history, next.priceUSD,
                                   uint32_t(time(nullptr)) - next.sourceAgeMs / 1000);
                xSemaphoreGive(snapshotMutex);
                btcUpdates.fetch_add(1);
                reconnectMs = kBtcPollMs;
                socket.setReconnectInterval(reconnectMs);
                if (uint32_t(millis() - lastLog) >= 15000 || btcUpdates.load() == 1) {
                    Serial.printf("BTC LIVE: %.2f USD, %+.2f%%; updates=%lu\n", next.priceUSD,
                                  next.change24h, (unsigned long)btcUpdates.load());
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
            xSemaphoreTake(snapshotMutex, portMAX_DELAY);
            snapshot.btc.requestOk = false;
            xSemaphoreGive(snapshotMutex);
        }
        wasConnected = connected;
        if (connected && time(nullptr) >= 1700000000) {
            if (!initialized) {

                socket.beginSslWithClientKey("ws.kraken.com", 443, "/v2", kPublicApiRoots, nullptr,
                                             nullptr, "");
                socket.setReconnectInterval(kBtcPollMs);
                socket.enableHeartbeat(15000, 5000, 2);
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
    client.setHandshakeTimeout(config::kTlsHandshakeTimeoutSeconds);
    StaticJsonDocument<64> heightFilter;
    heightFilter.set(true);
    StaticJsonDocument<64> heightDoc;
    uint32_t height = 0;
    const bool heightOk = getJson(client, kHeightUrl, heightDoc, heightFilter) &&
                          parseBlockHeight(heightDoc.as<JsonVariantConst>(), height);
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    if (heightOk) {
        snapshot.chain.blockHeight = height;
        snapshot.chain.heightAt = millis();
        snapshot.chain.heightValid = true;
    }
    snapshot.chain.heightOk = heightOk;
    xSemaphoreGive(snapshotMutex);

    StaticJsonDocument<64> feeFilter;
    feeFilter["hourFee"] = true;
    StaticJsonDocument<128> feeDoc;
    double fee = 0;
    const bool feeOk = getJson(client, kFeesUrl, feeDoc, feeFilter) &&
                       parseHourFee(feeDoc.as<JsonVariantConst>(), fee);
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    if (feeOk) {
        snapshot.chain.hourFee = fee;
        snapshot.chain.feeAt = millis();
        snapshot.chain.feeValid = true;
    }
    snapshot.chain.feeOk = feeOk;
    xSemaphoreGive(snapshotMutex);
    if (heightOk || feeOk)
        Serial.printf("CHAIN: height=%lu fee1h=%.0f sat/vB (OK=%d/%d)\n", (unsigned long)height,
                      fee, int(heightOk), int(feeOk));
}

bool pollDifficulty() {
    WiFiClientSecure client;
    client.setCACert(kPublicApiRoots);
    client.setHandshakeTimeout(config::kTlsHandshakeTimeoutSeconds);
    StaticJsonDocument<64> filter;
    filter["currentDifficulty"] = true;
    StaticJsonDocument<128> doc;
    double difficulty = 0;
    const bool ok = getJson(client, kDifficultyUrl, doc, filter) &&
                    parseDifficulty(doc.as<JsonVariantConst>(), difficulty);
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    if (ok) {
        snapshot.chain.difficulty = difficulty;
        snapshot.chain.difficultyAt = millis();
        snapshot.chain.difficultyValid = true;
    }
    snapshot.chain.difficultyOk = ok;
    xSemaphoreGive(snapshotMutex);
    Serial.printf("DIFFICULTY: %.0f OK=%d\n", difficulty, int(ok));
    return ok;
}
bool pollHistory() {
    const int64_t epoch = time(nullptr);
    char url[128];
    snprintf(url, sizeof(url),
             "https://api.kraken.com/0/public/OHLC?pair=XBTUSD&interval=15&since=%lld",
             (long long)(epoch - 87300));
    WiFiClientSecure client;
    client.setCACert(kPublicApiRoots);
    client.setHandshakeTimeout(config::kTlsHandshakeTimeoutSeconds);
    StaticJsonDocument<256> filter;
    filter["error"] = true;
    filter["result"]["XXBTZUSD"] = true;
    DynamicJsonDocument doc(24576);
    BtcHistory next;
    const bool ok = getJson(client, url, doc, filter, 16384) &&
                    parseKrakenHistory(doc.as<JsonVariantConst>(), millis(), time(nullptr), next);
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    if (ok) {
        history = next;
        if (btcFresh(snapshot.btc, millis(), networkConnected()))
            updateHistoryQuote(history, snapshot.btc.priceUSD,
                               uint32_t(time(nullptr)) - snapshot.btc.sourceAgeMs / 1000 -
                                   uint32_t(millis() - snapshot.btc.receivedAt) / 1000);
    } else
        history.requestOk = false;
    xSemaphoreGive(snapshotMutex);
    Serial.printf(
        "HISTORY: points=%u span=%lu sec OK=%d heap=%u\n", unsigned(next.count),
        (unsigned long)(next.count ? next.points[next.count - 1].epoch - next.points[0].epoch : 0),
        int(ok), ESP.getFreeHeap());
    return ok;
}
void chainTask(void*) {
    uint32_t requestedAt = 0;
    uint32_t difficultyAt = 0, historyAt = 0, historyWait = 60000, difficultyWait = 60000;
    bool difficultyDue = true, historyDue = true;
    bool due = true, wasConnected = false;
    while (true) {
        chainHeartbeat.store(millis());
        const bool connected = networkConnected();
        if (connected && !wasConnected)
            due = difficultyDue = historyDue = true;
        wasConnected = connected;
        if (connected && time(nullptr) >= 1700000000 &&
            (due || uint32_t(millis() - requestedAt) >= kChainPollMs)) {
            pollChain();
            due = false;
            requestedAt = millis();
        }
        if (connected && time(nullptr) >= 1700000000) {
            if (difficultyDue || uint32_t(millis() - difficultyAt) >= difficultyWait) {
                difficultyWait = pollDifficulty() ? kDifficultyPollMs : 60000;
                difficultyAt = millis();
                difficultyDue = false;
            }
            if (historyDue || uint32_t(millis() - historyAt) >= historyWait) {
                historyWait =
                    pollHistory() ? kHistoryPollMs : min(historyWait * 2, uint32_t(300000));
                historyAt = millis();
                historyDue = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

}
