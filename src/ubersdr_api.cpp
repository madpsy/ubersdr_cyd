// ubersdr_api.cpp — polls the UberSDR Go server and aggregates a snapshot.
//
// One HTTP request is issued per poll "step".  Steps round-robin across the
// endpoints so each individual request is short and the loop() never blocks
// for long.  A full cycle (all endpoints) completes roughly every
// kStepIntervalMs * kNumSteps milliseconds.

#include "ubersdr_api.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "debug_log.h"
#include "settings.h"

// ── Reachability transition flags ────────────────────────────────────────────
// Defined at file scope (not inside the anonymous namespace) so they are
// accessible both from the task (inside the namespace via extern) and from
// the display loop (Core 1) via the header's extern declarations.
volatile bool g_apiWentDown = false;
volatile bool g_apiWentUp   = false;

namespace {

// ── Poll timing ───────────────────────────────────────────────────────────────
// One endpoint is fetched every kStepIntervalMs.  With 5 steps that gives a
// full refresh of every metric about every 10 s while keeping each loop tick
// responsive.
constexpr uint32_t kStepIntervalMs = 2000;
constexpr uint16_t kHttpTimeoutMs  = 4000;

// Poll steps — one endpoint each.
enum PollStep : uint8_t {
  kStepDescription = 0,   // /api/description       → capacity + timezone
  kStepSessions,          // /admin/sessions        → user / bypass counts
  kStepLoad,              // /admin/system-load     → load + CPU temp
  kStepBands,             // /api/noisefloor/latest → per-band FT8 SNR
  kStepSpace,             // /api/spaceweather      → K/A index, flux
  kStepWeather,           // /api/weather           → terrestrial weather
  kStepSpectrum,          // /api/noisefloor/fft    → one band per cycle (round-robin)
  kStepPskRank,           // /admin/psk-rank        → PSKReporter spots + DXCC rank
  kStepWsprRank,          // /admin/wspr-rank       → WSPR Live 24h/today/yesterday rank
  kStepRbnRank,           // /admin/rbn-data        → RBN skimmer rank
  kStepGpsdo,             // /admin/gpsdo-health    → GPSDO lock + GPS fix status
  kStepHealth,            // /admin/monitor-health  → per-component health grid
  kNumSteps
};

// ── Shared snapshot + mutex ───────────────────────────────────────────────────
// g_work  — written by the API task (Core 0) during HTTP fetch, no mutex needed
//           because only the API task ever writes to it.
// g_snap  — published copy; written by the API task under g_snapMutex after each
//           successful step, read by Core 1 under g_snapMutex via getUberSDRSnapshot().
// The mutex is held only for the brief g_snap = g_work copy, never during HTTP I/O.
UberSDRSnapshot   g_work;
UberSDRSnapshot   g_snap;
SemaphoreHandle_t g_snapMutex    = nullptr;

uint8_t           g_step              = 0;
bool              g_forceRefresh      = false;
volatile bool     g_sessionsFastPoll  = false;  // insert extra sessions step ASAP
int               g_spectrumBand      = 0;   // round-robin index for per-band FFT

// ── Task handle + HWM ────────────────────────────────────────────────────────
TaskHandle_t      g_apiTaskHandle = nullptr;
volatile uint32_t g_apiTaskHwm   = 0;   // updated periodically by the task

// ── Reachability tracking (task-private) ─────────────────────────────────────
// Down-transition fires after kFailThreshold consecutive steps with no success.
constexpr uint8_t kFailThreshold       = 6;   // ~12 s of consecutive failures
uint8_t           g_failStreak         = 0;
bool              g_apiWasDown         = false;
uint32_t          g_lastKnownSuccessMs = 0;

// ── HTTP helper ───────────────────────────────────────────────────────────────
// Performs a blocking GET and returns the response body in `out`.
// When withAuth is true the X-Admin-Password header is added.
// Returns the HTTP status code, or -1 on connection/timeout failure.
int httpGet(const char* path, bool withAuth, String& out) {
  const AppSettings& cfg = getSettings();
  if (cfg.ubersdrHost.length() == 0) {
    debugLogf("GET %s: no host configured", path);
    return -1;
  }

  // Use WiFiClientSecure (no cert validation) when TLS is enabled, otherwise
  // plain WiFiClient.  Both share the same Client interface so the rest of
  // the function is identical.
  WiFiClientSecure tlsClient;
  WiFiClient       plainClient;
  Client*          clientPtr;
  if (cfg.ubersdrTls) {
    tlsClient.setInsecure();
    clientPtr = &tlsClient;
  } else {
    clientPtr = &plainClient;
  }
  Client& client = *clientPtr;
  if (cfg.ubersdrTls) {
    tlsClient.setTimeout(kHttpTimeoutMs / 1000);
  } else {
    plainClient.setTimeout(kHttpTimeoutMs / 1000);
  }

  if (!client.connect(cfg.ubersdrHost.c_str(), cfg.ubersdrPort)) {
    debugLogf("GET %s: connect FAILED %s:%u", path,
              cfg.ubersdrHost.c_str(), cfg.ubersdrPort);
    return -1;
  }

  // Request line + headers
  client.print(F("GET "));
  client.print(path);
  client.print(F(" HTTP/1.1\r\nHost: "));
  client.print(cfg.ubersdrHost);
  client.print(F("\r\n"));
  if (withAuth) {
    client.print(F("X-Admin-Password: "));
    client.print(cfg.ubersdrPassword);
    client.print(F("\r\n"));
  }
  client.print(F("User-Agent: ubersdr-cyd/1.0\r\n"));
  client.print(F("Accept: application/json\r\n"));
  client.print(F("Connection: close\r\n\r\n"));

  // ── Read status line ──
  const uint32_t deadline = millis() + kHttpTimeoutMs;
  while (client.connected() && !client.available()) {
    if (millis() > deadline) { client.stop(); return -1; }
    delay(5);
  }

  String statusLine = client.readStringUntil('\n');   // "HTTP/1.1 200 OK\r"
  int statusCode = -1;
  {
    const int sp1 = statusLine.indexOf(' ');
    if (sp1 > 0) {
      statusCode = statusLine.substring(sp1 + 1, sp1 + 4).toInt();
    }
  }

  // ── Read headers (blank line terminates); detect chunked encoding ──
  bool chunked = false;
  while (client.connected() || client.available()) {
    if (millis() > deadline) { client.stop(); return statusCode; }
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
    String lower = line;
    lower.toLowerCase();
    if (lower.indexOf("transfer-encoding:") >= 0 && lower.indexOf("chunked") >= 0) {
      chunked = true;
    }
  }

  // ── Read raw body ──
  String raw;
  raw.reserve(4096);
  while ((client.connected() || client.available())) {
    if (millis() > deadline) break;
    while (client.available()) {
      raw += static_cast<char>(client.read());
    }
    if (!client.connected() && !client.available()) break;
    delay(1);
  }
  client.stop();

  // ── De-chunk if necessary ──
  // Chunked format: <hexlen>\r\n<data>\r\n ... 0\r\n\r\n
  if (chunked) {
    out = "";
    out.reserve(raw.length());
    int pos = 0;
    const int len = raw.length();
    while (pos < len) {
      // Read chunk-size line (hex up to CRLF).
      int eol = raw.indexOf('\n', pos);
      if (eol < 0) break;
      String sizeLine = raw.substring(pos, eol);
      sizeLine.trim();
      // Strip any chunk extensions after ';'.
      const int semi = sizeLine.indexOf(';');
      if (semi >= 0) sizeLine = sizeLine.substring(0, semi);
      const long chunkSize = strtol(sizeLine.c_str(), nullptr, 16);
      pos = eol + 1;
      if (chunkSize <= 0) break;   // 0-size terminator (or parse failure)
      if (pos + chunkSize > len) {
        out += raw.substring(pos);  // truncated — take what we have
        break;
      }
      out += raw.substring(pos, pos + chunkSize);
      pos += chunkSize;
      // Skip trailing CRLF after the chunk data.
      if (pos < len && raw[pos] == '\r') pos++;
      if (pos < len && raw[pos] == '\n') pos++;
    }
  } else {
    out = raw;
  }

  debugLogf("GET %s -> %d (%u bytes%s)", path, statusCode, out.length(),
            chunked ? ", dechunked" : "");
  return statusCode;
}

// Streaming GET: parses the JSON body directly off the socket with a filter,
// so the full response is never buffered in RAM.  Required for endpoints with
// large payloads — /admin/sessions can exceed 100 KB with many sessions, which
// does not fit in a String on the ESP32.  The request is made as HTTP/1.0 so
// the server never uses chunked transfer encoding, letting ArduinoJson read
// the body straight from the stream.
// Returns the HTTP status code (-1 on connect/timeout, -2 on JSON error).
int httpGetJsonFiltered(const char* path, bool withAuth,
                        const JsonDocument& filter, JsonDocument& doc) {
  const AppSettings& cfg = getSettings();
  if (cfg.ubersdrHost.length() == 0) {
    debugLogf("GET %s: no host configured", path);
    return -1;
  }

  WiFiClientSecure tlsClient;
  WiFiClient       plainClient;
  Client*          clientPtr;
  if (cfg.ubersdrTls) {
    tlsClient.setInsecure();
    clientPtr = &tlsClient;
  } else {
    clientPtr = &plainClient;
  }
  Client& client = *clientPtr;
  if (cfg.ubersdrTls) {
    tlsClient.setTimeout(kHttpTimeoutMs / 1000);
  } else {
    plainClient.setTimeout(kHttpTimeoutMs / 1000);
  }

  if (!client.connect(cfg.ubersdrHost.c_str(), cfg.ubersdrPort)) {
    debugLogf("GET %s: connect FAILED %s:%u", path,
              cfg.ubersdrHost.c_str(), cfg.ubersdrPort);
    return -1;
  }

  client.print(F("GET "));
  client.print(path);
  client.print(F(" HTTP/1.0\r\nHost: "));
  client.print(cfg.ubersdrHost);
  client.print(F("\r\n"));
  if (withAuth) {
    client.print(F("X-Admin-Password: "));
    client.print(cfg.ubersdrPassword);
    client.print(F("\r\n"));
  }
  client.print(F("User-Agent: ubersdr-cyd/1.0\r\n"));
  client.print(F("Accept: application/json\r\n"));
  client.print(F("Connection: close\r\n\r\n"));

  const uint32_t deadline = millis() + kHttpTimeoutMs;
  while (client.connected() && !client.available()) {
    if (millis() > deadline) { client.stop(); return -1; }
    delay(5);
  }

  String statusLine = client.readStringUntil('\n');
  int statusCode = -1;
  {
    const int sp1 = statusLine.indexOf(' ');
    if (sp1 > 0) {
      statusCode = statusLine.substring(sp1 + 1, sp1 + 4).toInt();
    }
  }

  // Skip headers (blank line terminates).
  while (client.connected() || client.available()) {
    if (millis() > deadline) { client.stop(); return statusCode; }
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }

  if (statusCode != 200) {
    client.stop();
    return statusCode;
  }

  const DeserializationError err =
      deserializeJson(doc, client, DeserializationOption::Filter(filter));
  client.stop();
  if (err) {
    debugLogf("GET %s: stream JSON err %s", path, err.c_str());
    return -2;
  }
  debugLogf("GET %s -> %d (streamed)", path, statusCode);
  return statusCode;
}

// ── Quality helpers (mirror band_snr_quality.go) ──────────────────────────────
String snrQuality(float snr) {
  if (snr >= 30) return "EXCELLENT";
  if (snr >= 20) return "GOOD";
  if (snr >= 6)  return "FAIR";
  return "POOR";
}

// Approximate centre frequency of a wavelength-labelled amateur band, in kHz.
// freq = c / wavelength → 299792 kHz·m / wavelength_m.  Used to sort bands by
// ascending centre frequency (160m/1.8 MHz first … 10m/28 MHz last), which is
// robust regardless of the numeric quirks of the label (e.g. 630m vs 60m).
// Returns a large value for unparseable labels so they sort last.
int bandFreqKhz(const String& b) {
  int wl = 0;
  for (uint16_t i = 0; i < b.length(); ++i) {
    const char c = b[i];
    if (c >= '0' && c <= '9') wl = wl * 10 + (c - '0');
    else break;
  }
  if (wl <= 0) return 1000000;   // unknown → sort last
  return 299792 / wl;
}

// ── Per-endpoint parsers ──────────────────────────────────────────────────────

void pollDescription() {
  String body;
  const int code = httpGet("/api/description", false, body);
  if (code != 200) return;

  // /api/description is a large payload; use a filter so ArduinoJson only
  // retains the fields we care about (keeps heap usage tiny).
  JsonDocument filter;
  filter["max_clients"] = true;
  filter["available_clients"] = true;
  filter["receiver"]["timezone_offset"] = true;
  filter["receiver"]["callsign"] = true;
  filter["cw_skimmer_callsign"] = true;
  filter["ant_switch"] = true;   // whole object (enabled/grounded/active_labels)
  filter["rotator"] = true;      // whole object (enabled/connected/azimuth)

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    debugLogf("description: JSON err %s", err.c_str());
    return;
  }

