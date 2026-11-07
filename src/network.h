#pragma once
#include "btc_history.h"
#include "metrics.h"
#include "miner_control.h"
#include <Arduino.h>

void networkBegin();
void networkLoop();
LiveSnapshot networkSnapshot();
BtcHistory networkHistory();
void networkOpenSetup();
bool networkSetupActive();
bool networkConnected();
void networkPrintStatus();
void networkStatusText(char* out, size_t size);
ControlReport networkControlReport();
bool networkMinerCommand(const MinerCommand& command, uint32_t& id);
