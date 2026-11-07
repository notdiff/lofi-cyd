#pragma once
#include <cstdint>

#ifndef LOFI_MINER_HOST
#define LOFI_MINER_HOST "192.168.1.100"
#endif

namespace config {
constexpr char kVersion[] = "0.9.3";
constexpr char kMinerHost[] = LOFI_MINER_HOST;
constexpr char kMinerUrl[] = "http://" LOFI_MINER_HOST "/api/system/info";
constexpr char kMinerSystemUrl[] = "http://" LOFI_MINER_HOST "/api/system";
constexpr char kMinerRestartUrl[] = "http://" LOFI_MINER_HOST "/api/system/restart";
constexpr char kHeightUrl[] = "https://mempool.space/api/blocks/tip/height";
constexpr char kFeesUrl[] = "https://mempool.space/api/v1/fees/recommended";
constexpr char kDifficultyUrl[] = "https://mempool.space/api/v1/mining/hashrate/3d";
constexpr uint32_t kHttpConnectTimeoutMs = 2500;
constexpr uint32_t kHttpReadTimeoutMs = 3000;
constexpr uint32_t kTlsHandshakeTimeoutSeconds = 8;
constexpr uint32_t kScreenRefreshMs = 250;
constexpr uint32_t kRabbitRefreshMs = 25;
}
