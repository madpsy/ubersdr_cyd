#include "settings.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_partition.h>

#include <memory>

#include "app_config.h"

namespace {

Preferences  preferences;
AppSettings  currentSettings;

constexpr char kNamespace[]       = "ubersdr";
constexpr char kWifiJsonPath[]    = "/wifi.json";
constexpr char kUberSDRJsonPath[] = "/ubersdr.json";

bool isPlaceholder(const String& value) {
  return value.length() == 0 || value.startsWith("your-");
}

String limitedString(String value, size_t maxLen) {
  if (value.length() > maxLen) value = value.substring(0, maxLen);
  return value;
}

void normalizeSettings(AppSettings& settings) {
  settings.wifiSsid     = limitedString(settings.wifiSsid,     64);
  settings.wifiPassword = limitedString(settings.wifiPassword, 64);
  settings.brightnessPercent =
      constrain(settings.brightnessPercent,
                static_cast<uint8_t>(5),
                static_cast<uint8_t>(100));

  settings.ubersdrHost     = limitedString(settings.ubersdrHost,     64);
  settings.ubersdrPassword = limitedString(settings.ubersdrPassword, 64);
  if (settings.ubersdrPort == 0) settings.ubersdrPort = 8080;

  // Strip a placeholder host so hasUberSDRServer() reports false.
  if (isPlaceholder(settings.ubersdrHost)) settings.ubersdrHost = "";
}

// Try to read WiFi credentials from /wifi.json on LittleFS.
// Returns true and populates ssid/pass if the file exists and is valid.
bool loadWifiJson(String& ssid, String& pass) {
  if (!LittleFS.exists(kWifiJsonPath)) {
    Serial.println("wifi.json: not found on LittleFS");
    return false;
  }

  File f = LittleFS.open(kWifiJsonPath, "r");
  if (!f) {
    Serial.println("wifi.json: failed to open");
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("wifi.json: JSON parse error: %s\n", err.c_str());
    return false;
  }

  ssid = doc["ssid"] | "";
  pass = doc["password"] | "";

  if (isPlaceholder(ssid)) {
    Serial.println("wifi.json: placeholder credentials, ignoring");
    return false;
  }

  Serial.printf("wifi.json: loaded SSID \"%s\"\n", ssid.c_str());
  return true;
}

// Try to read UberSDR server config from /ubersdr.json on LittleFS.
// Returns true and populates host/port/pass if valid (non-placeholder host).
bool loadUberSDRJson(String& host, uint16_t& port, String& pass) {
  if (!LittleFS.exists(kUberSDRJsonPath)) {
    Serial.println("ubersdr.json: not found on LittleFS");
    return false;
  }

  File f = LittleFS.open(kUberSDRJsonPath, "r");
  if (!f) {
    Serial.println("ubersdr.json: failed to open");
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("ubersdr.json: JSON parse error: %s\n", err.c_str());
    return false;
  }

  host = doc["host"] | "";
  port = doc["port"] | 8080;
  pass = doc["password"] | "";

  if (isPlaceholder(host)) {
    Serial.println("ubersdr.json: placeholder host, ignoring");
    return false;
  }

  Serial.printf("ubersdr.json: loaded host \"%s:%u\"\n", host.c_str(), port);
  return true;
}

// ── Web-flasher config import ─────────────────────────────────────────────────
// The browser flasher (docs/flash/) writes WiFi + UberSDR settings into the
// raw "usercfg" partition: 8-byte magic, uint32 LE JSON length, JSON bytes.
// Import the values into NVS once, then erase the sector so the credentials
// don't linger in flash and later portal/on-screen edits stick.

constexpr char   kUserCfgMagic[8]  = {'U', 'B', 'S', 'D', 'R', 'C', 'F', '1'};
constexpr size_t kUserCfgMaxJson   = 3072;

void importFlasherConfig() {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "usercfg");
  if (part == nullptr) return;

  struct __attribute__((packed)) {
    char     magic[8];
    uint32_t length;
  } header;
  if (esp_partition_read(part, 0, &header, sizeof(header)) != ESP_OK) return;
  if (memcmp(header.magic, kUserCfgMagic, sizeof(kUserCfgMagic)) != 0) return;