  // The public description payload carries server-computed capacity + usage:
  //   max_clients       = configured MaxSessions
  //   available_clients = max_clients - current non-bypassed users
  // So current users = max_clients - available_clients.  This matches the
  // server's own unique-user accounting exactly (no per-session guessing).
  if (doc["max_clients"].is<int>()) {
    g_work.maxSessions = doc["max_clients"].as<int>();
  }
  if (doc["available_clients"].is<int>()) {
    const int avail = doc["available_clients"].as<int>();
    int used = g_work.maxSessions - avail;
    if (used < 0) used = 0;
    g_work.userCount  = used;
    g_work.usersValid = true;
  }

  // Instance callsign (shown in the header).
  g_work.callsign = doc["receiver"]["callsign"] | "";

  // CW skimmer callsign (may differ from receiver callsign; used for RBN rank).
  g_work.cwSkimmerCallsign = doc["cw_skimmer_callsign"] | "";

  // Instance local-time offset (DST-adjusted, minutes) from receiver block.
  JsonVariant tz = doc["receiver"]["timezone_offset"];
  if (tz.is<int>()) {
    g_work.tzOffsetMinutes = tz.as<int>();
    g_work.tzValid = true;
  }

  // Antenna switch — the "ant_switch" key exists ONLY when the switch is
  // enabled on the server.  Join active_labels into a display string.
  JsonObject asw = doc["ant_switch"].as<JsonObject>();
  if (!asw.isNull()) {
    g_work.antSwitchEnabled  = asw["enabled"] | false;
    g_work.antSwitchGrounded = asw["grounded"] | false;
    String labels;
    JsonArray al = asw["active_labels"].as<JsonArray>();
    if (!al.isNull()) {
      for (JsonVariant v : al) {
        if (labels.length()) labels += ", ";
        labels += String(v.as<const char*>());
      }
    }
    g_work.antSwitchLabels = labels;
  } else {
    g_work.antSwitchEnabled = false;
  }

