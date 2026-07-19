#pragma once

#include <Arduino.h>

struct AppSettings {
  String   wifiSsid;
  String   wifiPassword;
  uint8_t  brightnessPercent;
  bool     autoBrightness;      // adjust backlight from front LDR (GPIO 34)
  uint16_t screenOffTimeoutSec; // 0 = disabled; screen blanks after this many seconds
  bool     ledEnabled;          // onboard RGB status LED on/off
  bool     keepHotspotOn;

  // ── UberSDR server (overview display) ──
  String   ubersdrHost;      // IP or hostname (no scheme)
  uint16_t ubersdrPort;      // HTTP port
  String   ubersdrPassword;  // admin password → X-Admin-Password header
  bool     ubersdrTls;       // true = connect via HTTPS (no cert validation)
};

void settingsBegin();
const AppSettings& getSettings();
void saveSettings(const AppSettings& settings);
bool hasWifiCredentials();

// True once a usable UberSDR host is configured (non-empty, non-placeholder).
bool hasUberSDRServer();

void factoryResetSettings();
