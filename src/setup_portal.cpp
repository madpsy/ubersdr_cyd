#include "setup_portal.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "connectivity.h"
#include "debug_log.h"
#include "display.h"
#include "notifications.h"
#include "settings.h"
#include "ubersdr_api.h"

namespace {
constexpr byte kDnsPort = 53;
constexpr char kApSsid[] = "UberSDR-Setup";
constexpr char kApPassword[] = "ubersdr1";

// How long the STA must be confirmed connected before the hotspot auto-shuts.
constexpr uint32_t kApAutoOffConfirmMs = 8000;

DNSServer dnsServer;
WebServer server(80);
bool portalStarted = false;
bool mdnsStarted = false;
bool pendingWifiReconnect = false;
uint32_t pendingWifiReconnectAtMs = 0;
bool pendingReboot = false;
uint32_t pendingRebootAtMs = 0;
bool hotspotActive = false;
uint32_t staConfirmedSinceMs = 0;

// ── helpers ──────────────────────────────────────────────────────────────────

void startHotspot() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(kApSsid, kApPassword);
  delay(100);
  dnsServer.start(kDnsPort, "*", WiFi.softAPIP());
  hotspotActive = true;
  staConfirmedSinceMs = 0;
  Serial.println("Setup hotspot ON  SSID: " + String(kApSsid));
}

void stopHotspot() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  hotspotActive = false;
  Serial.println("Setup hotspot OFF (Wi-Fi confirmed)");
}

bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

String htmlEscape(const String& input) {
  String out;
  out.reserve(input.length());
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if      (c == '&')  out += F("&amp;");
    else if (c == '<')  out += F("&lt;");
    else if (c == '>')  out += F("&gt;");
    else if (c == '"')  out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else                out += c;
  }
  return out;
}

String checked(bool value) {
  return value ? F(" checked") : F("");
}

String limitedArg(const char* name, size_t maxLen, bool trimWhitespace = true) {
  String value = server.arg(name);
  if (trimWhitespace) value.trim();
  if (value.length() > maxLen) value = value.substring(0, maxLen);
  return value;
}

String uptimeText(uint32_t seconds) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu",
           static_cast<unsigned long>(seconds / 3600),
           static_cast<unsigned long>((seconds % 3600) / 60),
           static_cast<unsigned long>(seconds % 60));
  return String(buf);
}

// ── HTML page ─────────────────────────────────────────────────────────────────