  // Rotator — always present in the payload; only meaningful when enabled.
  JsonObject rot = doc["rotator"].as<JsonObject>();
  if (!rot.isNull()) {
    g_work.rotatorEnabled   = rot["enabled"]   | false;
    g_work.rotatorConnected = rot["connected"] | false;
    g_work.rotatorAzimuth   = rot["azimuth"]   | -1;
  } else {
    g_work.rotatorEnabled = false;
  }

  debugLogf("description: max=%d used=%d tz=%dmin sw=%d rot=%d/%d az=%d",
            g_work.maxSessions, g_work.userCount, g_work.tzOffsetMinutes,
            g_work.antSwitchEnabled ? 1 : 0,
            g_work.rotatorEnabled ? 1 : 0, g_work.rotatorConnected ? 1 : 0,
            g_work.rotatorAzimuth);
  g_work.lastSuccessMs = millis();
}

void pollSessions() {
  // Filter for both response shapes: the aggregate fields served by
  // ?compact=1, plus the per-session fields needed to compute the bypass
  // count ourselves on older servers that ignore the parameter and return
  // the full per-session payload.
  JsonDocument filter;
  filter["users"] = true;
  filter["bypassed_users"] = true;
  filter["max_sessions"] = true;
  filter["external_sessions"] = true;
  filter["audio_kbps"] = true;
  filter["waterfall_kbps"] = true;
  filter["total_kbps"] = true;
  // non_bypassed_users is an object keyed by UUID strings.  ArduinoJson v7 has
  // no wildcard syntax for arbitrary object keys, so pass the whole map through
  // with `true`.  It is small (one entry per connected user) so RAM is fine.
  filter["non_bypassed_users"] = true;
  JsonObject fSess = filter["sessions"].add<JsonObject>();
  fSess["is_internal"] = true;
  fSess["is_bypassed"] = true;
  fSess["user_session_id"] = true;
  fSess["client_ip"] = true;

  // The full sessions payload grows with active sessions (100 KB+ is normal
  // on a busy instance) — far too large to buffer, so parse off the stream.
  JsonDocument doc;
  const int code = httpGetJsonFiltered("/admin/sessions?compact=1", true, filter, doc);
  if (code != 200) return;

  // ── Compact server response: counts computed server-side ──
  if (doc["bypassed_users"].is<int>()) {
    g_work.bypassCount = doc["bypassed_users"].as<int>();
    if (doc["max_sessions"].is<int>()) g_work.maxSessions = doc["max_sessions"].as<int>();
    if (doc["users"].is<int>())        g_work.userCount   = doc["users"].as<int>();
    g_work.usersValid = true;

    // Network aggregates only exist in the compact payload.
    g_work.externalSessions = doc["external_sessions"] | 0;
    g_work.audioKbps        = doc["audio_kbps"]        | 0;
    g_work.waterfallKbps    = doc["waterfall_kbps"]    | 0;
    g_work.totalKbps        = doc["total_kbps"]        | 0;
    g_work.netValid = true;

    // ── Per-user detail from non_bypassed_users map ──
    // Each key is a UUID; each value is { country, country_code, connected_since }.
    // Parse up to kMaxUserDetails entries; extras are silently dropped.
    int nd = 0;
    JsonObject nbUsers = doc["non_bypassed_users"].as<JsonObject>();
    if (!nbUsers.isNull()) {
      for (JsonPair kv : nbUsers) {
        if (nd >= kMaxUserDetails) break;
        JsonObject u = kv.value().as<JsonObject>();
        if (u.isNull()) continue;

        UserDetail& ud = g_work.userDetails[nd];

        // Country name — truncate to fit char[28].
        const char* country = u["country"] | "";
        strncpy(ud.country, country, sizeof(ud.country) - 1);
        ud.country[sizeof(ud.country) - 1] = '\0';

        // Country code — truncate to fit char[4].
        const char* cc = u["country_code"] | "";
        strncpy(ud.countryCode, cc, sizeof(ud.countryCode) - 1);
        ud.countryCode[sizeof(ud.countryCode) - 1] = '\0';

        // connected_since: ISO-8601 UTC string → time_t via sscanf.
        // Format: "2026-07-19T08:30:00Z"
        ud.connectedSince = 0;
        const char* cs = u["connected_since"] | "";
        if (cs && cs[0]) {
          int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
          if (sscanf(cs, "%d-%d-%dT%d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc) == 6) {
            // Convert to time_t using mktime with UTC (no DST).
            struct tm t = {};
            t.tm_year = yr - 1900;
            t.tm_mon  = mo - 1;
            t.tm_mday = dy;
            t.tm_hour = hr;
            t.tm_min  = mn;
            t.tm_sec  = sc;
            // timegm is not available on all platforms; use mktime + timezone offset.
            // On ESP32 with NTP, timezone is UTC (TZ not set), so mktime ≈ timegm.
            ud.connectedSince = mktime(&t);
          }
        }

        // Frequency in Hz (0 when spectrum-only / no audio session).
        ud.frequencyHz = u["frequency"] | (uint32_t)0;

        // Mode string — force uppercase, truncate to fit char[8].
        const char* modeStr = u["mode"] | "";
        int mi = 0;
        while (modeStr[mi] && mi < (int)(sizeof(ud.mode) - 1)) {
          ud.mode[mi] = (modeStr[mi] >= 'a' && modeStr[mi] <= 'z')
                        ? modeStr[mi] - 32 : modeStr[mi];
          mi++;
        }
        ud.mode[mi] = '\0';

        nd++;
      }
    }
    g_work.userDetailCount = nd;

    debugLogf("sessions: users=%d bypass=%d ext=%d net=%dkbps details=%d (compact)",
              g_work.userCount, g_work.bypassCount,
              g_work.externalSessions, g_work.totalKbps, nd);
    g_work.lastSuccessMs = millis();
    return;
  }

  // ── Fallback: full payload — dedupe bypassed users ourselves ──
  JsonArray sessions = doc["sessions"].as<JsonArray>();
  if (sessions.isNull()) {
    debugLog("sessions: no 'sessions' array");
    return;
  }

  // Count UNIQUE bypassed users, not sessions — one user has several session
  // rows (audio + spectrum + dxcluster) so counting rows would over-count.
  // Dedupe on user_session_id (fall back to client_ip when empty), matching
  // the server's GetBypassedUserCount() semantics.
  // We track up to a small fixed number of distinct bypass keys.
  constexpr int kMaxKeys = 32;
  String keys[kMaxKeys];
  int nKeys = 0;

  for (JsonObject s : sessions) {
    if (s["is_internal"].as<bool>()) continue;
    if (!s["is_bypassed"].as<bool>()) continue;

    String key = s["user_session_id"] | "";
    if (key.length() == 0) key = s["client_ip"] | "";
    if (key.length() == 0) continue;

    bool seen = false;
    for (int i = 0; i < nKeys; ++i) {
      if (keys[i] == key) { seen = true; break; }
    }
    if (!seen && nKeys < kMaxKeys) keys[nKeys++] = key;
  }

  g_work.bypassCount = nKeys;
  g_work.usersValid  = true;   // sessions endpoint reachable
  debugLogf("sessions: bypass=%d", nKeys);
  g_work.lastSuccessMs = millis();
}

void pollLoad() {
  String body;
  const int code = httpGet("/admin/system-load", true, body);
  if (code != 200) return;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    debugLogf("load: JSON err %s", err.c_str());
    return;
  }

  // The server sends load averages as STRINGS (e.g. "0.52"), read from
  // /proc/loadavg — not JSON numbers.  Parse them via String::toFloat().
  // Fall back to numeric access in case a future server sends real numbers.
  auto readLoad = [&](const char* key) -> float {
    JsonVariant v = doc[key];
    if (v.is<const char*>()) return String(v.as<const char*>()).toFloat();
    if (v.is<float>())       return v.as<float>();
    return 0.0f;
  };
  g_work.load1  = readLoad("load_1min");
  g_work.load5  = readLoad("load_5min");
  g_work.load15 = readLoad("load_15min");
  g_work.cpuCores = doc["cpu_cores"] | 0;
  g_work.cpuTempAvailable = doc["cpu_temp_available"] | false;
  g_work.cpuTempC         = doc["cpu_temp_c"] | 0.0f;
  g_work.cpuTempThresholdC= doc["cpu_temp_threshold_c"] | 0.0f;
  g_work.cpuTempStatus    = doc["cpu_temp_status"] | "";
  g_work.loadValid = true;
  debugLogf("load: %.2f/%.2f/%.2f cores=%d temp=%.0f",
            g_work.load1, g_work.load5, g_work.load15,
            g_work.cpuCores, g_work.cpuTempC);
  g_work.lastSuccessMs = millis();
}

