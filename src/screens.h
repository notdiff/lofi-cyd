#pragma once
#include "btc_chart.h"
#include "overview.h"
#include "screen_frame.h"
#include <ctime>

inline void minerDetailValue(const MinerSample& m, int item, uint32_t now, bool fresh, char* text,
                             size_t size) {
    snprintf(text, size, "--");
    if (!m.valid)
        return;
    switch (item) {
    case 0:
        formatHashRate(text, size, m.hashRateGH);
        break;
    case 1:
        snprintf(text, size, "%.1f W", m.powerW);
        break;
    case 2:
        snprintf(text, size, "%.0f C", m.temperatureC);
        break;
    case 3:
        if (m.detailsMask & 1)
            snprintf(text, size, "%.0f C", m.vrmTemperatureC);
        break;
    case 4:
        if (m.detailsMask & 2)
            snprintf(text, size, "%.0f rpm", m.fanRPM);
        break;
    case 5:
        if (m.extraMask & (1u << FanPercent))
            snprintf(text, size, "%.0f%%", m.extra[FanPercent]);
        break;
    case 6:
        if (m.detailsMask & 8)
            snprintf(text, size, "%.3f V", m.coreVoltageV);
        break;
    case 7:
        if (m.extraMask & (1u << CoreSet))
            snprintf(text, size, "%.3f V", m.extra[CoreSet] / 1000);
        break;
    case 8:
        if (m.detailsMask & 4)
            snprintf(text, size, "%.0f MHz", m.frequencyMHz);
        break;
    case 9:
        if (m.detailsMask & 16)
            formatUptime(text, size,
                         uint64_t(m.uptimeSeconds) +
                             (fresh ? uint32_t(now - m.receivedAt) / 1000 : 0));
        break;
    case 10:
        formatCount(text, size, m.sharesAccepted);
        break;
    case 11:
        if (m.detailsMask & 32)
            formatCount(text, size, m.sharesRejected);
        break;
    case 12:
        if (m.bestValid)
            snprintf(text, size, "%s", m.bestDiff);
        break;
    case 13:
        if (m.sessionValid)
            snprintf(text, size, "%s", m.bestSessionDiff);
        break;
    case 14:
        if (m.extraMask & (1u << InputVoltage))
            snprintf(text, size, "%.0f", m.extra[InputVoltage]);
        break;
    case 15:
        if (m.extraMask & (1u << InputCurrent))
            snprintf(text, size, "%.0f", m.extra[InputCurrent]);
        break;
    case 16:
        if (m.extraMask & (1u << MinerHeap))
            snprintf(text, size, "%.2f MB", m.extra[MinerHeap] / 1e6);
        break;
    case 17:
        if ((m.extraMask & ((1u << AsicDetected) | (1u << AsicCount))) ==
            ((1u << AsicDetected) | (1u << AsicCount)))
            snprintf(text, size, "%.0f / %.0f", m.extra[AsicDetected], m.extra[AsicCount]);
        break;
    case 18:
        if (m.extraMask & (1u << SmallCores))
            formatCount(text, size, uint64_t(m.extra[SmallCores]));
        break;
    case 19:
        if (m.extraMask & (1u << Overheat))
            snprintf(text, size, "%s", m.extra[Overheat] ? "YES" : "NO");
        break;
    case 20:
        if (m.extraMask & (1u << Fallback))
            snprintf(text, size, "%s", m.extra[Fallback] ? "FALLBACK" : "PRIMARY");
        break;
    case 21:
        if (m.extraMask & (1u << AutoFan))
            snprintf(text, size, "%s", m.extra[AutoFan] ? "AUTO" : "MANUAL");
        break;
    }
}
inline ScreenFrame makeSetupFrame() {
    ScreenFrame frame;
    screenField(frame, 25, 32, 137, 14, 10, kScreenCyan, "WI-FI SETUP");
    screenField(frame, 25, 49, 137, 18, 13, kScreenWhite, "Lofi-Miner-Setup");
    screenField(frame, 25, 68, 137, 17, 13, kScreenWhite, "PASS: lofiminer");
    screenField(frame, 25, 87, 137, 20, 13, kScreenCyan, "192.168.4.1");
    return frame;
}
inline ScreenFrame makeScreenFrame(Page page, const LiveSnapshot& data, uint32_t now, int64_t epoch,
                                   bool connected, bool demo = false,
                                   const BtcHistory* history = nullptr) {
    ScreenFrame frame;
    char text[48] = {};
    if (page == Page::Clock) {
        const bool valid = epoch >= 1700000000;
        screenField(frame, 40, 31, 164, 13, 10, valid ? kScreenCyan : kScreenMuted,
                    demo ? "MSK / DEMO" : (valid ? "MSK" : "MSK / NTP WAIT"), true);

        const time_t moscow = time_t(epoch + 10800);
        tm date = {};
        if (valid)
            gmtime_r(&moscow, &date);
        if (valid)
            snprintf(text, sizeof(text), "%02d:%02d", date.tm_hour, date.tm_min);
        else
            snprintf(text, sizeof(text), "--:--");
        screenField(frame, 37, 44, 169, 55, 48, valid ? kScreenWhite : kScreenMuted, text, true);
        if (valid)
            snprintf(text, sizeof(text), "%02d.%02d.%04d", date.tm_mday, date.tm_mon + 1,
                     date.tm_year + 1900);
        else
            snprintf(text, sizeof(text), "WAITING FOR TIME");
        screenField(frame, 40, 101, 164, 18, 14, valid ? kScreenCyan : kScreenMuted, text, true);
    } else if (page == Page::Overview) {
        const auto overview = overviewText(data, now, connected);
        screenField(frame, 25, 32, 137, 14, 10, overview.btcFresh ? kScreenWhite : kScreenMuted,
                    demo ? "BTC/USD DEMO" : overview.heading);
        screenField(frame, 25, 47, 137, 38, 30, overview.btcFresh ? kScreenWhite : kScreenMuted,
                    overview.price);
        screenField(frame, 25, 87, 137, 20, 13,
                    overview.btcFresh ? (overview.negativeChange ? kScreenRed : kScreenCyan)
                                      : kScreenMuted,
                    overview.change);
        const uint16_t color = overview.minerFresh ? kScreenWhite : kScreenMuted;
        screenField(frame, 26, 151, 137, 17, 14, color, overview.hashRate);
        screenField(frame, 26, 172, 137, 15, 14, color, overview.temperaturePower);
        screenField(frame, 26, 191, 137, 15, 13,
                    overview.probabilityFresh ? kScreenCyan : kScreenMuted, overview.probability);
    } else if (page == Page::Details) {
        const auto& miner = data.miner;
        const bool fresh = minerFresh(miner, now, connected);
        const uint16_t color = fresh ? kScreenCyan : kScreenMuted;

        screenField(frame, 25, 33, 102, 14, 13, fresh ? kScreenWhite : kScreenMuted,
                    demo
                        ? "LV07 / DEMO"
                        : (miner.valid ? (fresh ? "LV07 / LIVE" : "LV07 / STALE") : "LV07 / WAIT"));
        screenField(frame, 133, 33, 65, 14, 10, fresh ? kScreenWhite : kScreenMuted,
                    miner.valid && miner.wifiKnown ? (miner.wifiConnected ? "WIFI OK" : "WIFI DOWN")
                                                   : "WIFI --");
        snprintf(text, sizeof(text), "UPDATE %s",
                 miner.valid && (miner.extraMask & (1u << UpdateAllowed))
                     ? (miner.extra[UpdateAllowed] ? "ON" : "OFF")
                     : "--");
        screenField(frame, 213, 33, 85, 14, 10, fresh ? kScreenWhite : kScreenMuted, text);
        const char* labels[] = {"HASH",  "POWER",   "ASIC",  "VRM",    "FAN",    "FAN %",
                                "CORE",  "SET",     "FREQ",  "UPTIME", "SHARES", "REJECT",
                                "BEST",  "SESSION", "V raw", "I raw",  "HEAP",   "ASICs",
                                "CORES", "HOT",     "STRAT", "FANCTL"};
        for (int i = 0; i < 22; ++i) {
            const int x = i < 11 ? 25 : 167, y = 48 + (i % 11) * 13;
            screenField(frame, x, y, 50, 13, 10, fresh ? kScreenWhite : kScreenMuted, labels[i]);
            minerDetailValue(miner, i, now, fresh, text, sizeof(text));
            const uint16_t valueColor = i == 19 && miner.valid &&
                                                (miner.extraMask & (1u << Overheat)) &&
                                                miner.extra[Overheat] && fresh
                                            ? kScreenRed
                                            : color;
            screenField(frame, x + 52, y, 79, 13, 13, valueColor, text);
        }
        screenButton(frame, kSettingsButton, "Settings", fresh && !demo);
        screenButton(frame, kRestartButton, "Restart", fresh && !demo);
    } else {
        const bool fresh = btcFresh(data.btc, now, connected);
        const uint16_t color = fresh ? kScreenWhite : kScreenMuted;
        const bool graphFresh = history && historyFresh(*history, now, connected);
        const auto chart = history ? makeBtcChart(*history, now, epoch, connected) : BtcChart{};
        screenField(frame, 25, 29, 137, 14, 10, graphFresh ? kScreenWhite : kScreenMuted,
                    demo ? "BTC 24h / DEMO"
                         : (chart.valid ? (graphFresh ? "BTC 24h / LIVE" : "BTC 24h / STALE")
                                        : "BTC 24h / WAIT"));
        if (!chart.valid)
            screenField(frame, 27, 71, 132, 15, 10, kScreenMuted, "HISTORY WAIT", true);
        screenField(frame, 25, 117, 73, 13, 10, kScreenMuted, "24h ago");
        screenField(frame, 125, 117, 37, 13, 10, kScreenMuted, "now");
        if (data.btc.valid)
            formatShortPrice(text, sizeof(text), data.btc.priceUSD);
        else
            snprintf(text, sizeof(text), "--");
        screenField(frame, 25, 151, 76, 14, 13, color, text);
        if (data.btc.valid)
            snprintf(text, sizeof(text), "%+.2f%%", data.btc.change24h);
        else
            snprintf(text, sizeof(text), "--%%");
        screenField(frame, 103, 151, 59, 14, 10,
                    fresh ? (data.btc.change24h < 0 ? kScreenRed : kScreenCyan) : kScreenMuted,
                    text);
        for (int i = 0; i < 2; ++i) {
            char price[24] = "--";
            if (data.btc.valid && data.btc.rangeValid)
                formatShortPrice(price, sizeof(price), i == 0 ? data.btc.lowUSD : data.btc.highUSD);
            snprintf(text, sizeof(text), "%s %s", i == 0 ? "LO" : "HI", price);
            screenField(frame, i == 0 ? 25 : 96, 167, 66, 13, 10,
                        fresh ? kScreenCyan : kScreenMuted, text);
        }
        const bool heightFresh = chainHeightFresh(data.chain, now, connected);
        const bool feeFresh = chainFeeFresh(data.chain, now, connected);
        if (data.chain.heightValid)
            snprintf(text, sizeof(text), "BLOCK %lu", (unsigned long)data.chain.blockHeight);
        else
            snprintf(text, sizeof(text), "BLOCK --");
        screenField(frame, 25, 183, 137, 13, 10, heightFresh ? kScreenWhite : kScreenMuted, text);
        if (data.chain.feeValid) {
            char fee[16];
            formatCount(fee, sizeof(fee), uint64_t(std::lround(data.chain.hourFee)));
            snprintf(text, sizeof(text), "FEE ~1h %s sat/vB", fee);
        } else
            snprintf(text, sizeof(text), "FEE ~1h -- sat/vB");
        screenField(frame, 25, 200, 137, 13, 10, feeFresh ? kScreenCyan : kScreenMuted, text);
    }
    return frame;
}
