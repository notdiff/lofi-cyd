#pragma once
#include <Arduino.h>
#include "metrics.h"

void networkBegin();
void networkLoop();
LiveSnapshot networkSnapshot();
void networkOpenSetup();
bool networkSetupActive();
bool networkConnected();
void networkPrintStatus();
void networkStatusText(char *out, size_t size);