  if (header.length > 0 && header.length <= kUserCfgMaxJson) {
    std::unique_ptr<char[]> buf(new char[header.length + 1]);
    if (esp_partition_read(part, sizeof(header), buf.get(), header.length) ==
        ESP_OK) {
      buf[header.length] = '\0';

      JsonDocument doc;
      if (deserializeJson(doc, buf.get()) == DeserializationError::Ok) {
        const String ssid  = doc["wifi_ssid"] | "";
        const String wpass = doc["wifi_password"] | "";
        if (!isPlaceholder(ssid)) {
          preferences.putString("ssid", limitedString(ssid, 64));
          preferences.putString("pass", limitedString(wpass, 64));
          Serial.printf("usercfg: imported WiFi SSID \"%s\"\n", ssid.c_str());
        }

        const String   host  = doc["ubersdr_host"] | "";
        const uint16_t port  = doc["ubersdr_port"] | 8080;
        const String   upass = doc["ubersdr_password"] | "";
        if (!isPlaceholder(host)) {
          preferences.putString("ushost", limitedString(host, 64));
          preferences.putUShort("usport", port == 0 ? 8080 : port);
          preferences.putString("uspass", limitedString(upass, 64));
          Serial.printf("usercfg: imported UberSDR host \"%s:%u\"\n",
                        host.c_str(), port);
        }
      } else {
        Serial.println("usercfg: JSON parse error, discarding");
      }
    }
  }

  // Consume the blob whether or not it parsed, so we never re-import.
  esp_partition_erase_range(part, 0, SPI_FLASH_SEC_SIZE);
  Serial.println("usercfg: partition consumed and erased");
}

}  // namespace

void settingsBegin() {
  // Mount LittleFS (format on first use if blank)
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  preferences.begin(kNamespace, false);

  // Import any config baked in by the browser flasher (one-shot, into NVS).
  importFlasherConfig();

  // ── Wi-Fi credentials ──
  // Priority 1: /wifi.json on LittleFS (user-editable, gitignored)
  String jsonSsid, jsonPass;
  if (loadWifiJson(jsonSsid, jsonPass)) {
    currentSettings.wifiSsid     = jsonSsid;
    currentSettings.wifiPassword = jsonPass;
  } else {
    // Priority 2: NVS (saved by on-screen keyboard or web portal)
    currentSettings.wifiSsid     = preferences.getString("ssid", "");
    currentSettings.wifiPassword = preferences.getString("pass", "");
  }

  // ── UberSDR server config ──
  // Priority 1: /ubersdr.json on LittleFS
  // Priority 2: NVS (saved by the web portal)
  // Priority 3: compile-time defaults from app_config.h
  String usHost, usPass;
  uint16_t usPort = 8080;
  if (loadUberSDRJson(usHost, usPort, usPass)) {
    currentSettings.ubersdrHost     = usHost;
    currentSettings.ubersdrPort     = usPort;
    currentSettings.ubersdrPassword = usPass;
  } else {
    currentSettings.ubersdrHost =
        preferences.getString("ushost", String(UBERSDR_HOST));
    currentSettings.ubersdrPort =
        preferences.getUShort("usport", static_cast<uint16_t>(UBERSDR_PORT));
    if (currentSettings.ubersdrPort == 0) currentSettings.ubersdrPort = 8080;
    currentSettings.ubersdrPassword =
        preferences.getString("uspass", String(UBERSDR_PASSWORD));
  }

  currentSettings.brightnessPercent =
      preferences.getUChar("bright", static_cast<uint8_t>(DEFAULT_BRIGHTNESS));
  currentSettings.keepHotspotOn = preferences.getBool("apalwayson", false);

  normalizeSettings(currentSettings);

  Serial.printf("Settings: SSID=\"%s\" brightness=%u%% ubersdr=\"%s:%u\"\n",
                currentSettings.wifiSsid.c_str(),
                currentSettings.brightnessPercent,
                currentSettings.ubersdrHost.c_str(),
                currentSettings.ubersdrPort);
}

const AppSettings& getSettings() {
  return currentSettings;
}

void saveSettings(const AppSettings& settings) {
  currentSettings = settings;
  normalizeSettings(currentSettings);

  // Save to NVS (on-screen keyboard / web portal changes go here)
  preferences.putString("ssid",      currentSettings.wifiSsid);
  preferences.putString("pass",      currentSettings.wifiPassword);
  preferences.putUChar("bright",     currentSettings.brightnessPercent);
  preferences.putBool("apalwayson",  currentSettings.keepHotspotOn);

  preferences.putString("ushost",    currentSettings.ubersdrHost);
  preferences.putUShort("usport",    currentSettings.ubersdrPort);
  preferences.putString("uspass",    currentSettings.ubersdrPassword);
}

bool hasWifiCredentials() {
  return currentSettings.wifiSsid.length() > 0;
}

bool hasUberSDRServer() {
  return currentSettings.ubersdrHost.length() > 0;
}

void factoryResetSettings() {
  preferences.clear();
  // Note: wifi.json / ubersdr.json on LittleFS are NOT cleared by factory reset —
  // they are developer config files. Only NVS overrides are cleared.
  settingsBegin();
}
