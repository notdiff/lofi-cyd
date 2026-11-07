#pragma once
#include "metrics.h"

constexpr uint16_t kHistoryCapacity = 98;
constexpr uint32_t kHistoryPollMs = 300000, kHistoryStaleMs = 900000;
struct HistoryPoint {
    uint32_t epoch;
    float price;
    HistoryPoint(uint32_t at = 0, float value = 0) : epoch(at), price(value) {}
};
struct BtcHistory {
    HistoryPoint points[kHistoryCapacity] = {};
    uint16_t count = 0;
    uint32_t currentBucket = 0, receivedAt = 0;
    bool valid = false, requestOk = false;
};
inline bool historyFresh(const BtcHistory& history, uint32_t now, bool connected) {
    return connected && history.valid && history.requestOk &&
           uint32_t(now - history.receivedAt) < kHistoryStaleMs;
}
inline void updateHistoryQuote(BtcHistory& history, double price, uint32_t epoch) {
    if (!history.valid || !history.count || !std::isfinite(price) || price <= 0 ||
        epoch < history.points[history.count - 1].epoch)
        return;
    const uint32_t bucket = epoch / 900 * 900;
    if (bucket < history.currentBucket || bucket - history.currentBucket > 900)
        return;
    if (bucket != history.currentBucket) {
        if (history.count == kHistoryCapacity) {
            memmove(history.points, history.points + 1,
                    (kHistoryCapacity - 1) * sizeof(HistoryPoint));
            --history.count;
        }
        ++history.count;
        history.currentBucket = bucket;
    }
    history.points[history.count - 1] = {epoch, float(price)};
}
