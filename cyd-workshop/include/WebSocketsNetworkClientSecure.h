#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>

// WebSockets' supported custom transport; bound TLS handshake independently of TCP.
using WebSocketsNetworkClient = WiFiClient;
class WebSocketsNetworkClientSecure : public WiFiClientSecure {
public:
    WebSocketsNetworkClientSecure() { setHandshakeTimeout(8); }
};
// Arduino-ESP32 2.x WiFiClient::setTimeout() takes seconds, not milliseconds.
#define NETWORK_SET_TIMEOUT_IN_SECONDS