void pollBands() {
  String body;
  const int code = httpGet("/api/noisefloor/latest", false, body);
  // 204 = no measurements yet — treat as "no data" but not a hard error.
  if (code != 200) return;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    debugLogf("bands: JSON err %s", err.c_str());
    return;
  }

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) {
    debugLog("bands: root not object");
    return;
  }

  int n = 0;
  for (JsonPair kv : root) {
    if (n >= kMaxBands) break;
    JsonObject m = kv.value().as<JsonObject>();
    if (m.isNull()) continue;
    const float snr = m["ft8_snr"] | 0.0f;
    g_work.bands[n].band    = kv.key().c_str();
    g_work.bands[n].snr     = snr;
    g_work.bands[n].quality = snrQuality(snr);
    n++;
  }

  // Sort by ascending centre frequency (160m/1.8 MHz first … 10m/28 MHz last).
  for (int i = 0; i < n - 1; ++i) {
    for (int j = 0; j < n - 1 - i; ++j) {
      if (bandFreqKhz(g_work.bands[j].band) > bandFreqKhz(g_work.bands[j + 1].band)) {
        BandCondition tmp = g_work.bands[j];
        g_work.bands[j] = g_work.bands[j + 1];
        g_work.bands[j + 1] = tmp;
      }
    }
  }

  g_work.bandCount = n;
  g_work.bandsValid = (n > 0);
  debugLogf("bands: %d bands", n);
  if (n > 0) g_work.lastSuccessMs = millis();
}

