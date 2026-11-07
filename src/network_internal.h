#pragma once
#include "config.h"
#include "network.h"
#include <ArduinoJson.h>
#include <atomic>
#include <freertos/semphr.h>

class WiFiClient;

namespace network_detail {
using config::kDifficultyUrl;
using config::kFeesUrl;
using config::kHeightUrl;
using config::kMinerUrl;

extern SemaphoreHandle_t snapshotMutex;
extern LiveSnapshot snapshot;
extern BtcHistory history;
extern ControlMailbox controlMailbox;
extern std::atomic<uint32_t> pollingHeartbeat, btcHeartbeat, btcUpdates, chainHeartbeat;
extern std::atomic<int> lastMinerHttp, lastHeightHttp, lastFeesHttp, lastDifficultyHttp,
    lastHistoryHttp;
extern std::atomic<bool> btcSocketConnected;

bool getJson(WiFiClient& client, const char* url, JsonDocument& document, JsonDocument& filter,
             size_t limit = 4096);
void pollingTask(void*);
void btcTask(void*);
void chainTask(void*);
}
