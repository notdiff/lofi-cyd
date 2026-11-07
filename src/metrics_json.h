#pragma once
#include "btc_history.h"
#include "metrics.h"
#include <ArduinoJson.h>
#include <cstdlib>
#include <ctime>

inline bool jsonNumber(JsonVariantConst value, double& out, double min, double max) {
    if (!value.is<double>())
        return false;
    out = value.as<double>();
    return std::isfinite(out) && out >= min && out <= max;
}
inline bool difficultyText(JsonVariantConst value, char* out, size_t size) {
    if (!value.is<const char*>())
        return false;
    const char* text = value.as<const char*>();
    const size_t length = strlen(text);
    if (!length || length >= size)
        return false;
    bool digit = false;
    for (size_t i = 0; i < length; ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9')
            digit = true;
        else if (c != '.' && !(i == length - 1 && strchr("kKMGBTPE", c)))
            return false;
    }
    if (!digit)
        return false;
    if (length > 10) {
        char* end = nullptr;
        const double number = strtod(text, &end);
        if (!std::isfinite(number) || number < 0)
            return false;
        snprintf(out, size, "%.3g%s", number, end);
    } else
        snprintf(out, size, "%s", text);
    return true;
}
inline bool parseMiner(JsonVariantConst root, uint32_t now, MinerSample& out) {
    MinerSample next;
    if (!root.is<JsonObjectConst>() || !jsonNumber(root["hashRate"], next.hashRateGH, 0, 1e9) ||
        !jsonNumber(root["temp"], next.temperatureC, -40, 200) ||
        !jsonNumber(root["power"], next.powerW, 0, 100000) ||
        !root["sharesAccepted"].is<uint64_t>())
        return false;
    next.sharesAccepted = root["sharesAccepted"].as<uint64_t>();
    next.bm1366 = root["ASICModel"].is<const char*>() && strcmp(root["ASICModel"], "BM1366") == 0;
    if (jsonNumber(root["vrTemp"], next.vrmTemperatureC, -40, 200))
        next.detailsMask |= 1;
    if (jsonNumber(root["fanrpm"], next.fanRPM, 0, 100000) ||
        jsonNumber(root["fanSpeed"], next.fanRPM, 0, 100000))
        next.detailsMask |= 2;
    if (jsonNumber(root["frequency"], next.frequencyMHz, 0, 10000))
        next.detailsMask |= 4;
    double millivolts;
    if (jsonNumber(root["coreVoltageActual"], millivolts, 0, 5000)) {
        next.coreVoltageV = millivolts / 1000;
        next.detailsMask |= 8;
    }
    if (root["uptimeSeconds"].is<uint32_t>()) {
        next.uptimeSeconds = root["uptimeSeconds"];
        next.detailsMask |= 16;
    }
    if (root["sharesRejected"].is<uint64_t>()) {
        next.sharesRejected = root["sharesRejected"];
        next.detailsMask |= 32;
    }
    const char* keys[] = {
        "voltage",      "current",    "fanspeed",       "coreVoltage",   "freeHeap",
        "asicDetected", "asicCount",  "smallCoreCount", "overheat_mode", "isUsingFallbackStratum",
        "autofanspeed", "allowUpdate"};
    const double maxima[] = {1e9, 1e9, 100, 5000, 1e10, 10000, 10000, 1e9, 1, 1, 1, 1};
    for (uint8_t i = 0; i < ExtraCount; ++i) {
        if (jsonNumber(root[keys[i]], next.extra[i], 0, maxima[i]))
            next.extraMask |= 1u << i;
        else if (i >= Overheat && root[keys[i]].is<bool>()) {
            next.extra[i] = root[keys[i]].as<bool>() ? 1 : 0;
            next.extraMask |= 1u << i;
        }
    }
    if (root["wifiStatus"].is<const char*>()) {
        const char* status = root["wifiStatus"];
        next.wifiKnown = true;
        next.wifiConnected = strcmp(status, "Connected!") == 0 || strcmp(status, "Connected") == 0;
    }
    next.bestValid = difficultyText(root["bestDiff"], next.bestDiff, sizeof(next.bestDiff));
    next.sessionValid =
        difficultyText(root["bestSessionDiff"], next.bestSessionDiff, sizeof(next.bestSessionDiff));
    next.receivedAt = now;
    next.valid = next.requestOk = true;
    out = next;
    return true;
}