void pollSpace() {
  String body;
  const int code = httpGet("/api/spaceweather", false, body);
  if (code != 200) return;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    debugLogf("space: JSON err %s", err.c_str());
    return;
  }

  g_work.kIndex     = doc["k_index"]  | 0;
  g_work.aIndex     = doc["a_index"]  | 0;
  g_work.solarFlux  = doc["solar_flux"] | 0.0f;
  g_work.propQuality= doc["propagation_quality"] | "";
  g_work.spaceValid = true;
  debugLogf("space: K=%d A=%d flux=%.0f q=%s", g_work.kIndex, g_work.aIndex,
            g_work.solarFlux, g_work.propQuality.c_str());
  g_work.lastSuccessMs = millis();
}

// Title-case a lowercase OWM description ("light rain" → "Light Rain").
String titleCase(const String& s) {
  String out = s;
  bool start = true;
  for (uint16_t i = 0; i < out.length(); ++i) {
    if (out[i] == ' ') { start = true; continue; }
    if (start && out[i] >= 'a' && out[i] <= 'z') out[i] -= 32;
    start = false;
  }
  return out;
}

// Convert a wind bearing in degrees to an 8-point compass string.
String windCompass(int deg) {
  static const char* dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
  const int idx = static_cast<int>((deg + 22) / 45) % 8;
  return String(dirs[idx]);
}

void pollWeather() {
  String body;
  const int code = httpGet("/api/weather", false, body);
  if (code != 200) return;   // 404 = no data cached yet

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    debugLogf("weather: JSON err %s", err.c_str());
    return;
  }

  JsonArray wx = doc["weather"].as<JsonArray>();
  if (wx.isNull() || wx.size() == 0) {
    debugLog("weather: no 'weather' array");
    return;
  }

  g_work.wxDescription = titleCase(String(wx[0]["description"] | ""));
  g_work.wxMain        = wx[0]["main"] | "";
  g_work.wxLocation    = doc["name"] | "";

  g_work.wxTempC    = doc["main"]["temp"]     | 0.0f;
  g_work.wxHumidity = doc["main"]["humidity"] | 0;
  g_work.wxPressure = doc["main"]["pressure"] | 0;

  const float windMs = doc["wind"]["speed"] | 0.0f;
  g_work.wxWindKmh = static_cast<int>(windMs * 3.6f + 0.5f);

  if (doc["wind"]["deg"].is<int>() || doc["wind"]["deg"].is<float>()) {
    g_work.wxWindDir = windCompass(doc["wind"]["deg"].as<int>());
  } else {
    g_work.wxWindDir = "";
  }

  const float gustMs = doc["wind"]["gust"] | 0.0f;
  g_work.wxGustKmh = gustMs > 0 ? static_cast<int>(gustMs * 3.6f + 0.5f) : 0;

  g_work.weatherValid = true;
  debugLogf("weather: %s %.0fC H%d%% %dkm/h",
            g_work.wxDescription.c_str(), g_work.wxTempC,
            g_work.wxHumidity, g_work.wxWindKmh);
  g_work.lastSuccessMs = millis();
}

// Staging buffer keyed by band index (parallel to g_work.bands[]).  Fetched
// spectra accumulate here across poll cycles; each cycle we compact the valid
// ones into g_work.spectrum[] for rendering.  Kept in .bss (file scope) so it
// doesn't consume stack.
BandSpectrum g_specStage[kMaxBands];

// Fetch + downsample one band's FFT into the staging slot g_specStage[idx].
// Returns true if a valid curve was captured.
bool fetchBandSpectrum(int idx) {
  const String band = g_work.bands[idx].band;

  String path = "/api/noisefloor/fft?band=" + band;
  String body;
  const int code = httpGet(path.c_str(), false, body);
  if (code != 200) return false;   // 204 = not ready yet

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    debugLogf("fft %s: JSON err %s", band.c_str(), err.c_str());
    return false;
  }

  JsonArray data = doc["data"].as<JsonArray>();
  if (data.isNull() || data.size() == 0) return false;
  const int n = data.size();

  BandSpectrum& bs = g_specStage[idx];
  bs.band = band;

  // Parse band frequency extents and FT8 marker (if present).
  bs.startFreqMhz = doc["start_freq"].as<float>() / 1e6f;
  bs.endFreqMhz   = doc["end_freq"].as<float>()   / 1e6f;
  bs.ft8FreqMhz   = 0.0f;
  bs.ft8BwMhz     = 0.003f;  // default 3 kHz
  for (JsonVariant m : doc["markers"].as<JsonArray>()) {
    const char* name = m["display_name"] | "";
    if (strcmp(name, "FT8") == 0) {
      bs.ft8FreqMhz = m["frequency"].as<float>() / 1e6f;
      float bw = m["bandwidth"].as<float>();
      if (bw > 0) bs.ft8BwMhz = bw / 1e6f;
      break;
    }
  }

  // Single O(n) pass: per-bucket peak + global dB min/max.
  float peaks[kSpectrumPoints];
  for (int p = 0; p < kSpectrumPoints; ++p) peaks[p] = -1e9f;

  float dbMin = 1e9f, dbMax = -1e9f;
  int i = 0;
  for (JsonVariant v : data) {
    const float d = v.as<float>();
    if (d < dbMin) dbMin = d;
    if (d > dbMax) dbMax = d;
    int p = (int)((int64_t)i * kSpectrumPoints / n);
    if (p >= kSpectrumPoints) p = kSpectrumPoints - 1;
    if (d > peaks[p]) peaks[p] = d;
    i++;
  }
  if (dbMax <= dbMin) dbMax = dbMin + 1.0f;
  bs.dbMin = dbMin;
  bs.dbMax = dbMax;
  const float span = dbMax - dbMin;

  for (int p = 0; p < kSpectrumPoints; ++p) {
    float peak = peaks[p];
    if (peak < -1e8f) peak = dbMin;
    int norm = (int)((peak - dbMin) / span * 255.0f + 0.5f);
    if (norm < 0) norm = 0;
    if (norm > 255) norm = 255;
    bs.pts[p] = (uint8_t)norm;
  }
  bs.valid = true;
  return true;
}

