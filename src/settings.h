#pragma once

#include <Arduino.h>

struct AppSettings {
  String   wifiSsid;
  String   wifiPassword;
  uint8_t  brightnessPercent;
  bool     keepHotspotOn;

  // ── UberSDR server (overview display) ──
  String   ubersdrHost;      // IP or hostname (no scheme)
  uint16_t ubersdrPort;      // HTTP port
  String   ubersdrPassword;  // admin password → X-Admin-Password header
};

void settingsBegin();
const AppSettings& getSettings();
void saveSettings(const AppSettings& settings);
bool hasWifiCredentials();

// True once a usable UberSDR host is configured (non-empty, non-placeholder).
bool hasUberSDRServer();

void factoryResetSettings();
