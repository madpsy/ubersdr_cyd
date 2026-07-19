#pragma once

#include <Arduino.h>

// ── UberSDR API client ────────────────────────────────────────────────────────
//
// Polls the UberSDR Go server's HTTP endpoints on a timer and exposes the
// results via a shared snapshot struct.  All network I/O happens in
// ubersdrApiLoop() which must be called every loop() iteration.
//
// Endpoints polled (host/port/password come from settings.h — data/ubersdr.json
// on LittleFS, NVS via the web portal, or app_config.h defaults, in that order):
//   GET /api/description            (public)           — capacity, callsign, timezone
//   GET /admin/sessions?compact=1   (X-Admin-Password) — user counts + throughput
//   GET /admin/system-load          (X-Admin-Password) — load averages + CPU temp
//   GET /api/noisefloor/latest      (public)           — per-band FT8 SNR
//   GET /api/noisefloor/fft?band=…  (public)           — per-band spectrum charts
//   GET /api/spaceweather           (public)           — K/A index, solar flux
//   GET /api/weather                (public)           — terrestrial weather
//   GET /admin/psk-rank             (X-Admin-Password) — PSKReporter rank
//   GET /admin/wspr-rank            (X-Admin-Password) — WSPR Live rank
//   GET /admin/rbn-data             (X-Admin-Password) — RBN skimmer rank
//
// Call order:
//   ubersdrApiBegin()  — once, in setup()
//   ubersdrApiLoop()   — every loop() iteration (drives the poll timer)

// Maximum number of amateur bands tracked for the band-conditions slide.
constexpr int kMaxBands = 12;

// Maximum number of non-bypassed user details stored from the compact sessions
// response (non_bypassed_users map).  Matches the health dot-grid density
// (2 columns × 8 rows = 16 entries visible on one page).
constexpr int kMaxUserDetails = 16;

// Per-user detail parsed from the non_bypassed_users map in the compact
// /admin/sessions?compact=1 response.
struct UserDetail {
  char     country[28];      // e.g. "United Kingdom" (truncated, null-terminated)
  char     countryCode[4];   // e.g. "GB" (null-terminated; empty when absent)
  time_t   connectedSince;   // UTC epoch parsed from connected_since ISO-8601 string
  uint32_t frequencyHz;      // audio frequency in Hz (0 when spectrum-only / no audio)
  char     mode[8];          // e.g. "USB", "LSB", "CW" (uppercased; empty when absent)
};

// Maximum number of health items stored from /admin/monitor-health.
constexpr int kMaxHealthItems = 24;

// Maximum issues stored per health item (extra issues are dropped).
constexpr int kMaxHealthIssues = 4;

// A single component health item.
struct HealthItem {
  char     name[20];                        // truncated display name (null-terminated)
  uint8_t  status;                          // 0 = ok, 1 = warning, 2 = critical
  String   issues[kMaxHealthIssues];        // issue strings (may be empty)
  int      issueCount;                      // number of populated issues[] entries
};

// Number of downsampled points stored per band for the spectrum slide.  Each
// mini-chart is ~150 px wide; 128 points gives good detail at tiny RAM cost.
constexpr int kSpectrumPoints = 128;

struct BandCondition {
  String band;      // e.g. "20m"
  float  snr;       // FT8 SNR in dB
  String quality;   // "EXCELLENT" / "GOOD" / "FAIR" / "POOR"
};

// A single band's downsampled spectrum, normalised to 0..255 for compact
// storage (0 = dbMin, 255 = dbMax).  Rendered as a filled area plot.
struct BandSpectrum {
  String  band;                     // e.g. "20m"
  bool    valid;                    // true once FFT data has been captured
  uint8_t pts[kSpectrumPoints];     // normalised magnitude, 0..255
  float   dbMin;                    // dB value mapped to 0
  float   dbMax;                    // dB value mapped to 255
  float   startFreqMhz;             // band start frequency in MHz
  float   endFreqMhz;               // band end frequency in MHz
  float   ft8FreqMhz;               // FT8 centre frequency in MHz (0 = none)
  float   ft8BwMhz;                 // FT8 bandwidth in MHz (typically 0.003)
};

