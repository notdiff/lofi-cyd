#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>

using WebSocketsNetworkClient = WiFiClient;
class WebSocketsNetworkClientSecure : public WiFiClientSecure {
  public:
    WebSocketsNetworkClientSecure() {
        setHandshakeTimeout(8);
    }
};

#define NETWORK_SET_TIMEOUT_IN_SECONDS
