#pragma once
#include <ctime>
#include "overview.h"

constexpr uint16_t kScreenWhite = 0xEFFF, kScreenCyan = 0x07FD, kScreenMuted = 0x8C71, kScreenRed = 0xFB6D;
enum class Page : uint8_t { Clock, Overview, Details, Bitcoin };
struct ScreenField {
    int x, y, w, h;
    uint8_t fontSize;
    uint16_t color;
    bool centered;
    char text[48];
};
struct ScreenFrame {
    ScreenField fields[16] = {};
    uint8_t count = 0;
};
inline void screenField(ScreenFrame &frame, int x, int y, int w, int h, uint8_t font,
                        uint16_t color, const char *text, bool centered = false) {
    auto &field = frame.fields[frame.count++];
    field.x = x; field.y = y; field.w = w; field.h = h;
    field.fontSize = font; field.color = color; field.centered = centered;
    snprintf(field.text, sizeof(field.text), "%s", text);
}
inline LiveSnapshot demoSnapshot(uint32_t now) {
    LiveSnapshot data;
    data.miner.hashRateGH = 1103.4144; data.miner.temperatureC = 58;
    data.miner.powerW = 23.8935; data.miner.sharesAccepted = 23;
    data.miner.receivedAt = now; data.miner.valid = true; data.miner.requestOk = true;
    data.miner.vrmTemperatureC = 61;
    data.miner.fanRPM = 3568;
    data.miner.frequencyMHz = 550;
    data.miner.coreVoltageV = 1.229;
    data.miner.uptimeSeconds = 883;
    data.miner.detailsMask = 63;
    data.btc.priceUSD = 96420; data.btc.change24h = 2.4;
    data.btc.receivedAt = now; data.btc.valid = true; data.btc.requestOk = true;
    data.btc.lowUSD = 94180; data.btc.highUSD = 97030; data.btc.rangeValid = true;
    data.chain.blockHeight = 960123; data.chain.hourFee = 3;
    data.chain.heightAt = now; data.chain.feeAt = now;
    data.chain.heightValid = true; data.chain.feeValid = true;
    data.chain.heightOk = true; data.chain.feeOk = true;
    return data;
}
inline ScreenFrame makeSetupFrame() {
    ScreenFrame frame;
    screenField(frame,25,32,137,14,10,kScreenCyan,"WI-FI SETUP");
    screenField(frame,25,49,137,18,13,kScreenWhite,"Lofi-Miner-Setup");
    screenField(frame,25,68,137,17,13,kScreenWhite,"PASS: lofiminer");
    screenField(frame,25,87,137,20,13,kScreenCyan,"192.168.4.1");
    return frame;
}
inline ScreenFrame makeScreenFrame(Page page, const LiveSnapshot &data, uint32_t now,
                                   int64_t epoch, bool connected, bool demo = false) {
    ScreenFrame frame;
    char text[48] = {};
    if (page == Page::Clock) {
        const bool valid = epoch >= 1700000000;
        screenField(frame,40,31,164,13,10,valid ? kScreenCyan : kScreenMuted,
                    demo ? "MSK / DEMO" : (valid ? "MSK" : "MSK / NTP WAIT"),true);
        // Moscow is UTC+3 without DST. NTP/price timestamp checks remain in UTC.
        const time_t moscow = time_t(epoch + 10800);
        tm date = {};
        if (valid) gmtime_r(&moscow,&date);
        if (valid) snprintf(text,sizeof(text),"%02d:%02d",date.tm_hour,date.tm_min);
        else snprintf(text,sizeof(text),"--:--");
        screenField(frame,37,44,169,55,48,valid ? kScreenWhite : kScreenMuted,text,true);
        if (valid) snprintf(text,sizeof(text),"%02d.%02d.%04d",date.tm_mday,date.tm_mon+1,date.tm_year+1900);
        else snprintf(text,sizeof(text),"WAITING FOR TIME");
        screenField(frame,40,101,164,18,14,valid ? kScreenCyan : kScreenMuted,text,true);
    } else if (page == Page::Overview) {
        const auto overview = overviewText(data,now,connected);
        screenField(frame,25,32,137,14,10,overview.btcFresh ? kScreenWhite : kScreenMuted,
                    demo ? "BTC/USD DEMO" : overview.heading);
        screenField(frame,25,47,137,38,30,overview.btcFresh ? kScreenWhite : kScreenMuted,overview.price);
        screenField(frame,25,87,137,20,13,overview.btcFresh ? (overview.negativeChange ? kScreenRed : kScreenCyan) : kScreenMuted,overview.change);
        const uint16_t color = overview.minerFresh ? kScreenWhite : kScreenMuted;
        screenField(frame,26,151,137,17,14,color,overview.hashRate);
        screenField(frame,26,172,137,15,14,color,overview.temperaturePower);
        screenField(frame,26,191,137,15,14,color,overview.shares);
    } else if (page == Page::Details) {
        const auto &miner = data.miner;
        const bool fresh = minerFresh(miner,now,connected);
        const uint16_t color = fresh ? kScreenCyan : kScreenMuted;
        screenField(frame,26,32,185,17,13,fresh ? kScreenWhite : kScreenMuted,
                    demo ? "LV07 / DEMO" : (miner.valid ? (fresh ? "LV07 / LIVE" : "LV07 / STALE") : "LV07 / WAIT"),true);
        const char *labels[] = {"ASIC","VRM","FAN","FREQ","CORE","UPTIME"};
        const int xs[] = {29,126,29,126,29,126}, ys[] = {58,58,97,97,135,135};
        for (int i = 0; i < 6; ++i) {
            screenField(frame,xs[i],ys[i],84,13,10,fresh ? kScreenWhite : kScreenMuted,labels[i]);
            snprintf(text,sizeof(text),"--");
            if (miner.valid) {
                if (i == 0) snprintf(text,sizeof(text),"%.0f C",miner.temperatureC);
                if (i == 1 && (miner.detailsMask & 1)) snprintf(text,sizeof(text),"%.0f C",miner.vrmTemperatureC);
                if (i == 2 && (miner.detailsMask & 2)) snprintf(text,sizeof(text),"%.0f rpm",miner.fanRPM);
                if (i == 3 && (miner.detailsMask & 4)) snprintf(text,sizeof(text),"%.0f MHz",miner.frequencyMHz);
                if (i == 4 && (miner.detailsMask & 8)) snprintf(text,sizeof(text),"%.2f V",miner.coreVoltageV);
                if (i == 5 && (miner.detailsMask & 16))
                    formatUptime(text,sizeof(text),uint64_t(miner.uptimeSeconds) + (fresh ? uint32_t(now-miner.receivedAt)/1000 : 0));
            }
            screenField(frame,xs[i],ys[i]+14,84,17,14,color,text);
        }
        char accepted[16] = "--", rejected[16] = "--";
        if (miner.valid) formatCount(accepted,sizeof(accepted),miner.sharesAccepted);
        if (miner.valid && (miner.detailsMask & 32)) formatCount(rejected,sizeof(rejected),miner.sharesRejected);
        snprintf(text,sizeof(text),"%s OK / %s REJ%s",accepted,rejected,miner.valid && !fresh ? " *" : "");
        screenField(frame,28,177,182,15,13,color,text,true);
    } else {
        const bool fresh = btcFresh(data.btc,now,connected);
        const uint16_t color = fresh ? kScreenWhite : kScreenMuted;
        screenField(frame,25,29,137,14,10,color,demo ? "BTC/USD DEMO" :
                    (data.btc.valid ? (fresh ? "BTC/USD LIVE" : "BTC/USD STALE") : "BTC/USD WAIT"));
        if (data.btc.valid) formatPrice(text,sizeof(text),data.btc.priceUSD);
        else snprintf(text,sizeof(text),"--");
        screenField(frame,25,43,137,34,30,color,text);
        if (data.btc.valid) snprintf(text,sizeof(text),"%+.2f%% / 24h",data.btc.change24h);
        else snprintf(text,sizeof(text),"-- / 24h");
        screenField(frame,25,79,137,19,13,fresh ? (data.btc.change24h < 0 ? kScreenRed : kScreenCyan) : kScreenMuted,text);
        for (int i = 0; i < 2; ++i) {
            const int x = i == 0 ? 25 : 100;
            screenField(frame,x,105,62,12,10,color,i == 0 ? "LOW 24h" : "HIGH 24h");
            if (data.btc.valid && data.btc.rangeValid) formatShortPrice(text,sizeof(text),i == 0 ? data.btc.lowUSD : data.btc.highUSD);
            else snprintf(text,sizeof(text),"--");
            screenField(frame,x,118,62,13,10,fresh ? kScreenCyan : kScreenMuted,text);
        }
        const bool heightFresh = chainHeightFresh(data.chain,now,connected);
        const bool feeFresh = chainFeeFresh(data.chain,now,connected);
        screenField(frame,25,153,137,13,10,heightFresh ? kScreenWhite : kScreenMuted,
                    demo ? "BLOCK / DEMO" : (data.chain.heightValid && !heightFresh ? "BLOCK / STALE" : "BLOCK HEIGHT"));
        if (data.chain.heightValid) snprintf(text,sizeof(text),"%lu",(unsigned long)data.chain.blockHeight);
        else snprintf(text,sizeof(text),"--");
        screenField(frame,25,167,137,15,14,heightFresh ? kScreenCyan : kScreenMuted,text);
        screenField(frame,25,186,137,12,10,feeFresh ? kScreenWhite : kScreenMuted,
                    demo ? "FEE ~1h / DEMO" : (data.chain.feeValid && !feeFresh ? "FEE ~1h / STALE" : "FEE ~1h"));
        if (data.chain.feeValid) snprintf(text,sizeof(text),"%.0f sat/vB",data.chain.hourFee);
        else snprintf(text,sizeof(text),"-- sat/vB");
        screenField(frame,25,199,137,14,13,feeFresh ? kScreenCyan : kScreenMuted,text);
    }
    return frame;
}
