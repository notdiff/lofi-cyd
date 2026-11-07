#include "network_internal.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <memory>

namespace network_detail {
class BoundedBody : public Stream {
  public:
    std::unique_ptr<char[]> data;
    size_t capacity;
    size_t length = 0;
    explicit BoundedBody(size_t maximum)
        : data(new(std::nothrow) char[maximum + 1]), capacity(maximum) {}
    size_t write(uint8_t byte) override {
        return write(&byte, 1);
    }
    size_t write(const uint8_t* buffer, size_t size) override {
        if (!data || size > capacity - length)
            return 0;
        memcpy(data.get() + length, buffer, size);
        length += size;
        data[length] = '\0';
        return size;
    }
    int available() override {
        return 0;
    }
    int read() override {
        return -1;
    }
    int peek() override {
        return -1;
    }
    void flush() override {}
};

bool getJson(WiFiClient& client, const char* url, JsonDocument& doc, JsonDocument& filter,
             size_t limit) {
    HTTPClient http;
    http.setConnectTimeout(config::kHttpConnectTimeoutMs);
    http.setTimeout(config::kHttpReadTimeoutMs);
    http.setReuse(false);
    http.setUserAgent(String("Lofi-Miner/") + config::kVersion);
    if (!http.begin(client, url))
        return false;
    const int status = http.GET();
    (strcmp(url, kMinerUrl) == 0
         ? lastMinerHttp
         : (strcmp(url, kHeightUrl) == 0
                ? lastHeightHttp
                : (strcmp(url, kFeesUrl) == 0
                       ? lastFeesHttp
                       : (strcmp(url, kDifficultyUrl) == 0 ? lastDifficultyHttp
                                                           : lastHistoryHttp))))
        .store(status);
    bool ok = false;
    if (status == HTTP_CODE_OK && http.getSize() <= int(limit)) {
        BoundedBody body(limit);
        const int result = http.writeToStream(&body);
        if (result >= 0 && body.length > 0) {

            const auto error = deserializeJson(doc, static_cast<const char*>(body.data.get()),
                                               body.length, DeserializationOption::Filter(filter),
                                               DeserializationOption::NestingLimit(8));
            ok = !error;
            if (error)
                Serial.printf("JSON decode error: %s (%u bytes)\n", error.c_str(),
                              unsigned(body.length));
        } else
            Serial.printf("HTTP body read failed: %d (%u bytes)\n", result, unsigned(body.length));
    }
    if (!ok)
        Serial.printf(
            "%s request failed (HTTP %d or invalid JSON)\n",
            strcmp(url, kMinerUrl) == 0
                ? "Miner"
                : (strcmp(url, kDifficultyUrl) == 0
                       ? "Difficulty"
                       : (strcmp(url, kHeightUrl) == 0 || strcmp(url, kFeesUrl) == 0 ? "Chain"
                                                                                     : "History")),
            status);
    http.end();
    return ok;
}

}
