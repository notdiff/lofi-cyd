#pragma once
#include "metrics.h"

struct OverviewText {
    char heading[24];
    char price[32];
    char change[32];
    char hashRate[32];
    char temperaturePower[40];
    char probability[40];
    bool probabilityFresh = false;
    bool minerFresh = false;
    bool btcFresh = false;
    bool negativeChange = false;
};
inline OverviewText overviewText(const LiveSnapshot& data, uint32_t now, bool connected) {
    OverviewText text = {};
    text.minerFresh = minerFresh(data.miner, now, connected);
    text.btcFresh = btcFresh(data.btc, now, connected);
    snprintf(text.heading, sizeof(text.heading), "BTC/USD  %s",
             data.btc.valid ? (text.btcFresh ? "LIVE" : "STALE")
                            : (connected ? "WAIT" : "NO WIFI"));
    if (data.btc.valid) {
        formatPrice(text.price, sizeof(text.price), data.btc.priceUSD);
        snprintf(text.change, sizeof(text.change), "%+.2f%% / 24h", data.btc.change24h);
        text.negativeChange = data.btc.change24h < 0;
    } else {
        snprintf(text.price, sizeof(text.price), "--");
        snprintf(text.change, sizeof(text.change), "-- / 24h");
    }
    if (data.miner.valid) {
        formatHashRate(text.hashRate, sizeof(text.hashRate), data.miner.hashRateGH);
        snprintf(text.temperaturePower, sizeof(text.temperaturePower), "%.0f C  %.1f W",
                 data.miner.temperatureC, data.miner.powerW);

    } else {
        snprintf(text.hashRate, sizeof(text.hashRate), "-- TH/s");
        snprintf(text.temperaturePower, sizeof(text.temperaturePower), "-- C  -- W");
    }
    text.probabilityFresh = text.minerFresh && difficultyFresh(data.chain, now, connected);
    formatProbability(text.probability, sizeof(text.probability),
                      data.miner.valid && data.chain.difficultyValid
                          ? blockProbability10m(data.miner.hashRateGH, data.chain.difficulty)
                          : NAN);
    return text;
}
