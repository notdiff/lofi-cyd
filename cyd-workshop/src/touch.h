#pragma once
#include <cstdint>
#include "touch_gesture.h"

void touchBegin(uint8_t rotation);
void touchSetRotation(uint8_t rotation);
TouchEvent touchPoll(uint32_t now, bool enabled);
void touchPrintStatus();
