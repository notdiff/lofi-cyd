#include <cassert>
#include <cstring>
#include <iostream>
#include "rabbit_scene.h"
#include "pages_assets.h"
#include "screens.h"

static const RabbitSprite *const groups[] = {kRabbittool,kRabbitnotebook,kRabbitsleeping,kRabbitfalltool,kRabbitfallnotebook};
static int groupFrames(int group) { return group < 2 ? 8 : (group == 2 ? 4 : 12); }
static void writePixel(uint16_t color, uint8_t alpha) {
    const char rgba[] = {char((color>>11&31)*255/31),char((color>>5&63)*255/63),
                         char((color&31)*255/31),char(alpha*17)};
    std::cout.write(rgba,4);
}
int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1],"--region") == 0) {
        const auto r = rabbitRegion(std::atoi(argv[2]));
        std::cout << '[' << r.x << ',' << r.y << ',' << r.w << ',' << r.h << "]\n";
        return 0;
    }
    if (argc == 2 && strcmp(argv[1],"--frames") == 0) {
        for (int group = 0; group < 5; ++group)
            for (int frame = 0; frame < groupFrames(group); ++frame) {
                const auto &sprite = groups[group][frame];
                uint8_t colors[144];
                for (int y = 0; y < sprite.height; ++y) {
                    rabbitRow(sprite,y,colors);
                    for (int x = 0; x < sprite.width; ++x)
                        writePixel(sprite.palette[colors[x]],sprite.opacity[colors[x]]);
                }
            }
        return 0;
    }
    if (argc == 6 && strcmp(argv[1],"--composite") == 0) {
        const int page = std::atoi(argv[2]), frame = std::atoi(argv[3]), lift = std::atoi(argv[4]);
        const auto region = rabbitRegion(page);
        const auto action = strcmp(argv[5],"fall") == 0 ? RabbitAction::Fall : RabbitAction::Idle;
        const auto &sprite = rabbitSpriteForPose(page,{uint8_t(frame),int8_t(lift),action});
        uint16_t line[320];
        for (int row = 0; row < region.h; ++row) {
            rabbitCompositeRow(sprite,region,lift,row,kPageBackgrounds[page],line);
            for (int x = 0; x < region.w; ++x) writePixel(line[x],15);
        }
        return 0;
    }
    if (argc == 3 && (strcmp(argv[1],"--timeline") == 0 || strcmp(argv[1],"--reaction") == 0)) {
        const uint8_t page = std::atoi(argv[2]);
        RabbitAnimator rabbit(42);
        RabbitPose previous = {255,0,RabbitAction::Idle};
        bool first = true;
        const bool reaction = strcmp(argv[1],"--reaction") == 0;
        const uint32_t end = reaction ? 7000 : 45000;
        std::cout << '[';
        for (uint32_t now = 0; now <= end; now += 25) {
            if (reaction && now == 1000) rabbit.react(page,now);
            const auto pose = rabbit.update(page,now);
            if (pose.frame != previous.frame || pose.lift != previous.lift || pose.action != previous.action || now == end) {
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"ms\":" << now << ",\"frame\":" << unsigned(pose.frame)
                          << ",\"lift\":" << int(pose.lift) << ",\"action\":\"" << rabbitActionName(pose.action) << "\"}";
                previous = pose;
            }
        }
        std::cout << "]\n";
        return 0;
    }
    assert(rabbitBlend(0xffff,0x1234,0) == 0x1234);
    assert(rabbitBlend(0x1234,0xffff,15) == 0x1234);
    const auto overview = rabbitRegion(1), btc = rabbitRegion(3);
    assert(overview.x == btc.x && overview.y == btc.y && overview.w == btc.w && overview.h == btc.h);
    assert(kRabbittool[0].width == kRabbitnotebook[0].width && kRabbittool[0].height == kRabbitnotebook[0].height);
    for (int page = 0; page < 4; ++page) {
        const auto region = rabbitRegion(page);
        assert(region.x >= 0 && region.y >= 0 && region.x+region.w <= 320 && region.y+region.h <= 240);
        const auto fields = makeScreenFrame(Page(page),demoSnapshot(1000),2000,1791244800,true);
        for (int i = 0; i < fields.count; ++i) {
            const auto &f = fields.fields[i];
            assert(region.x+region.w <= f.x || f.x+f.w <= region.x || region.y+region.h <= f.y || f.y+f.h <= region.y);
        }
        RabbitAnimator normal(7), wrapped(7);
        if (!rabbitEnabled(page)) {
            assert(page == 2 && region.w == 0 && region.h == 0);
            for (uint32_t now = 0; now <= 100000; now += 25)
                assert(normal.update(page,now).frame == 255);
            assert(!normal.blinks && !normal.waves && !normal.helmets && !normal.sleepActions);
            continue;
        }
        bool closedEyes = false, halfEyes = false, waving = false, helmet = false, sleepingMove = false;
        for (uint32_t now = 0; now <= 100000; now += 25) {
            const auto pose = normal.update(page,now), overflow = wrapped.update(page,UINT32_MAX-4000+now);
            assert(pose.frame == overflow.frame && pose.action == overflow.action && pose.lift == overflow.lift);
            assert(pose.frame < (page == 0 ? 4 : 8) && pose.lift >= 0 && pose.lift <= 1);
            closedEyes |= pose.action == RabbitAction::Blink && pose.frame == 2;
            halfEyes |= pose.action == RabbitAction::Blink && pose.frame == 1;
            waving |= pose.action == RabbitAction::Wave;
            helmet |= pose.action == RabbitAction::Helmet;
            sleepingMove |= pose.action == RabbitAction::Ear || pose.action == RabbitAction::Nuzzle;
            if (page == 0) assert(pose.action != RabbitAction::Blink && pose.action != RabbitAction::Wave && pose.action != RabbitAction::Helmet);
        }
        if (page == 0) assert(sleepingMove && !normal.blinks && !normal.waves && !normal.helmets);
        else assert(closedEyes && halfEyes && waving && helmet && !normal.sleepActions);
    }
    RabbitAnimator rabbit(1);
    rabbit.update(1,0); rabbit.update(1,20000);
    assert(rabbit.update(0,20001).action == RabbitAction::Sleep);
    assert(rabbit.update(3,20002).action == RabbitAction::Idle);
    RabbitAnimator sleeping(1);
    assert(sleeping.update(0,0).action == RabbitAction::Sleep);
    for (uint32_t now = 0; now <= 100000; now += 100) {
        assert(!sleeping.react(0,now));
        assert(sleeping.update(0,now).action != RabbitAction::Fall);
    }
    assert(!sleeping.falls);
    for (uint8_t page : {uint8_t(1),uint8_t(3)}) {
        RabbitAnimator tapped(1), overflow(1);
        const uint32_t duration = 2000;
        assert(tapped.react(page,100));
        assert(overflow.react(page,UINT32_MAX-1500));
        assert(!tapped.react(page,150)); // Repeated taps cannot postpone completion.
        bool seen[12] = {};
        for (uint32_t elapsed = 0; elapsed < duration; elapsed += 25) {
            const auto pose = tapped.update(page,100+elapsed);
            const auto wrapped = overflow.update(page,UINT32_MAX-1500+elapsed);
            assert(pose.action == RabbitAction::Fall);
            assert(pose.frame == wrapped.frame && pose.action == wrapped.action);
            assert(pose.frame < 12); seen[pose.frame] = true;
        }
        for (bool frame : seen) assert(frame);
        assert(tapped.update(page,100+duration).action == RabbitAction::Idle);
        assert(tapped.falls == 1u);
        const RabbitPose idle = {0,0,page == 0 ? RabbitAction::Sleep : RabbitAction::Idle};
        const auto region = rabbitRegion(page);
        int hits = 0;
        for (int y = region.y; y < region.y+region.h; ++y)
            for (int x = region.x; x < region.x+region.w; ++x) hits += rabbitHit(page,idle,x,y);
        assert(hits > 1000 && hits < region.w*region.h);
        assert(!rabbitHit(page,idle,region.x-1,region.y));
        assert(!rabbitHit(page,idle,region.x+region.w,region.y));
        assert(!rabbitHit(page,idle,region.x,region.y-1));
    }
    assert(!rabbit.react(2,21000));
    assert(rabbit.react(1,22000));
    assert(rabbit.update(3,22001).action == RabbitAction::Idle);
    assert(rabbit.react(1,23000)); rabbit.update(2,23001);
    assert(rabbit.update(1,23002).action == RabbitAction::Idle);
    // Decode all rows into a guarded buffer, checking dimensions and token bounds.
    int repeats = 0;
    for (int group = 0; group < 5; ++group)
        for (int frame = 0; frame < groupFrames(group); ++frame) {
            const auto &sprite = groups[group][frame];
            for (int y = 0; y < sprite.height; ++y) {
                uint8_t guarded[146]; memset(guarded,0xaa,sizeof(guarded));
                rabbitRow(sprite,y,guarded+1);
                assert(guarded[0] == 0xaa && guarded[sprite.width+1] == 0xaa);
                int width = 0;
                for (int at = sprite.rows[y]; at < sprite.rows[y+1];) {
                    const uint8_t token = sprite.data[at++];
                    if (!token) {
                        assert(at+2 <= sprite.rows[y+1]);
                        const uint8_t count = sprite.data[at++];
                        assert(count >= 4); width += count; ++at; ++repeats;
                        continue;
                    }
                    assert((token&127) > 0); width += token&127;
                    if (!(token&128)) at += token&127;
                    assert(at <= sprite.rows[y+1]);
                }
                assert(width == sprite.width);
                for (int x = 0; x < sprite.width; ++x) assert(sprite.opacity[guarded[x+1]] <= 15);
            }
        }
    assert(repeats > 0);
    std::cout << "PASS: mascot placement, blink/actions, tap reactions and deadlines, hit masks, page changes, millis rollover and sprite bounds\n";
}
