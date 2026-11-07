#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr uint32_t kMinerPollMs = 5000;
constexpr uint32_t kMinerStaleMs = 15000;
constexpr uint32_t kBtcPollMs = 15000;
constexpr uint32_t kBtcStaleMs = 60000;
constexpr uint32_t kChainPollMs = 60000;
constexpr uint32_t kChainStaleMs = 180000;

enum MinerExtra : uint8_t {
    InputVoltage,
    InputCurrent,
    FanPercent,
    CoreSet,
    MinerHeap,
    AsicDetected,
    AsicCount,
    SmallCores,
    Overheat,
    Fallback,
    AutoFan,
    UpdateAllowed,
    ExtraCount
};
struct MinerSample {
    double hashRateGH = 0;
    double temperatureC = 0;
    double powerW = 0;
    uint64_t sharesAccepted = 0;
    uint32_t receivedAt = 0;
    bool valid = false;
    bool requestOk = false;
    double vrmTemperatureC = 0;
    double fanRPM = 0;
    double frequencyMHz = 0;
    double coreVoltageV = 0;
    uint32_t uptimeSeconds = 0;
    uint64_t sharesRejected = 0;
    uint8_t detailsMask = 0;
    double extra[ExtraCount] = {};
    uint16_t extraMask = 0;
    char bestDiff[16] = "--", bestSessionDiff[16] = "--";
    bool bestValid = false, sessionValid = false;
    bool wifiKnown = false, wifiConnected = false;
    bool bm1366 = false;
};
struct BtcSample {
    double priceUSD = 0;
    double change24h = 0;
    uint32_t sourceAgeMs = 0;
    uint32_t receivedAt = 0;
    bool valid = false;
    bool requestOk = false;
    double lowUSD = 0;
    double highUSD = 0;
    bool rangeValid = false;
};
struct ChainSample {
    uint32_t blockHeight = 0;
    double hourFee = 0;
    uint32_t heightAt = 0, feeAt = 0;
    bool heightValid = false, feeValid = false;
    bool heightOk = false, feeOk = false;
    double difficulty = 0;
    uint32_t difficultyAt = 0;
    bool difficultyValid = false, difficultyOk = false;
};
struct LiveSnapshot {
    MinerSample miner;
    BtcSample btc;
    ChainSample chain;
};
inline bool minerFresh(const MinerSample& sample, uint32_t now, bool connected) {
    return connected && sample.valid && sample.requestOk &&
           uint32_t(now - sample.receivedAt) < kMinerStaleMs;
}
inline bool chainHeightFresh(const ChainSample& sample, uint32_t now, bool connected) {
    return connected && sample.heightValid && sample.heightOk &&
           uint32_t(now - sample.heightAt) < kChainStaleMs;
}
inline bool chainFeeFresh(const ChainSample& sample, uint32_t now, bool connected) {
    return connected && sample.feeValid && sample.feeOk &&
           uint32_t(now - sample.feeAt) < kChainStaleMs;
}
inline bool btcFresh(const BtcSample& sample, uint32_t now, bool connected) {
    const uint32_t elapsed = now - sample.receivedAt;
    return connected && sample.valid && sample.requestOk && elapsed < kBtcStaleMs &&
           sample.sourceAgeMs < kBtcStaleMs - elapsed;
}
inline void formatPrice(char* out, size_t size, double price) {
    if (!std::isfinite(price) || price < 0 || price > 1e12) {
        snprintf(out, size, "--");
        return;
    }
    char raw[24];
    snprintf(raw, sizeof(raw), "%.0f", price);
    const size_t length = strlen(raw);
    size_t pos = 0;
    if (size > 1)
        out[pos++] = '$';
    for (size_t i = 0; i < length && pos + 1 < size; ++i) {
        if (i && (length - i) % 3 == 0 && pos + 1 < size)
            out[pos++] = ',';
        if (pos + 1 < size)
            out[pos++] = raw[i];
    }
    if (size)
        out[pos] = '\0';
}
inline void formatHashRate(char* out, size_t size, double gh) {
    if (gh >= 1e6)
        snprintf(out, size, "%.2f PH/s", gh / 1e6);
    else if (gh >= 1000)
        snprintf(out, size, "%.2f TH/s", gh / 1000);
    else
        snprintf(out, size, "%.1f GH/s", gh);
}
inline void formatCount(char* out, size_t size, uint64_t value) {
    if (value < 1000000ULL) {
        snprintf(out, size, "%llu", (unsigned long long)value);
        return;
    }
    const char* units[] = {"M", "B", "T", "P", "E"};
    double scaled = double(value) / 1e6;
    int unit = 0;
    while (scaled >= 999.995 && unit < 4) {
        scaled /= 1000;
        ++unit;
    }
    snprintf(out, size, "%.2f%s", scaled, units[unit]);
}
inline void formatShortPrice(char* out, size_t size, double price) {
    if (price < 999999.5)
        formatPrice(out, size, price);
    else {
        const double scale = price >= 1e12 ? 1e12 : (price >= 1e9 ? 1e9 : 1e6);
        snprintf(out, size, "$%.2f%s", price / scale,
                 price >= 1e12 ? "T" : (price >= 1e9 ? "B" : "M"));
    }
}
inline void formatUptime(char* out, size_t size, uint64_t seconds) {
    if (seconds >= 86400)
        snprintf(out, size, "%llud%02lluh", (unsigned long long)(seconds / 86400),
                 (unsigned long long)(seconds / 3600 % 24));
    else if (seconds >= 3600)
        snprintf(out, size, "%lluh%02llum", (unsigned long long)(seconds / 3600),
                 (unsigned long long)(seconds / 60 % 60));
    else
        snprintf(out, size, "%llum%02llus", (unsigned long long)(seconds / 60),
                 (unsigned long long)(seconds % 60));
}

constexpr uint32_t kDifficultyPollMs = 600000, kDifficultyStaleMs = 1800000;
inline bool difficultyFresh(const ChainSample& sample, uint32_t now, bool connected) {
    return connected && sample.difficultyValid && sample.difficultyOk &&
           uint32_t(now - sample.difficultyAt) < kDifficultyStaleMs;
}

inline double blockProbability10m(double hashRateGH, double difficulty) {
    if (!std::isfinite(hashRateGH) || hashRateGH < 0 || !std::isfinite(difficulty) ||
        difficulty <= 0)
        return NAN;
    const double lambda = hashRateGH * 1e9 * 600 / (difficulty * (281474976710656.0 / 65535.0));
    return -std::expm1(-lambda);
}
inline void formatProbability(char* out, size_t size, double probability) {
    if (!std::isfinite(probability) || probability < 0 || probability > 1)
        snprintf(out, size, "P10m --");
    else if (probability > 0 && probability * 100 < 0.0000005)
        snprintf(out, size, "P10m %.1e%%", probability * 100);
    else
        snprintf(out, size, "P10m %.6f%%", probability * 100);
}
