#pragma once
#include "btc_history.h"
#include "metrics.h"
inline LiveSnapshot demoSnapshot(uint32_t now) {
    LiveSnapshot data;
    data.miner.hashRateGH = 1103.4144;
    data.miner.temperatureC = 58;
    data.miner.powerW = 23.8935;
    data.miner.sharesAccepted = 23;
    data.miner.receivedAt = now;
    data.miner.valid = true;
    data.miner.requestOk = true;
    data.miner.vrmTemperatureC = 61;
    data.miner.fanRPM = 3568;
    data.miner.frequencyMHz = 550;
    data.miner.coreVoltageV = 1.229;
    data.miner.uptimeSeconds = 883;
    data.miner.detailsMask = 63;
    data.miner.bm1366 = true;
    data.btc.priceUSD = 96420;
    data.btc.change24h = 2.4;
    data.btc.receivedAt = now;
    data.btc.valid = true;
    data.btc.requestOk = true;
    data.btc.lowUSD = 94180;
    data.btc.highUSD = 97030;
    data.btc.rangeValid = true;
    data.chain.blockHeight = 960123;
    data.chain.hourFee = 3;
    data.chain.heightAt = now;
    data.chain.feeAt = now;
    data.chain.heightValid = true;
    data.chain.feeValid = true;
    data.chain.heightOk = true;
    data.chain.feeOk = true;
    data.miner.extraMask = (1u << ExtraCount) - 1;
    const double extras[] = {12000, 17406.25, 66, 1250, 8411212, 2, 2, 894, 0, 0, 1, 0};
    for (int i = 0; i < ExtraCount; ++i)
        data.miner.extra[i] = extras[i];
    snprintf(data.miner.bestDiff, sizeof(data.miner.bestDiff), "8.84M");
    snprintf(data.miner.bestSessionDiff, sizeof(data.miner.bestSessionDiff), "95.1k");
    data.miner.bestValid = data.miner.sessionValid = true;
    data.miner.wifiKnown = data.miner.wifiConnected = true;
    data.chain.difficulty = 132716002350731.3;
    data.chain.difficultyAt = now;
    data.chain.difficultyOk = data.chain.difficultyValid = true;
    return data;
}
inline BtcHistory demoHistory(uint32_t now, int64_t epoch, double lastPrice = 96420) {
    BtcHistory history;
    for (int i = 0; i <= 96; ++i)
        history.points[history.count++] = {uint32_t(epoch - 86400 + i * 900),
                                           float(lastPrice + 650 * std::sin(i * 0.14) + i * 8 -
                                                 650 * std::sin(96 * 0.14) - 96 * 8)};
    history.currentBucket = uint32_t(epoch) / 900 * 900;
    history.receivedAt = now;
    history.valid = history.requestOk = true;
    return history;
}