// Aggregated snapshot of all polled UberSDR metrics.
// Each section carries its own validity flag so a slide can render "no data"
// independently when one endpoint fails while others succeed.
struct UberSDRSnapshot {
  // ── Users (/admin/sessions?compact=1; userCount/maxSessions also derived
  //    from /api/description max_clients − available_clients) ──
  bool usersValid;
  int  userCount;      // unique non-bypassed active users
  int  bypassCount;    // unique bypassed users
  int  maxSessions;    // configured capacity

  // Per-user detail from non_bypassed_users map (compact endpoint only).
  // Populated when the server returns the map; userDetailCount == 0 otherwise.
  UserDetail userDetails[kMaxUserDetails];
  int        userDetailCount;   // 0..kMaxUserDetails

  // ── Network stats (/admin/sessions?compact=1 only; netValid false when the
  //    server predates the compact parameter) ──
  bool netValid;
  int  externalSessions;   // non-internal session count
  int  audioKbps;          // summed instantaneous audio throughput
  int  waterfallKbps;      // summed instantaneous waterfall throughput
  int  totalKbps;          // audio + waterfall

  // ── Instance identity / timezone (/api/description → receiver.*) ──
  String callsign;           // receiver.callsign (shown in the header)
  String cwSkimmerCallsign;  // cw_skimmer_callsign (for RBN rank lookup)
  bool tzValid;
  int  tzOffsetMinutes;   // DST-adjusted offset from UTC, in minutes

  // ── Antenna switch + rotator (/api/description) ──
  bool   antSwitchEnabled;    // ant_switch present + enabled
  bool   antSwitchGrounded;   // ant_switch.grounded
  String antSwitchLabels;     // comma-joined ant_switch.active_labels

  bool   rotatorEnabled;      // rotator.enabled
  bool   rotatorConnected;    // rotator.connected
  int    rotatorAzimuth;      // rotator.azimuth in whole degrees (-1 = unknown)

  // ── System load (/admin/system-load) ──
  bool   loadValid;
  float  load1, load5, load15;
  int    cpuCores;
  bool   cpuTempAvailable;
  float  cpuTempC;
  float  cpuTempThresholdC;
  String cpuTempStatus;   // "ok" / "warning" / "critical"

  // ── Band conditions (/api/noisefloor/latest) ──
  bool          bandsValid;
  BandCondition bands[kMaxBands];
  int           bandCount;

  // ── Per-band spectrum (/api/noisefloor/fft?band=…) ──
  bool          spectrumValid;             // at least one band captured
  BandSpectrum  spectrum[kMaxBands];       // parallel to bands[] order
  int           spectrumCount;

  // ── Rankings (/admin/psk-rank, /admin/wspr-rank, /admin/rbn-data) ──
  bool pskValid;
  int  pskSpotsRank;    // PSKReporter spots "All" rank (0 = not available)
  int  pskSpotsDay;     // spots today
  int  pskDxccRank;     // PSKReporter countries "All" rank (0 = not available)
  int  pskDxccDay;      // countries today
  bool wsprValid;
  int  wsprRank24h;     // WSPR Live rolling-24h rank (0 = not available)
  int  wsprUnique24h;   // unique spots in 24h
  int  wsprRankToday;   // WSPR Live today rank (0 = not available)
  int  wsprUniqueToday; // unique spots today
  int  wsprRankYest;    // WSPR Live yesterday rank (0 = not available)
  int  wsprUniqueYest;  // unique spots yesterday
  bool rbnValid;
  int  rbnRank;         // RBN skimmer rank (0 = not available)
  int  rbnTotal;        // total skimmers in RBN dataset
  int  rbnSpots;        // spot count for today

  // ── Space weather (/api/spaceweather) ──
  bool   spaceValid;
  int    kIndex;
  int    aIndex;
  float  solarFlux;
  String propQuality;   // "Excellent" / "Good" / "Fair" / "Poor"

