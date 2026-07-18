#pragma once

#include <Arduino.h>

struct SystemSnapshot {
  bool wifiConnected;
  bool timeValid;
  time_t epoch;
  uint32_t uptimeSeconds;
  String localIp;
};

void connectivityBegin();
void connectivityLoop();
void reconnectWifi();
SystemSnapshot getSystemSnapshot();
