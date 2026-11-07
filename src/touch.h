#pragma once
#include "touch_gesture.h"
#include <cstdint>

void touchBegin(uint8_t rotation);
void touchSetRotation(uint8_t rotation);
TouchEvent touchPoll(uint32_t now, bool enabled);
void touchPrintStatus();
