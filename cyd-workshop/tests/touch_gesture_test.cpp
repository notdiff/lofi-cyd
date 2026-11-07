#include <cassert>
#include <iostream>
#include "touch_gesture.h"

static void tap(uint32_t start) {
    TouchGesture g;
    assert(!g.update(true,240,150,start).tap);
    assert(!g.update(true,242,152,start+40).tap);
    assert(!g.update(true,241,151,start+80).tap);
    assert(!g.update(false,0,0,start+100).tap);
    const auto event = g.update(false,0,0,start+160);
    assert(event.tap && event.swipe == Swipe::None && event.x == 241 && event.y == 151);
    assert(!g.update(false,0,0,start+200).tap);
}
int main() {
    tap(0); tap(UINT32_MAX-100);
    for (int direction : {-1,1}) {
        TouchGesture g;
        g.update(true,240,150,0); g.update(true,240+direction*30,150,50);
        g.update(true,240+direction*70,150,100);
        const auto event = g.update(false,0,0,180);
        assert(!event.tap && event.swipe == (direction < 0 ? Swipe::Next : Swipe::Previous));
    }
    TouchGesture g;
    g.update(true,240,150,0); g.update(false,0,0,100);
    assert(!g.update(false,0,0,200).tap); // One noisy point.
    g.update(true,240,150,300); g.update(true,240,150,1500);
    assert(!g.update(false,0,0,1580).tap); // Hold.
    g.update(true,240,150,2000); g.update(true,275,150,2040); g.update(true,240,150,2080);
    assert(!g.update(false,0,0,2160).tap); // Drag returning to its origin.
    g.update(true,240,150,3000); g.update(true,240,150,3040);
    g.update(false,0,0,3060); g.update(true,241,150,3080);
    assert(g.update(false,0,0,3160).tap); // Brief contact dropout.
    g.update(true,240,150,4000); g.update(true,240,150,4040); g.reset();
    assert(!g.update(false,0,0,4200).tap);
    std::cout << "PASS: taps vs swipes, release/centroid, noise, holds, drags, dropout, reset and millis rollover\n";
}
