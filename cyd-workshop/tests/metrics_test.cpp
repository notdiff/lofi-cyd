#include <cassert>
#include <fstream>
#include <sstream>
#include <iostream>
#include "metrics_json.h"
#include "screens.h"
#include "display_text.h"

static JsonVariantConst decode(const char *json, JsonDocument &doc) {
    assert(!deserializeJson(doc, json));
    return doc.as<JsonVariantConst>();
}
static int fontSize(const BitmapFont *font) {
    return font == &kFont48 ? 48 : (font == &kFont30 ? 30 : (font == &kFont14 ? 14 : (font == &kFont13 ? 13 : 10)));
}
static void checkLayout(const OverviewText &text) {
    assert(fittedFont(text.heading, 137, 14, kFont10));
    assert(fittedFont(text.price, 137, 38, kFont30));
    assert(fittedFont(text.change, 137, 20, kFont13));
    assert(fittedFont(text.hashRate, 137, 17, kFont14));
    assert(fittedFont(text.temperaturePower, 137, 15, kFont14));
    assert(fittedFont(text.shares, 137, 15, kFont14));
}
static LiveSnapshot normalData() {
    LiveSnapshot data = demoSnapshot(1000);
    data.miner.hashRateGH = 1103.41440805813;
    data.btc.priceUSD = 86169.25574705143;
    data.btc.change24h = 1.15;
    data.btc.lowUSD = 85110.1; data.btc.highUSD = 86683.4;
    return data;
}
static void checkScreen(const ScreenFrame &frame) {
    assert(frame.count <= 16);
    for (int i = 0; i < frame.count; ++i) {
        const auto &field = frame.fields[i];
        assert(field.x >= 0 && field.y >= 0 && field.x+field.w <= 320 && field.y+field.h <= 240);
        const auto *font = fittedFont(field.text,field.w,field.h,bitmapFontForSize(field.fontSize));
        assert(font);
        int advance = field.centered ? (field.w-bitmapTextWidth(field.text,*font))/2 : 0;
        for (const char *c = field.text; *c; ++c) {
            assert(*c >= 32 && *c <= 126);
            const auto &glyph = font->glyphs[*c-32];
            for (int row = 0; row < font->height; ++row)
                for (int col = 0; col < glyph.width; ++col) {
                    const uint32_t bit = row*glyph.width+col;
                    if (pgm_read_byte(font->bits+glyph.offset+bit/8) & (0x80 >> (bit%8)))
                        assert(advance+col < field.w && row < field.h);
                }
            advance += glyph.advance;
        }
        for (int j = i+1; j < frame.count; ++j) {
            const auto &other = frame.fields[j];
            assert(field.x+field.w <= other.x || other.x+other.w <= field.x ||
                   field.y+field.h <= other.y || other.y+other.h <= field.y);
        }
    }
}
static void emitScreen(const char *name, int page, const ScreenFrame &frame) {
    checkScreen(frame);
    std::cout << "{\"name\":\"" << name << "\",\"page\":" << page << ",\"fields\":[";
    for (int i = 0; i < frame.count; ++i) {
        const auto &f = frame.fields[i];
        const auto *font = fittedFont(f.text,f.w,f.h,bitmapFontForSize(f.fontSize));
        if (i) std::cout << ',';
        std::cout << "{\"text\":\"" << f.text << "\",\"x\":" << f.x+(f.centered ? (f.w-bitmapTextWidth(f.text,*font))/2 : 0)
                  << ",\"y\":" << f.y << ",\"font\":" << fontSize(font) << ",\"color\":" << f.color << '}';
    }
    std::cout << "]}";
}
static void emitPreview(const char *name, const OverviewText &text) {
    checkLayout(text);
    std::cout << "{\"name\":\"" << name << "\",\"minerFresh\":" << text.minerFresh
              << ",\"btcFresh\":" << text.btcFresh << ",\"negative\":" << text.negativeChange << ",\"fields\":[";
    const char *values[] = {text.heading,text.price,text.change,text.hashRate,text.temperaturePower,text.shares};
    const int x[] = {25,25,25,26,26,26}, y[] = {32,47,87,151,172,191};
    const int heights[] = {14,38,20,17,15,15};
    const BitmapFont *preferred[] = {&kFont10,&kFont30,&kFont13,&kFont14,&kFont14,&kFont14};
    for (int i = 0; i < 6; ++i) {
        if (i) std::cout << ',';
        std::cout << "{\"text\":\"" << values[i] << "\",\"x\":" << x[i] << ",\"y\":" << y[i]
                  << ",\"font\":" << fontSize(fittedFont(values[i],137,heights[i],*preferred[i])) << '}';
    }
    std::cout << "]}";
}
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--preview-pages") == 0) {
        std::cout << '[';
        bool first = true;
        for (int page = 0; page < 4; ++page) {
            for (int state = 0; state < 3; ++state) {
                if (!first) std::cout << ',';
                first = false;
                const char *names[] = {"live","stale","waiting"};
                emitScreen(names[state],page,makeScreenFrame(Page(page),state == 2 ? LiveSnapshot{} : normalData(),
                           state == 1 ? 950000 : 2000,state == 2 ? 0 : 1791244800,state != 1));
            }
        }
        std::cout << ','; emitScreen("setup",1,makeSetupFrame());
        std::cout << "]\n";
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--preview") == 0) {
        auto data = normalData();
        std::cout << '[';
        emitPreview("live", overviewText(data, 2000, true));
        std::cout << ',';
        emitPreview("stale", overviewText(data, 950000, false));
        std::cout << ',';
        emitPreview("waiting", overviewText({}, 1000, true));
        std::cout << ',';
        data.btc.priceUSD = 9999999;
        data.btc.change24h = -99.99;
        data.miner = {1e9,200,100000,UINT64_MAX,1000,true,true};
        emitPreview("large-values", overviewText(data, 2000, true));
        std::cout << "]\n";
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--check-btc") == 0) {
        std::ifstream file(argv[2]);
        std::stringstream contents;
        contents << file.rdbuf();
        DynamicJsonDocument doc(4096);
        assert(!deserializeJson(doc, contents.str()));
        BtcSample quote;
        int64_t sourceTime;
        assert(parseUtcTimestamp(doc["data"][0]["timestamp"],sourceTime));
        assert(parseKrakenTicker(doc.as<JsonVariantConst>(),1000,sourceTime+1,quote));
        assert(quote.priceUSD > 0);
        std::cout << "Captured Kraken WebSocket response parsed successfully\n";
        return 0;
    }

    StaticJsonDocument<2048> doc;
    MinerSample miner;
    assert(parseMiner(decode(R"({"hashRate":1103.41440805813,"temp":58,"power":23.8935546875,"sharesAccepted":23})",doc),1000,miner));
    assert(miner.valid && miner.sharesAccepted == 23 && std::abs(miner.powerW - 23.8935546875) < 1e-9);
    assert(minerFresh(miner,14999,true));
    assert(!minerFresh(miner,16000,true));
    assert(!minerFresh(miner,1001,false));
    miner.receivedAt = UINT32_MAX - 100;
    assert(minerFresh(miner,100,true)); // millis rollover
    miner.requestOk = false;
    assert(!minerFresh(miner,100,true));
    for (const char *bad : {
        R"({"hashRate":1100,"temp":58,"power":24})",
        R"({"hashRate":"1100","temp":58,"power":24,"sharesAccepted":23})",
        R"({"hashRate":1100,"temp":null,"power":24,"sharesAccepted":23})",
        R"({"hashRate":-1,"temp":58,"power":24,"sharesAccepted":23})",
        R"({"hashRate":1100,"temp":58,"power":24,"sharesAccepted":-1})",
        R"({"hashRate":1100,"temp":58,"power":24,"sharesAccepted":1.5})",
        R"({"hashRate":1100,"temp":58,"power":true,"sharesAccepted":23})", "[]", "null"}) {
        const auto before = miner;
        assert(!parseMiner(decode(bad,doc),1000,miner));
        assert(miner.sharesAccepted == before.sharesAccepted && miner.receivedAt == before.receivedAt);
    }
    assert(deserializeJson(doc,"{broken"));
    assert(parseMiner(decode(R"({"hashRate":0,"temp":0,"power":0,"sharesAccepted":18446744073709551615})",doc),1000,miner));
    assert(miner.sharesAccepted == UINT64_MAX);

    int64_t epoch;
    assert(parseUtcTimestamp("2026-10-06T00:00:00Z",epoch));
    assert(epoch == 1791244800);
    assert(!parseUtcTimestamp("2026-02-29T00:00:00Z",epoch));
    assert(parseUtcTimestamp("2024-02-29T00:00:00Z",epoch));
    assert(!parseUtcTimestamp("2026-10-06T25:00:00Z",epoch));
    assert(!parseUtcTimestamp("2026-10-06T00:00:00+03:00",epoch));
    assert(parseUtcTimestamp("2026-10-06T00:00:00Z",epoch));
    const auto root = decode(R"({"channel":"ticker","type":"snapshot","data":[{"symbol":"BTC/USD","timestamp":"2026-10-06T00:00:00.123456Z","last":96420,"change_pct":-2.4,"low":94180,"high":97030}]})",doc);
    BtcSample btc;
    assert(parseKrakenTicker(root,1000,epoch + 10,btc));
    assert(std::abs(btc.change24h + 2.4) < 1e-9 && btc.sourceAgeMs == 10000 && btc.rangeValid);
    assert(btcFresh(btc,50999,true));
    assert(!btcFresh(btc,51000,true));
    assert(!parseKrakenTicker(root,1000,epoch + 60,btc));
    assert(!parseKrakenTicker(root,1000,epoch - 121,btc));
    assert(!parseKrakenTicker(root,1000,0,btc));
    doc["data"][0]["change_pct"] = nullptr;
    assert(!parseKrakenTicker(doc.as<JsonVariantConst>(),1000,epoch,btc));
    doc["data"][0]["change_pct"] = 2.4;
    doc["data"][0]["symbol"] = "ETH/USD";
    assert(!parseKrakenTicker(doc.as<JsonVariantConst>(),1000,epoch,btc));
    assert(parseUtcTimestamp("2026-10-06T00:00:00.123456Z",epoch));
    assert(!parseUtcTimestamp("2026-10-06T00:00:00.xyzZ",epoch));
    uint32_t height;
    double fee;
    assert(parseBlockHeight(decode("970198",doc),height) && height == 970198);
    assert(!parseBlockHeight(decode("-1",doc),height));
    assert(parseHourFee(decode(R"({"hourFee":1,"minimumFee":1})",doc),fee) && fee == 1);
    assert(!parseHourFee(decode(R"({"minimumFee":1})",doc),fee));
    assert(parseMiner(decode(R"({"hashRate":1103.4144,"temp":58,"power":23.8935,"sharesAccepted":23,"vrTemp":61,"fanrpm":3568,"fanSpeed":68,"frequency":550,"coreVoltage":1250,"coreVoltageActual":1229,"uptimeSeconds":883,"sharesRejected":0})",doc),1000,miner));
    assert(miner.detailsMask == 63 && miner.fanRPM == 3568 && std::abs(miner.coreVoltageV-1.229) < 1e-9 && miner.uptimeSeconds == 883);
    doc["fanrpm"] = nullptr; doc["fanSpeed"] = 3568;
    assert(parseMiner(doc.as<JsonVariantConst>(),1000,miner) && miner.fanRPM == 3568);
    doc["fanSpeed"] = nullptr; doc["fanspeed"] = 68;
    assert(parseMiner(doc.as<JsonVariantConst>(),1000,miner) && !(miner.detailsMask & 2));

    auto data = normalData();
    auto text = overviewText(data,2000,true);
    assert(strcmp(text.price,"$86,169") == 0);
    assert(strcmp(text.hashRate,"1.10 TH/s") == 0);
    assert(strcmp(text.temperaturePower,"58 C  23.9 W") == 0);
    assert(strcmp(text.shares,"23 SHARES") == 0);
    checkLayout(text);
    data.miner.requestOk = false;
    text = overviewText(data,2000,true);
    assert(!text.minerFresh && strcmp(text.shares,"23 SHARES *") == 0);
    checkLayout(text);
    text = overviewText({},2000,false);
    assert(strcmp(text.price,"--") == 0 && strcmp(text.shares,"-- SHARES") == 0);
    checkLayout(text);
    for (double price : {1.0,99999.0,100000.0,9999999.0,1e12}) {
        data.btc.priceUSD = price;
        data.btc.change24h = -99.99;
        data.miner = {1e9,200,100000,UINT64_MAX,1000,true,true};
        checkLayout(overviewText(data,2000,true));
        checkLayout(overviewText(data,950000,false));
    }
    assert(fittedFont("Lofi-Miner-Setup",137,18,kFont13));
    assert(fittedFont("PASS: lofiminer",137,17,kFont13));
    assert(fittedFont("192.168.4.1",137,20,kFont13));
    const auto clock = makeScreenFrame(Page::Clock,{},0,1791320399,true); // 20:59:59 UTC
    assert(strcmp(clock.fields[1].text,"23:59") == 0 && strcmp(clock.fields[2].text,"06.10.2026") == 0);
    const auto midnight = makeScreenFrame(Page::Clock,{},0,1791320400,true);
    assert(strcmp(midnight.fields[1].text,"00:00") == 0 && strcmp(midnight.fields[2].text,"07.10.2026") == 0);
    checkScreen(makeSetupFrame());
    for (int page = 0; page < 4; ++page) {
        checkScreen(makeScreenFrame(Page(page),normalData(),2000,epoch,true));
        checkScreen(makeScreenFrame(Page(page),normalData(),950000,epoch,false));
        checkScreen(makeScreenFrame(Page(page),{},2000,0,true));
        auto limits = demoSnapshot(1000);
        limits.btc.priceUSD = 1e12; limits.btc.change24h = -99.99;
        limits.btc.lowUSD = 999999.5; limits.btc.highUSD = 1e12;
        limits.miner.sharesAccepted = UINT64_MAX; limits.miner.sharesRejected = UINT64_MAX;
        limits.miner.uptimeSeconds = UINT32_MAX; limits.miner.fanRPM = 100000;
        limits.chain.hourFee = 1e6;
        checkScreen(makeScreenFrame(Page(page),limits,2000,epoch,true));
        limits.miner.detailsMask = 0; limits.chain.feeValid = false;
        checkScreen(makeScreenFrame(Page(page),limits,2000,epoch,true));
    }
    std::cout << "PASS: miner/Kraken/chain JSON, freshness and rollover, Moscow midnight, four screen bounds and ink\n";
}
