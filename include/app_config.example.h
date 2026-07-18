#pragma once

// Copy this file to include/app_config.h and fill in your details.
// Keep app_config.h out of version control (it is listed in .gitignore).

// Optional compile-time Wi-Fi credentials.
// If left as placeholders the device will start with no credentials and you
// must configure Wi-Fi via the on-screen setup hotspot (SSID: UberSDR-Setup).
#define WIFI_SSID     "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

// Backlight brightness 5-100 %
#define DEFAULT_BRIGHTNESS 100

// ── UberSDR server connection (OPTIONAL compile-time defaults) ────────────────
// These are only fall-back defaults.  The overview display prefers, in order:
//   1. data/ubersdr.json on LittleFS  (see data/ubersdr.json.example)
//   2. values saved via the web setup portal (stored in NVS)
//   3. these compile-time defaults
// So the recommended way to configure the server is the setup portal or
// data/ubersdr.json — you can leave these as placeholders.
//
// The admin password is sent only as the X-Admin-Password header to the host
// configured here.  Public endpoints (noisefloor, spaceweather) need no auth.

// Hostname or IP address of the UberSDR Go server (no scheme, no trailing slash).
#define UBERSDR_HOST "ubersdr.local"

// HTTP port the UberSDR server listens on.
#define UBERSDR_PORT 8080

// Admin password — must match config.yaml admin.password on the server.
#define UBERSDR_PASSWORD "your-admin-password"

// Set to true to connect via HTTPS (TLS without certificate validation).
// Useful when the server is behind a reverse proxy with a self-signed cert.
// Leave false for plain HTTP on a local LAN (the typical deployment).
#define UBERSDR_TLS false