// Spectrum step: fetch a few bands per poll cycle (keeps each step short) into
// the staging buffer, then compact ALL valid staged bands (in band order) into
// g_work.spectrum[] so the slide shows only real data with the correct page
// count.  Staging is keyed by band index, so re-fetching a band updates it in
// place rather than duplicating.
void pollSpectrum() {
  const int nb = g_work.bandCount;
  if (nb <= 0) return;   // no band list yet

  constexpr int kFetchPerCycle = 3;
  for (int k = 0; k < kFetchPerCycle && k < nb; ++k) {
    if (g_spectrumBand >= nb) g_spectrumBand = 0;
    fetchBandSpectrum(g_spectrumBand);
    g_spectrumBand = (g_spectrumBand + 1) % nb;
  }

  // Compact staged → render array, preserving band order (low→high, matching
  // g_work.bands[] which pollBands() already sorted by wavelength).
  int w = 0;
  for (int r = 0; r < nb && w < kMaxBands; ++r) {
    if (g_specStage[r].valid) {
      g_work.spectrum[w] = g_specStage[r];
      w++;
    }
  }
  g_work.spectrumCount = w;
  g_work.spectrumValid = w > 0;

  debugLogf("fft: %d/%d bands have data", w, nb);
  if (w > 0) g_work.lastSuccessMs = millis();
}