String pageHtml(const String& message = "") {
  const AppSettings& settings = getSettings();
  const SystemSnapshot snap = getSystemSnapshot();

  String html;
  html.reserve(5000);

  html += F("<!doctype html><html><head>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>UberSDR Setup</title><style>");
  html += F("body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;"
            "margin:0;background:#10151c;color:#f3f7fb}");
  html += F("main{max-width:640px;margin:0 auto;padding:24px}");
  html += F("h1{margin-bottom:4px}");
  html += F("label{display:block;margin:14px 0 6px;color:#aeb8c4}");
  html += F("input,select{box-sizing:border-box;width:100%;padding:11px;"
            "border-radius:6px;border:1px solid #3a4653;background:#18212b;"
            "color:#fff;font-size:16px}");
  html += F("input[type=checkbox]{width:auto;margin-right:8px}");
  html += F("input[type=range]{padding:4px}");
  html += F("button{margin-top:18px;margin-right:8px;padding:12px 16px;"
            "border:0;border-radius:6px;background:#1aa7c8;color:#001018;"
            "font-weight:700;font-size:16px;cursor:pointer}");
  html += F(".danger{background:#ffbd66}");
  html += F(".card{border:1px solid #293440;border-radius:8px;padding:16px;"
            "margin:16px 0;background:#141b24}");
  html += F(".ok{color:#71e58d}.warn{color:#ffbd66}");
  html += F("code{background:#202b36;padding:2px 5px;border-radius:4px}");
  html += F("small{color:#aeb8c4}");
  html += F("</style></head><body><main>");

  html += F("<h1>UberSDR Setup</h1>");

  if (message.length() > 0) {
    html += F("<div class='card ok'>");
    html += htmlEscape(message);
    html += F("</div>");
  }

  // ── Status card ──
  html += F("<div class='card'>");
  html += F("<div>Wi-Fi: <strong class='");
  html += snap.wifiConnected ? F("ok'>connected") : F("warn'>not connected");
  html += F("</strong></div>");
  html += F("<div>IP: <code>");
  html += htmlEscape(snap.localIp);
  html += F("</code></div>");
  html += F("<div>NTP: <strong class='");
  html += snap.timeValid ? F("ok'>synced") : F("warn'>waiting");
  html += F("</strong></div>");
  html += F("<div>mDNS: <code>");
  html += mdnsStarted ? F("ubersdr-cyd.local") : F("--");
  html += F("</code></div>");
  html += F("<div>Uptime: <code>");
  html += uptimeText(snap.uptimeSeconds);
  html += F("</code></div>");
  html += F("<div>Free heap: <code>");
  html += String(ESP.getFreeHeap());
  html += F("</code></div>");
  html += F("<div>Setup AP: <code>");
  html += kApSsid;
  html += F("</code> / <code>");
  html += kApPassword;
  html += F("</code></div>");
  html += F("<div>Hotspot: <strong class='");
  html += hotspotActive ? F("warn'>on") : F("ok'>off");
  html += F("</strong></div>");
  html += F("<div><a href='/debug' style='color:#1aa7c8'>View debug log &raquo;</a>"
            " &nbsp; <a href='/debug/tasks' style='color:#1aa7c8'>Task info &raquo;</a></div>");
  html += F("</div>");

  // ── Settings form ──
  html += F("<form method='post' action='/save'>");

  html += F("<div class='card'><h2>Wi-Fi</h2>");
  html += F("<label for='ssid'>SSID</label>"
            "<input id='ssid' name='ssid' autocomplete='off' value='");
  html += htmlEscape(settings.wifiSsid);
  html += F("'>");
  html += F("<label for='pass'>Password</label>"
            "<input id='pass' name='pass' type='password' maxlength='64' "
            "autocomplete='new-password' placeholder='Leave blank to keep saved password'>");
  html += F("<label><input name='clearpass' type='checkbox' value='1'>"
            "Clear saved password (open network)</label>");
  html += F("<label><input name='keepap' type='checkbox' value='1'");
  html += checked(settings.keepHotspotOn);
  html += F(">Keep setup hotspot always on</label>"
            "<small>By default the hotspot turns off once the device joins your "
            "Wi-Fi. Tick this to leave it running.</small>");
  html += F("</div>");

  html += F("<div class='card'><h2>UberSDR Server</h2>");
  html += F("<label for='ushost'>Host / IP</label>"
            "<input id='ushost' name='ushost' autocomplete='off' "
            "placeholder='ubersdr.local' value='");
  html += htmlEscape(settings.ubersdrHost);
  html += F("'>");
  html += F("<label for='usport'>Port</label>"
            "<input id='usport' name='usport' type='number' min='1' max='65535' value='");
  html += String(settings.ubersdrPort);
  html += F("'>");
  html += F("<label><input name='ustls' type='checkbox' value='1'");
  html += checked(settings.ubersdrTls);
  html += F(">Use HTTPS (TLS, no certificate validation)</label>");
  html += F("<label for='uspass'>Admin password</label>"
            "<input id='uspass' name='uspass' type='password' maxlength='64' "
            "autocomplete='new-password' placeholder='Leave blank to keep saved password'>");
  html += F("<label><input name='clearuspass' type='checkbox' value='1'>"
            "Clear saved admin password</label>");
  html += F("<small>Sent only as the <code>X-Admin-Password</code> header to the "
            "host above. Used for the overview slideshow.</small>");
  html += F("</div>");

  html += F("<div class='card'><h2>Display</h2>");
  html += F("<label for='bright'>Backlight brightness: "
            "<span id='bval'>");
  html += String(settings.brightnessPercent);
  html += F("%</span></label>"
            "<input id='bright' name='bright' type='range' min='5' max='100' value='");
  html += String(settings.brightnessPercent);
  html += F("' oninput=\"document.getElementById('bval').textContent=this.value+'%'\">");
  html += F("</div>");

  html += F("<button type='submit'>Save settings</button>");
  html += F("</form>");

  html += F("<form method='post' action='/reboot'>"
            "<button class='danger' type='submit'>Restart device</button>"
            "</form>");

  html += F("<p><small>UberSDR CYD &mdash; settings page. "
            "No authentication is configured.</small></p>");
  html += F("</main></body></html>");
  return html;
}