inline bool parseUtcTimestamp(const char* text, int64_t& epoch) {
    if (!text)
        return false;
    const size_t length = strlen(text);
    if (length != 20) {
        if (length < 22 || length > 30 || text[19] != '.' || text[length - 1] != 'Z')
            return false;
        for (size_t i = 20; i < length - 1; ++i)
            if (text[i] < '0' || text[i] > '9')
                return false;
    }
    int y, m, d, h, minute, sec;
    char end;
    if (sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d%c", &y, &m, &d, &h, &minute, &sec, &end) != 7 ||
        (end != 'Z' && end != '.') || y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || h < 0 ||
        h > 23 || minute < 0 || minute > 59 || sec < 0 || sec > 59)
        return false;
    const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int limit = lengths[m - 1] + (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    if (d > limit)
        return false;
    y -= m <= 2;
    const int era = y / 400;
    const unsigned year = y - era * 400;
    const unsigned day = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned days = year * 365 + year / 4 - year / 100 + day;
    epoch = (int64_t(era) * 146097 + days - 719468) * 86400 + h * 3600 + minute * 60 + sec;
    return true;
}
inline bool parseKrakenTicker(JsonVariantConst root, uint32_t now, int64_t epochNow,
                              BtcSample& out) {
    if (!root["channel"].is<const char*>() || strcmp(root["channel"], "ticker") != 0 ||
        !root["type"].is<const char*>() ||
        (strcmp(root["type"], "snapshot") != 0 && strcmp(root["type"], "update") != 0))
        return false;
    const auto quote = root["data"][0];
    BtcSample next;
    int64_t sourceTime;
    if (!quote["symbol"].is<const char*>() || strcmp(quote["symbol"], "BTC/USD") != 0 ||
        !jsonNumber(quote["last"], next.priceUSD, 0.01, 1e12) ||
        !jsonNumber(quote["change_pct"], next.change24h, -100, 1e6) ||
        !jsonNumber(quote["low"], next.lowUSD, 0.01, 1e12) ||
        !jsonNumber(quote["high"], next.highUSD, next.lowUSD, 1e12) ||
        !parseUtcTimestamp(quote["timestamp"], sourceTime) || epochNow < 1700000000 ||
        sourceTime > epochNow + 120 || epochNow - sourceTime >= 60)
        return false;
    next.sourceAgeMs = uint32_t(epochNow > sourceTime ? epochNow - sourceTime : 0) * 1000;
    next.receivedAt = now;
    next.rangeValid = next.valid = next.requestOk = true;
    out = next;
    return true;
}
inline bool parseBlockHeight(JsonVariantConst root, uint32_t& height) {
    if (!root.is<uint32_t>() || root.as<uint32_t>() == 0 || root.as<uint32_t>() > 1000000000)
        return false;
    height = root.as<uint32_t>();
    return true;
}
inline bool parseHourFee(JsonVariantConst root, double& fee) {
    return jsonNumber(root["hourFee"], fee, 0, 1000000);
}
inline bool parseDifficulty(JsonVariantConst root, double& difficulty) {
    return jsonNumber(root["currentDifficulty"], difficulty, 1, 1e30);
}
inline bool parseKrakenHistory(JsonVariantConst root, uint32_t now, int64_t epoch,
                               BtcHistory& out) {
    if (epoch < 1700000000 || !root["error"].is<JsonArrayConst>() || root["error"].size() != 0)
        return false;
    const auto rows = root["result"]["XXBTZUSD"].as<JsonArrayConst>();
    if (rows.size() < 2 || rows.size() > 720)
        return false;
    BtcHistory next;
    uint32_t previous = 0;
    for (const auto row : rows) {
        if (!row.is<JsonArrayConst>() || row.size() != 8 || !row[0].is<uint32_t>() ||
            !row[4].is<const char*>())
            return false;
        const uint32_t bucket = row[0].as<uint32_t>();
        if ((previous && bucket <= previous) || bucket % 900 != 0 || bucket > epoch)
            return false;
        previous = bucket;
        const char* text = row[4];
        char* end = nullptr;
        const double price = strtod(text, &end);
        if (end == text || *end || !std::isfinite(price) || price <= 0 || price > 1e12)
            return false;
        const uint32_t pointAt = uint32_t(std::fmin(double(bucket) + 900, double(epoch)));
        if (pointAt < epoch - 86400)
            continue;
        if (next.count == kHistoryCapacity)
            return false;
        next.points[next.count++] = {pointAt, float(price)};
        next.currentBucket = bucket;
    }
    if (next.count < 2 || epoch - next.points[next.count - 1].epoch > 1800)
        return false;
    next.receivedAt = now;
    next.valid = next.requestOk = true;
    out = next;
    return true;
}
