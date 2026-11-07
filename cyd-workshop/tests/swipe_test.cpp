#include <cassert>
#include <iostream>
#include "swipe.h"

static Swipe stroke(int dx, int dy, uint32_t duration = 300, uint32_t start = 1000) {
    SwipeRecognizer gesture;
    for (int i = 0; i <= 10; ++i)
        assert(gesture.update(true,160+dx*i/10,120+dy*i/10,start+duration*i/10) == Swipe::None);
    assert(gesture.update(false,0,0,start+duration+79) == Swipe::None);
    const auto result = gesture.update(false,0,0,start+duration+80);
    assert(gesture.update(false,0,0,start+duration+500) == Swipe::None); // one per contact
    return result;
}
int main() {
    assert(stroke(-100,15) == Swipe::Next);
    assert(stroke(100,-15) == Swipe::Previous);
    assert(stroke(-60,0,50) == Swipe::Next); // threshold boundaries
    assert(stroke(-59,0) == Swipe::None);
    assert(stroke(0,0) == Swipe::None); // tap
    assert(stroke(3,-2,800) == Swipe::None); // stationary finger noise
    assert(stroke(30,100) == Swipe::None); // vertical gesture
    assert(stroke(60,50) == Swipe::None); // diagonal
    assert(stroke(150,56) == Swipe::None); // excessive vertical excursion
    assert(stroke(-100,0,49) == Swipe::None); // impulse
    assert(stroke(-100,0,1501) == Swipe::None); // hold then drag
    assert(stroke(-100,0,1500) == Swipe::Next);
    assert(stroke(-100,0,300,UINT32_MAX-150) == Swipe::Next); // millis rollover

    SwipeRecognizer gesture;
    assert(gesture.update(true,220,100,1000) == Swipe::None);
    assert(gesture.update(true,190,102,1040) == Swipe::None);
    assert(gesture.update(false,0,0,1060) == Swipe::None);
    assert(gesture.update(true,140,101,1100) == Swipe::None); // resume after pressure dropout
    assert(gesture.update(false,0,0,1180) == Swipe::Next);
    gesture.update(true,220,100,1300);
    gesture.update(true,100,100,1360);
    assert(gesture.update(false,0,0,1440) == Swipe::None); // two isolated samples
    gesture.update(true,220,100,1500);
    gesture.update(true,160,100,1560);
    gesture.update(true,100,100,1620);
    gesture.reset(); // BOOT, rotation or setup cancels a pending gesture
    assert(gesture.update(false,0,0,1700) == Swipe::None);
    gesture.update(true,200,20,1800);
    gesture.update(true,150,120,1860);
    gesture.update(true,100,20,1920);
    assert(gesture.update(false,0,0,2000) == Swipe::None); // vertical excursion ends at starting Y
    assert(adjacentPage(3,Swipe::Next) == 0 && adjacentPage(0,Swipe::Previous) == 3);
    assert(adjacentPage(1,Swipe::None) == 1);
    std::cout << "PASS: swipe directions, noise/taps/holds/diagonals, dropout, one per stroke, rollover and page wrap\n";
}