  // ── Terrestrial weather (/api/weather — OpenWeatherMap shape) ──
  bool   weatherValid;
  String wxDescription; // e.g. "light rain" (title-cased for display)
  String wxLocation;    // OWM "name"
  String wxMain;        // e.g. "Rain" / "Clear" — used for colour
  float  wxTempC;
  int    wxHumidity;    // %
  int    wxPressure;    // hPa
  int    wxWindKmh;     // rounded km/h
  String wxWindDir;     // compass, e.g. "NW" (empty when no bearing)
  int    wxGustKmh;     // rounded km/h (0 when absent)

  // ── GPSDO (/admin/gpsdo-health) ──
  bool     gpsdoValid;
  bool     gpsdoEnabled;
  bool     gpsdoHealthy;
  bool     gpsdoGpsLock;
  bool     gpsdoPllLock;
  bool     gpsdoAntennaOk;
  bool     gpsdoOutput1Enabled;
  String   gpsdoMode;          // "PLL" / "OCXO" etc.
  uint32_t gpsdoFreqHz;        // reference oscillator frequency in Hz
  String   gpsdoFix;           // "GPS" / "GNSS" / "None" etc.
  String   gpsdoFixMode;       // "3D" / "2D" / "None"
  int      gpsdoSatsUsed;
  int      gpsdoGpsInView;
  int      gpsdoGloInView;
  float    gpsdoHdop;
  float    gpsdoAltitudeM;
  String   gpsdoUtc;           // ISO-8601 datetime from GPS, e.g. "2026-07-18T11:36:05Z"

  // ── Monitor health (/admin/monitor-health) ──
  bool       healthValid;
  uint8_t    healthOverall;              // 0=ok, 1=warning, 2=critical
  HealthItem healthItems[kMaxHealthItems];
  int        healthItemCount;

  // ── Meta ──
  uint32_t lastSuccessMs;   // millis() of the most recent successful fetch (any)
};

// Initialise the API client (records config, resets snapshot).
// Call once in setup() before ubersdrApiTaskBegin().
void ubersdrApiBegin();

// Create the FreeRTOS background task that polls UberSDR endpoints on Core 0,
// keeping the display loop on Core 1 free of HTTP blocking.
// Call once in setup(), after ubersdrApiBegin().
void ubersdrApiTaskBegin();

// Returns a thread-safe copy of the latest aggregated snapshot.
// NOTE: UberSDRSnapshot is large (several KB); avoid placing it on the Arduino
// loopTask stack.  Prefer ubersdrApiGetHealth() for LED/status use cases.
UberSDRSnapshot getUberSDRSnapshot();

// Lightweight accessor — reads only the two health fields under the mutex.
// Use this instead of getUberSDRSnapshot() when you only need health status,
// to avoid a large stack allocation in the loopTask.
void ubersdrApiGetHealth(bool& healthValid, uint8_t& healthOverall);

// Force an immediate re-poll on the next task iteration (e.g. after WiFi reconnects).
void ubersdrApiRefresh();

// Jump the poll queue so the sessions endpoint fires on the very next tick.
// Call when the user pauses on the Users slide so user data refreshes within
// ~2 s instead of waiting up to kNumSteps * kStepIntervalMs for the normal
// round-robin to reach it.
void ubersdrApiPrioritiseSessions();

// Request an extra sessions poll on the next task tick without disrupting the
// normal round-robin.  Safe to call repeatedly (e.g. every 4 s while paused
// on the Users slide) — the task runs pollSessions() as a bonus step, then
// resumes the normal sequence so health and other endpoints are not starved.
void ubersdrApiRequestSessionsFastPoll();

// Returns the FreeRTOS stack high-water mark for the API task in bytes free.
// Returns 0 before the task has started.
uint32_t ubersdrApiGetStackHwm();

// ── Reachability transition flags ─────────────────────────────────────────────
// Set by the API task (Core 0); consumed by the display loop (Core 1) which
// calls notificationsPush() itself so notifications stay on one core.
// The display loop must clear each flag after consuming it.
extern volatile bool g_apiWentDown;   // true on the down-transition
extern volatile bool g_apiWentUp;     // true on the up-transition (recovery)
