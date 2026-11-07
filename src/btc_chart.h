#pragma once
#include "btc_history.h"

constexpr int kChartX = 25, kChartY = 46, kChartW = 137, kChartH = 66;
struct BtcChart {
    int16_t y[kChartW];
    uint16_t color = 0x07FD;
    bool valid = false, fresh = false;
    BtcChart() {
        for (auto& point : y)
            point = -1;
    }
};
inline BtcChart makeBtcChart(const BtcHistory& history, uint32_t now, int64_t epoch,
                             bool connected) {
    BtcChart plot;
    if (!history.valid || history.count < 2 || epoch < 1700000000)
        return plot;
    const int64_t from = epoch - 86400;
    double low = 1e30, high = 0;
    int first = -1, last = -1;
    for (uint16_t i = 0; i < history.count; ++i) {
        const auto& p = history.points[i];
        if (p.epoch < from || p.epoch > epoch || !std::isfinite(p.price) || p.price <= 0)
            continue;
        if (first < 0)
            first = i;
        last = i;
        low = std::fmin(low, p.price);
        high = std::fmax(high, p.price);
    }
    if (first < 0 || first == last)
        return plot;
    const double padding = std::fmax((high - low) * 0.07, std::fmax(high * 0.00001, 0.01));
    low -= padding;
    high += padding;
    auto row = [&](double price) {
        return int(std::lround((high - price) * (kChartH - 1) / (high - low)));
    };
    for (int i = first; i <= last; ++i) {
        const auto& p = history.points[i];
        const int x = int((int64_t(p.epoch) - from) * (kChartW - 1) / 86400);
        if (x >= 0 && x < kChartW)
            plot.y[x] = row(p.price);
        if (i == first)
            continue;
        const auto& prev = history.points[i - 1];
        if (p.epoch <= prev.epoch || p.epoch - prev.epoch > 1800 || prev.epoch < from)
            continue;
        const int x0 = int((int64_t(prev.epoch) - from) * (kChartW - 1) / 86400);
        const int y0 = row(prev.price), y1 = row(p.price);
        for (int at = x0; at <= x && at < kChartW; ++at)
            if (at >= 0)
                plot.y[at] = x == x0 ? y1 : y0 + (y1 - y0) * (at - x0) / (x - x0);
    }
    plot.valid = true;
    plot.fresh = historyFresh(history, now, connected);
    plot.color = !plot.fresh
                     ? 0x8C71
                     : (history.points[last].price < history.points[first].price ? 0xFB6D : 0x07FD);
    return plot;
}

inline bool btcChartPixel(const BtcChart& plot, int x, int y, uint16_t& color) {
    if (!plot.valid)
        return false;
    if (plot.y[x] >= 0) {
        const int previous = x > 0 && plot.y[x - 1] >= 0 ? plot.y[x - 1] : plot.y[x];
        const int bottom = previous > plot.y[x] ? previous : plot.y[x];
        const int top = previous < plot.y[x] ? previous : plot.y[x];
        if (y >= top && y <= bottom) {
            color = plot.color;
            return true;
        }
    }
    if (y == kChartH - 1 || y == kChartH / 2 || x == kChartW / 2) {
        color = 0x1189;
        return true;
    }
    return false;
}