// ── HTTP handlers ─────────────────────────────────────────────────────────────

void handleRoot() {
  String message;
  if (server.hasArg("saved"))    message = "Settings saved.";
  if (server.hasArg("rebooting")) message = "Restarting — back shortly.";
  server.send(200, "text/html", pageHtml(message));
}

void handleSave() {
  const AppSettings previous = getSettings();
  AppSettings settings = previous;

  settings.wifiSsid = limitedArg("ssid", 64, false);
  const String submittedPass = limitedArg("pass", 64, false);
  if (server.hasArg("clearpass")) {
    settings.wifiPassword = "";
  } else if (submittedPass.length() > 0) {
    settings.wifiPassword = submittedPass;
  }
  settings.keepHotspotOn = server.hasArg("keepap");
  settings.brightnessPercent = static_cast<uint8_t>(
      constrain(server.arg("bright").toInt(), 5L, 100L));

  // ── UberSDR server ──
  settings.ubersdrHost = limitedArg("ushost", 64);
  const long submittedPort = server.arg("usport").toInt();
  if (submittedPort >= 1 && submittedPort <= 65535) {
    settings.ubersdrPort = static_cast<uint16_t>(submittedPort);
  }
  settings.ubersdrTls = server.hasArg("ustls");
  const String submittedUsPass = limitedArg("uspass", 64, false);
  if (server.hasArg("clearuspass")) {
    settings.ubersdrPassword = "";
  } else if (submittedUsPass.length() > 0) {
    settings.ubersdrPassword = submittedUsPass;
  }

  saveSettings(settings);
  applyDisplaySettings();

  if (settings.wifiSsid != previous.wifiSsid ||
      settings.wifiPassword != previous.wifiPassword) {
    pendingWifiReconnect = true;
    pendingWifiReconnectAtMs = millis() + 1500;
  }

  server.sendHeader("Location", "/?saved=1", true);
  server.send(303, "text/plain", "Saved");
}

void handleReboot() {
  pendingReboot = true;
  pendingRebootAtMs = millis() + 1500;
  server.sendHeader("Location", "/?rebooting=1", true);
  server.send(303, "text/plain", "Restarting");
}

void handleRebootGet() {
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void handleDebug() {
  // Plain-text dump of the recent debug ring buffer.  Auto-refreshes via a
  // small header hint so a browser tab shows live-ish output.
  server.sendHeader("Refresh", "3");
  server.send(200, "text/plain; charset=utf-8", debugLogDump());
}

void handleDebugTasks() {
  // Reports the API background task stack high-water mark.
  // uxTaskGetSystemState() requires configUSE_TRACE_FACILITY=1 which is not
  // enabled in the Arduino-ESP32 framework defaults, so we report only the
  // values we can obtain without it.
  String json;
  json.reserve(128);
  json = "{\"api_task_hwm_bytes\":";
  json += String(ubersdrApiGetStackHwm());
  json += ",\"free_heap_bytes\":";
  json += String(ESP.getFreeHeap());
  json += ",\"min_free_heap_bytes\":";
  json += String(ESP.getMinFreeHeap());
  json += "}";
  server.send(200, "application/json", json);
}

// POST/GET /notify — webhook receiver for UberSDR's Generic-webhook
// notification channel (LAN only, no auth).  Accepts:
//   - text/plain body: the message itself             (webhook_format: text)
//   - JSON {channel, message, event, rule, timestamp} (webhook_format: json)
//   - JSON {"text": …} / {"content": …}               (slack / discord formats)
//   - ?msg=… query parameter                          (manual testing via GET)
// Toast title preference: rule name (free text) > event type > channel name.
void handleNotify() {
  String body = server.arg("plain");   // raw POST body
  if (body.length() == 0) body = server.arg("msg");
  if (body.length() > 2048) {
    server.send(413, "text/plain", "too large");
    return;
  }

  String title, message;
  String trimmed = body;
  trimmed.trim();
  if (trimmed.startsWith("{")) {
    JsonDocument doc;
    if (deserializeJson(doc, trimmed) == DeserializationError::Ok) {
      title = doc["rule"] | "";
      if (title.length() == 0) {
        // Event type is a snake_case identifier — make it readable.
        title = doc["event"] | "";
        title.replace('_', ' ');
      }
      if (title.length() == 0) title = doc["channel"] | "";
      message = doc["message"] | "";
      if (message.length() == 0) message = doc["text"]    | "";   // slack
      if (message.length() == 0) message = doc["content"] | "";   // discord
    }
  }
  if (message.length() == 0) message = trimmed;   // plain text
  if (message.length() == 0) {
    server.send(400, "text/plain", "empty message");
    return;
  }

  notificationsPush(title, message);
  server.send(200, "text/plain", "ok");
}

void handleCaptiveRedirect() {
  if (!hotspotActive) {
    handleRoot();
    return;
  }
  server.sendHeader("Location",
                    String("http://") + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "");
}
}  // namespace

