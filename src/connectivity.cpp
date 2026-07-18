#include "connectivity.h"

#include <WiFi.h>
#include <time.h>

#include "settings.h"

namespace {
constexpr char kNtpServer1[] = "pool.ntp.org";
constexpr char kNtpServer2[] = "time.nist.gov";
constexpr char kNtpServer3[] = "time.google.com";

// Treat any epoch before 2024-01-01 as "not yet synced".
constexpr time_t kValidTimeThreshold = 1704067200;
constexpr uint32_t kReconnectIntervalMs = 10000;

uint32_t g_lastReconnectAttemptMs = 0;

bool isTimeValid(time_t now) {
  return now >= kValidTimeThreshold;
}

void beginWifi() {
  const AppSettings& settings = getSettings();
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  if (settings.wifiSsid.length() > 0) {
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPassword.c_str());
  }
}
}  // namespace

void connectivityBegin() {
  Serial.println();
  Serial.println("UberSDR CYD starting");
  const AppSettings& settings = getSettings();
  Serial.print("Configured Wi-Fi SSID: ");
  Serial.println(settings.wifiSsid.length() > 0 ? settings.wifiSsid : "(none)");

  beginWifi();
  // UTC — the display layer can apply local offsets if needed later.
  configTime(0, 0, kNtpServer1, kNtpServer2, kNtpServer3);
}

void connectivityLoop() {
  const uint32_t nowMs = millis();
  if (hasWifiCredentials() &&
      WiFi.status() != WL_CONNECTED &&
      nowMs - g_lastReconnectAttemptMs >= kReconnectIntervalMs) {
    g_lastReconnectAttemptMs = nowMs;
    reconnectWifi();
  }
}

void reconnectWifi() {
  const AppSettings& settings = getSettings();
  WiFi.disconnect(false);
  if (settings.wifiSsid.length() == 0) {
    return;
  }
  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPassword.c_str());
}

SystemSnapshot getSystemSnapshot() {
  const time_t now = time(nullptr);
  SystemSnapshot snap;
  snap.wifiConnected = WiFi.status() == WL_CONNECTED;
  snap.timeValid = isTimeValid(now);
  snap.epoch = now;
  snap.uptimeSeconds = millis() / 1000;
  snap.localIp = snap.wifiConnected ? WiFi.localIP().toString() : String("--");
  return snap;
}
