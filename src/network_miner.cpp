#include "metrics_json.h"
#include "network_internal.h"
#include <HTTPClient.h>

namespace network_detail {
void pollMiner() {
    WiFiClient client;
    StaticJsonDocument<1024> filter;
    for (const char* field : {"hashRate",        "temp",           "power",
                              "sharesAccepted",  "vrTemp",         "fanrpm",
                              "fanSpeed",        "frequency",      "coreVoltageActual",
                              "uptimeSeconds",   "sharesRejected", "voltage",
                              "current",         "fanspeed",       "coreVoltage",
                              "freeHeap",        "asicDetected",   "asicCount",
                              "smallCoreCount",  "overheat_mode",  "isUsingFallbackStratum",
                              "autofanspeed",    "allowUpdate",    "bestDiff",
                              "bestSessionDiff", "wifiStatus",     "ASICModel"})
        filter[field] = true;
    StaticJsonDocument<2048> doc;
    MinerSample sample;
    const bool ok = getJson(client, kMinerUrl, doc, filter) &&
                    parseMiner(doc.as<JsonVariantConst>(), millis(), sample);
    if (ok)
        Serial.printf("Miner OK: %.2f GH/s, %.0f C, %.1f W, %llu shares; details=%u\n",
                      sample.hashRateGH, sample.temperatureC, sample.powerW,
                      (unsigned long long)sample.sharesAccepted, unsigned(sample.detailsMask));
    else if (lastMinerHttp.load() == 200)
        Serial.println("Miner: required fields invalid or missing");
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    if (ok)
        snapshot.miner = sample;
    else
        snapshot.miner.requestOk = false;
    xSemaphoreGive(snapshotMutex);
}

bool executeMinerCommand() {
    MinerCommand command;
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    const bool queued = controlMailbox.take(command);
    xSemaphoreGive(snapshotMutex);
    if (!queued)
        return false;

    int code = 0;
    char body[128];
    if (networkConnected() && minerCommandBody(command, body, sizeof(body))) {
        WiFiClient client;
        HTTPClient http;
        http.setConnectTimeout(config::kHttpConnectTimeoutMs);
        http.setTimeout(config::kHttpReadTimeoutMs);
        http.setReuse(false);
        if (http.begin(client, minerCommandUrl(command))) {
            http.addHeader("Content-Type", "application/json");
            code = http.sendRequest(minerCommandMethod(command), reinterpret_cast<uint8_t*>(body),
                                    strlen(body));
            http.end();
        }
    }
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    controlMailbox.finish(code);
    if (command.restart)
        snapshot.miner.requestOk = false;
    xSemaphoreGive(snapshotMutex);
    Serial.printf("MINER CONTROL: %s HTTP=%d (no automatic retry)\n",
                  command.restart ? "restart" : "save", code);
    return true;
}

void pollingTask(void*) {
    uint32_t minerAt = 0;
    bool due = true, wasConnected = false;
    while (true) {
        pollingHeartbeat.store(millis());
        const bool connected = networkConnected();
        if (connected && !wasConnected)
            due = true;
        wasConnected = connected;
        if (executeMinerCommand())
            due = true;
        xSemaphoreTake(snapshotMutex, portMAX_DELAY);
        const bool restarting = controlMailbox.command.restart && controlMailbox.report.id &&
                                uint32_t(millis() - controlMailbox.submittedAt) < 5000;
        xSemaphoreGive(snapshotMutex);
        if (connected && !restarting && (due || uint32_t(millis() - minerAt) >= kMinerPollMs)) {
            pollMiner();
            due = false;
            minerAt = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

}