// ── PSKReporter rank (/admin/psk-rank?callsign=CALL) ─────────────────────────
// Requires admin auth.  Returns 404 when PSKReporter is not enabled on the
// server, or when the callsign has no data yet.  Both cases are treated as
// "not available" (pskValid stays false).
void pollPskRank() {
  if (g_work.callsign.length() == 0) return;   // need callsign first
  String path = "/admin/psk-rank?callsign=" + g_work.callsign;
  String body;
  const int code = httpGet(path.c_str(), true, body);
  if (code != 200) {
    debugLogf("psk-rank: HTTP %d", code);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;

  JsonObject cr = doc["callsign_rank"];
  if (cr.isNull()) return;

  JsonObject rep = cr["reports"]["All"];
  JsonObject cty = cr["countries"]["All"];

  const int spotsRank = rep["rank"] | 0;
  const int dxccRank  = cty["rank"] | 0;
  if (spotsRank == 0 && dxccRank == 0) return;   // no data for this callsign

  g_work.pskSpotsRank = spotsRank;
  g_work.pskSpotsDay  = rep["day"]  | 0;
  g_work.pskDxccRank  = dxccRank;
  g_work.pskDxccDay   = cty["day"]  | 0;
  g_work.pskValid     = true;
  debugLogf("psk-rank: spots #%d (%d/day) dxcc #%d (%d/day)",
            g_work.pskSpotsRank, g_work.pskSpotsDay,
            g_work.pskDxccRank,  g_work.pskDxccDay);
  g_work.lastSuccessMs = millis();
}

// ── WSPR Live rank (/admin/wspr-rank?callsign=CALL) ──────────────────────────
// Requires admin auth.  Returns 404 when WSPR is not enabled.
// The response has three windows: rolling_24h, today, yesterday.
// Each window's data[] array has 0 or 1 rows (filtered by callsign).
void pollWsprRank() {
  if (g_work.callsign.length() == 0) return;
  String path = "/admin/wspr-rank?callsign=" + g_work.callsign;
  String body;
  const int code = httpGet(path.c_str(), true, body);
  if (code != 200) {
    debugLogf("wspr-rank: HTTP %d", code);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;

  // Helper: extract rank + unique from a window's data[0] row.
  auto readWindow = [&](const char* key, int& rank, int& uniq) {
    JsonArray data = doc[key]["data"].as<JsonArray>();
    if (data.isNull() || data.size() == 0) { rank = 0; uniq = 0; return; }
    JsonObject row = data[0];
    rank = row["original_rank"] | 0;
    if (rank == 0) rank = row["rank"] | 0;   // fallback field name
    uniq = row["unique"] | 0;
  };

  int r24 = 0, u24 = 0, rTod = 0, uTod = 0, rYest = 0, uYest = 0;
  readWindow("rolling_24h", r24,  u24);
  readWindow("today",       rTod, uTod);
  readWindow("yesterday",   rYest, uYest);

  if (r24 == 0 && rTod == 0 && rYest == 0) return;   // no data

  g_work.wsprRank24h     = r24;
  g_work.wsprUnique24h   = u24;
  g_work.wsprRankToday   = rTod;
  g_work.wsprUniqueToday = uTod;
  g_work.wsprRankYest    = rYest;
  g_work.wsprUniqueYest  = uYest;
  g_work.wsprValid       = true;
  debugLogf("wspr-rank: 24h #%d (%d uniq) today #%d (%d uniq)",
            r24, u24, rTod, uTod);
  g_work.lastSuccessMs = millis();
}

// ── GPSDO health (/admin/gpsdo-health) ───────────────────────────────────────
// Requires admin auth.  Returns 404 when GPSDO is not enabled on the server.
// On success, populates all gpsdoXxx fields in the snapshot.
void pollGpsdo() {
  String body;
  const int code = httpGet("/admin/gpsdo-health", true, body);
  if (code != 200) {
    // 404 = GPSDO not configured; clear the valid flag so the slide is hidden.
    if (code == 404) g_work.gpsdoValid = false;
    debugLogf("gpsdo-health: HTTP %d", code);
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    debugLogf("gpsdo-health: JSON err %s", err.c_str());
    return;
  }

  g_work.gpsdoEnabled = doc["enabled"] | false;
  if (!g_work.gpsdoEnabled) {
    g_work.gpsdoValid = false;
    debugLog("gpsdo-health: disabled");
    return;
  }

  g_work.gpsdoHealthy = doc["healthy"] | false;

  JsonObject dev = doc["device_status"].as<JsonObject>();
  if (!dev.isNull()) {
    g_work.gpsdoGpsLock       = dev["gps_lock"]        | false;
    g_work.gpsdoPllLock       = dev["pll_lock"]        | false;
    g_work.gpsdoAntennaOk     = dev["antenna_ok"]      | false;
    g_work.gpsdoOutput1Enabled= dev["output1_enabled"] | false;
    g_work.gpsdoMode          = dev["mode"]            | "";
    g_work.gpsdoFreqHz        = dev["frequency_hz"]    | (uint32_t)0;
  }

  JsonObject gps = doc["gps"].as<JsonObject>();
  if (!gps.isNull()) {
    g_work.gpsdoFix        = gps["fix"]         | "";
    g_work.gpsdoFixMode    = gps["fix_mode"]    | "";
    g_work.gpsdoSatsUsed   = gps["sats_used"]   | 0;
    g_work.gpsdoGpsInView  = gps["gps_in_view"] | 0;
    g_work.gpsdoGloInView  = gps["glo_in_view"] | 0;
    g_work.gpsdoHdop       = gps["hdop"]        | 0.0f;
    g_work.gpsdoAltitudeM  = gps["altitude_m"]  | 0.0f;
    g_work.gpsdoUtc        = gps["datetime_utc"] | "";
  }

  g_work.gpsdoValid = true;
  debugLogf("gpsdo-health: healthy=%d gps=%d pll=%d fix=%s/%s sats=%d hdop=%.2f",
            g_work.gpsdoHealthy ? 1 : 0,
            g_work.gpsdoGpsLock ? 1 : 0,
            g_work.gpsdoPllLock ? 1 : 0,
            g_work.gpsdoFix.c_str(), g_work.gpsdoFixMode.c_str(),
            g_work.gpsdoSatsUsed, g_work.gpsdoHdop);
  g_work.lastSuccessMs = millis();
}

// ── RBN skimmer rank (/admin/rbn-data?callsign=CW_SKIMMER_CALLSIGN) ──────────
// Requires admin auth.  Returns 404 when CW skimmer is not configured.
// Response: { stats_rank: N, stats_total_skimmers: N, statistics: { spot_count: N } }
void pollRbnRank() {
  if (g_work.cwSkimmerCallsign.length() == 0) return;   // no CW skimmer configured
  String path = "/admin/rbn-data?callsign=" + g_work.cwSkimmerCallsign;
  String body;
  const int code = httpGet(path.c_str(), true, body);
  if (code != 200) {
    debugLogf("rbn-data: HTTP %d", code);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;

  const int rank  = doc["stats_rank"]            | 0;
  const int total = doc["stats_total_skimmers"]  | 0;
  const int spots = doc["statistics"]["spot_count"] | 0;

  if (rank == 0 && total == 0) return;   // no data

  g_work.rbnRank  = rank;
  g_work.rbnTotal = total;
  g_work.rbnSpots = spots;
  g_work.rbnValid = true;
  debugLogf("rbn-data: rank #%d / %d skimmers, %d spots",
            rank, total, spots);
  g_work.lastSuccessMs = millis();
}

// ── Monitor health (/admin/monitor-health) ───────────────────────────────────
// Requires admin auth.  Parses the overall status, per-item name+status, and
// up to kMaxHealthIssues issue strings per item.
void pollHealth() {
  String body;
  const int code = httpGet("/admin/monitor-health", true, body);
  // The server may return non-200 codes to signal degraded health
  // (e.g. 207 Multi-Status, 503 Service Unavailable).  Accept any response
  // that has a body worth parsing; only bail on connection failures or
  // definitive "not available" codes.
  if (code < 0 || code == 401 || code == 403 || code == 404) {
    debugLogf("monitor-health: HTTP %d (skip)", code);
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    debugLogf("monitor-health: JSON err %s", err.c_str());
    return;
  }

  // Parse overall status string → uint8_t (0=ok, 1=warning, 2=critical).
  auto parseStatus = [](const char* s) -> uint8_t {
    if (!s) return 0;
    if (strcmp(s, "critical") == 0) return 2;
    if (strcmp(s, "warning")  == 0) return 1;
    return 0;
  };

  g_work.healthOverall = parseStatus(doc["overall"] | "ok");

  JsonArray items = doc["items"].as<JsonArray>();
  int n = 0;
  if (!items.isNull()) {
    for (JsonObject item : items) {
      if (n >= kMaxHealthItems) break;
      const char* name = item["name"] | "";
      const char* stat = item["status"] | "ok";
      // Truncate name to fit the fixed char[20] buffer (19 chars + NUL).
      strncpy(g_work.healthItems[n].name, name,
              sizeof(g_work.healthItems[n].name) - 1);
      g_work.healthItems[n].name[sizeof(g_work.healthItems[n].name) - 1] = '\0';
      g_work.healthItems[n].status = parseStatus(stat);

      // Parse issues[] array — store up to kMaxHealthIssues strings.
      int ni = 0;
      JsonArray issues = item["issues"].as<JsonArray>();
      if (!issues.isNull()) {
        for (JsonVariant v : issues) {
          if (ni >= kMaxHealthIssues) break;
          g_work.healthItems[n].issues[ni] = v.as<const char*>();
          ni++;
        }
      }
      g_work.healthItems[n].issueCount = ni;
      n++;
    }
  }
  g_work.healthItemCount = n;
  g_work.healthValid = true;
  debugLogf("monitor-health: overall=%d items=%d", g_work.healthOverall, n);
  g_work.lastSuccessMs = millis();
}

void runStep(uint8_t step) {
  switch (step) {
    case kStepDescription: pollDescription(); break;
    case kStepSessions:    pollSessions();    break;
    case kStepLoad:        pollLoad();        break;
    case kStepBands:       pollBands();       break;
    case kStepSpace:       pollSpace();       break;
    case kStepWeather:     pollWeather();     break;
    case kStepSpectrum:    pollSpectrum();    break;
    case kStepPskRank:     pollPskRank();     break;
    case kStepWsprRank:    pollWsprRank();    break;
    case kStepRbnRank:     pollRbnRank();     break;
    case kStepGpsdo:       pollGpsdo();       break;
    case kStepHealth:      pollHealth();      break;
    default: break;
  }
}

// ── FreeRTOS task ─────────────────────────────────────────────────────────────
// Runs on Core 0.  Fires one HTTP step per kStepIntervalMs, then yields via
// vTaskDelay so the WiFi/TCP stack (also Core 0) gets CPU time.
void apiTask(void* /*param*/) {
  constexpr uint32_t kHwmLogMs = 30000;
  uint32_t lastHwmMs = 0;

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      // Fast-poll: if the display has requested an extra sessions fetch (e.g.
      // user paused on the Users slide), run pollSessions() as a bonus step
      // without advancing g_step so the normal round-robin is not disrupted.
      // The flag is cleared here; the display sets it again periodically.
      if (g_sessionsFastPoll) {
        g_sessionsFastPoll = false;
        pollSessions();
        xSemaphoreTake(g_snapMutex, portMAX_DELAY);
        g_snap = g_work;
        xSemaphoreGive(g_snapMutex);
        vTaskDelay(pdMS_TO_TICKS(kStepIntervalMs));
        continue;   // go back to top; normal step fires on next iteration
      }

      const bool wasForced = g_forceRefresh;
      g_forceRefresh = false;
      runStep(g_step);   // HTTP fetch — writes to g_work, no mutex held
      g_step = (g_step + 1) % kNumSteps;
      const uint32_t successMs = g_work.lastSuccessMs;

      // Commit the updated working snapshot to the published copy under mutex.
      // This is the only section that holds the mutex, so Core 1 is never
      // blocked for more than a memcpy (~1 µs).
      xSemaphoreTake(g_snapMutex, portMAX_DELAY);
      g_snap = g_work;
      xSemaphoreGive(g_snapMutex);

      // Reachability check — outside mutex, only touches task-local state and
      // the two volatile flags (written atomically, read by Core 1).
      if (successMs != g_lastKnownSuccessMs) {
        if (g_apiWasDown) {
          g_apiWentUp = true;
          debugLog("api: recovered");
        }
        g_failStreak         = 0;
        g_apiWasDown         = false;
        g_lastKnownSuccessMs = successMs;
      } else {
        if (g_failStreak < kFailThreshold) ++g_failStreak;
        if (g_failStreak >= kFailThreshold && !g_apiWasDown) {
          g_apiWentDown = true;
          debugLog("api: unreachable");
          g_apiWasDown  = true;
        }
      }

      // Periodic HWM log.
      const uint32_t nowMs = millis();
      if (nowMs - lastHwmMs >= kHwmLogMs) {
        lastHwmMs = nowMs;
        const uint32_t hwm =
            static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr))
            * sizeof(StackType_t);
        g_apiTaskHwm = hwm;
        debugLogf("api task stack HWM: %u bytes free", hwm);
      }

      if (wasForced) continue;   // skip delay when a refresh was forced
    }

    vTaskDelay(pdMS_TO_TICKS(kStepIntervalMs));
  }
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void ubersdrApiBegin() {
  if (!g_snapMutex) g_snapMutex = xSemaphoreCreateMutex();

  // Task not yet started — safe to reset both buffers without the mutex.
  g_work = UberSDRSnapshot{};
  g_work.maxSessions = 20;   // sensible default until /api/description responds
  g_snap = g_work;

  g_step               = 0;
  g_forceRefresh       = true;
  g_failStreak         = 0;
  g_apiWasDown         = false;
  g_lastKnownSuccessMs = 0;
  g_apiWentDown        = false;
  g_apiWentUp          = false;

  const AppSettings& cfg = getSettings();
  debugLogf("ubersdr api begin: host=%s:%u pw=%s",
            cfg.ubersdrHost.length() ? cfg.ubersdrHost.c_str() : "(none)",
            cfg.ubersdrPort,
            cfg.ubersdrPassword.length() ? "set" : "empty");
}