// ── public API ────────────────────────────────────────────────────────────────

void setupPortalBegin() {
  startHotspot();

  server.on("/",                   HTTP_GET,  handleRoot);
  server.on("/save",               HTTP_POST, handleSave);
  server.on("/reboot",             HTTP_POST, handleReboot);
  server.on("/reboot",             HTTP_GET,  handleRebootGet);
  server.on("/debug",              HTTP_GET,  handleDebug);
  server.on("/debug/tasks",        HTTP_GET,  handleDebugTasks);
  server.on("/notify",             HTTP_POST, handleNotify);
  server.on("/notify",             HTTP_GET,  handleNotify);   // curl testing
  // Captive-portal detection endpoints
  server.on("/generate_204",       HTTP_GET,  handleCaptiveRedirect);
  server.on("/gen_204",            HTTP_GET,  handleCaptiveRedirect);
  server.on("/hotspot-detect.html",HTTP_GET,  handleRoot);
  server.on("/ncsi.txt",           HTTP_GET,  []() {
    server.send(200, "text/plain", "Microsoft NCSI");
  });
  server.onNotFound(handleCaptiveRedirect);
  server.begin();
  portalStarted = true;

  Serial.print("Setup portal started — connect to: ");
  Serial.println(kApSsid);
  Serial.print("Portal IP: ");
  Serial.println(WiFi.softAPIP());
}

void setupPortalLoop() {
  if (!portalStarted) return;

  // Start mDNS once STA is connected.
  // Use "ubersdr-cyd" (not "ubersdr") so the display does NOT collide with the
  // UberSDR server, which typically owns ubersdr.local on the same LAN.
  if (!mdnsStarted && WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("ubersdr-cyd")) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.println("mDNS: http://ubersdr-cyd.local/  (debug: /debug)");
    }
  }

  const AppSettings& settings = getSettings();
  const bool staConnected =
      WiFi.status() == WL_CONNECTED &&
      WiFi.localIP() != IPAddress(0, 0, 0, 0);
  const uint32_t nowMs = millis();

  if (settings.keepHotspotOn) {
    staConfirmedSinceMs = 0;
    if (!hotspotActive) startHotspot();
  } else if (staConnected) {
    if (staConfirmedSinceMs == 0) {
      staConfirmedSinceMs = nowMs;
    } else if (hotspotActive &&
               deadlineReached(nowMs, staConfirmedSinceMs + kApAutoOffConfirmMs)) {
      stopHotspot();
    }
  } else {
    staConfirmedSinceMs = 0;
  }

  if (hotspotActive) dnsServer.processNextRequest();
  server.handleClient();

  if (pendingWifiReconnect && deadlineReached(nowMs, pendingWifiReconnectAtMs)) {
    pendingWifiReconnect = false;
    reconnectWifi();
  }
  if (pendingReboot && deadlineReached(nowMs, pendingRebootAtMs)) {
    pendingReboot = false;
    ESP.restart();
  }
}