void ubersdrApiTaskBegin() {
  xTaskCreatePinnedToCore(
    apiTask,           // task function
    "ubersdr_api",     // name (visible in /debug/tasks)
    12288,             // stack bytes — TLS + JSON parsing are stack-hungry
    nullptr,           // parameter
    1,                 // priority (same as Arduino loop task)
    &g_apiTaskHandle,  // handle
    0                  // Core 0 (WiFi/TCP stack also lives here)
  );
  debugLog("ubersdr api task started on Core 0");
}

UberSDRSnapshot getUberSDRSnapshot() {
  xSemaphoreTake(g_snapMutex, portMAX_DELAY);
  const UberSDRSnapshot copy = g_snap;
  xSemaphoreGive(g_snapMutex);
  return copy;
}

void ubersdrApiGetHealth(bool& healthValid, uint8_t& healthOverall) {
  xSemaphoreTake(g_snapMutex, portMAX_DELAY);
  healthValid   = g_snap.healthValid;
  healthOverall = g_snap.healthOverall;
  xSemaphoreGive(g_snapMutex);
}

void ubersdrApiRefresh() {
  g_step        = 0;
  g_forceRefresh = true;
}

// Jump the poll queue so the sessions endpoint fires on the very next tick.
// Used when the user pauses on the Users slide so data refreshes within ~2 s
// instead of waiting up to 24 s for the normal round-robin to reach it.
void ubersdrApiPrioritiseSessions() {
  g_step         = kStepSessions;
  g_forceRefresh = true;   // skip the inter-step delay
}

// Request an extra sessions poll without disrupting the normal round-robin.
void ubersdrApiRequestSessionsFastPoll() {
  g_sessionsFastPoll = true;
}

uint32_t ubersdrApiGetStackHwm() {
  return g_apiTaskHwm;
}
