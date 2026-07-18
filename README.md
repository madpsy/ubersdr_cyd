# UberSDR CYD

ESP32-2432S028R ("Cheap Yellow Display") firmware — the display front-end for an UberSDR receiver.

## Hardware

| Board | ESP32-2432S028R (CYD) |
|---|---|
| Display | ILI9341 320×240 TFT (landscape) |
| Touch | XPT2046 (separate HSPI bus) |
| MCU | ESP32 (240 MHz dual-core) |

## Features

- **Overview slideshow** — polls the UberSDR server's HTTP API and cycles through modern, full-colour metric slides on the 320×240 display. Auto-advances every 8 s; tap left/right to step slides manually, tap the centre to pause/resume, tap the top edge to return to the status screen. Slides included:
  - **Users** — current / max connected users, capacity bar, free slots, bypass users and session count, plus a network-throughput card (total / audio / waterfall kbps) when the server supports `/admin/sessions?compact=1`.
  - **System Load** — 1/5/15-minute load averages (colour-coded vs core count) and CPU temperature gauge.
  - **Band Conditions** — colour-coded pill grid of per-band FT8 SNR quality (EXCELLENT/GOOD/FAIR/POOR).
  - **Spectrum** — a grid of live per-band FFT mini-charts (6 per page, each a distinct colour), sorted by ascending centre frequency. Spans multiple pages that share the slide's 8-second window.
  - **Space Weather** — propagation quality banner plus K-index, A-index and solar flux cards.
  - **Weather** — local terrestrial conditions (description, temperature, humidity, pressure, wind) from the instance's location.
  - **Antenna** — live antenna-switch state (active port label or GROUNDED) and a rotator compass dial with azimuth + 16-point bearing. Only shown when the server reports an enabled antenna switch or a connected rotator.
  - **Ranking** — PSKReporter spots/DXCC rank, WSPR Live rank (24h / today / yesterday) and RBN skimmer rank, laid out in 1–3 columns depending on which sources report data.

The header shows the instance callsign, a live **user-count chip** (`1/20`, green normally, amber at capacity), the slide counter, and a clock with the **UberSDR instance's local time** (derived from `receiver.timezone_offset` in `/api/description`), falling back to UTC until that offset is known.
- **Webhook notifications** — the display exposes `POST /notify` (port 80, LAN-only, no auth) for UberSDR's Generic-webhook notification channel. Incoming messages appear as a toast card overlaid on the bottom of the screen for ~6 s each (up to 6 queued, "+N" badge, tap to dismiss; slide auto-advance pauses while a toast is up). The toast title uses the JSON payload's `rule` name when present, then `event`, then `channel`. Accepts `webhook_format: text` or `json` (also Slack/Discord shapes), plus `?msg=…` on GET for testing:
  ```bash
  curl "http://ubersdr-cyd.local/notify?msg=hello"
  ```
  On the server, add a webhook notification channel with `webhook_url: http://<display-ip>/notify` and `webhook_format: json` (use the IP — Go's resolver usually won't do mDNS `.local`).
- **On-screen WiFi setup** — tap the screen to open a touch keyboard and enter your SSID and password directly on the display. No phone or laptop needed.
- **Setup hotspot** — the device also broadcasts `UberSDR-Setup` (password `ubersdr1`) so you can configure WiFi from a browser at `192.168.4.1` or `http://ubersdr-cyd.local/` once connected. (mDNS name is `ubersdr-cyd` to avoid clashing with the UberSDR server's own `ubersdr.local`.)
- **Debug log** — a rolling 100-line log of API polls and results is served at `http://<display-ip>/debug` (or `http://ubersdr-cyd.local/debug`), auto-refreshing every 3 seconds.
- **NVS persistence** — credentials and brightness are stored in ESP32 NVS flash and survive reboots.
- **Factory reset** — hold the BOOT button (GPIO 0, back of board) for 5 seconds to wipe all settings.
- **Status screen** — shows WiFi status, IP address, NTP sync, and uptime. Tap the header to open the overview slideshow.

### Extensible slide architecture

Each slide is a self-contained module (`src/slide_<name>.{h,cpp}`) deriving from the `Slide` base class in [`src/slide_base.h`](src/slide_base.h). To add a new slide:

1. Create `src/slide_<name>.{h,cpp}` implementing `name()`, `draw()` and (optionally) `hasData()`.
2. Register one instance in the `kSlides[]` table in [`src/slideshow.cpp`](src/slideshow.cpp).

Slides with no data are skipped automatically in the rotation. A slide may also
paginate: override `pageCount()` to return >1 and implement `setPage()`, and the
slideshow will split the slide's on-screen window evenly across its pages (e.g.
the Spectrum slide shows 6 band charts per page).

## Getting started

### 1. Clone and configure

```bash
git clone https://github.com/yourname/ubersdr_cyd
cd ubersdr_cyd
cp include/app_config.example.h include/app_config.h
```

Edit `include/app_config.h` with your WiFi credentials (optional — you can also configure on-screen).

**UberSDR server details** (host / port / admin password) are runtime settings, configured the same way as Wi-Fi. The device resolves them in priority order:

1. **`data/ubersdr.json`** on LittleFS (developer config, gitignored — copy from `data/ubersdr.json.example`):
   ```json
   {
     "host": "ubersdr.local",
     "port": 8080,
     "password": "your-admin-password"
   }
   ```
   Upload it with `pio run --target uploadfs`.
2. **Web setup portal** — the `UberSDR Server` card on the settings page (host, port, admin password). Saved to NVS and survives reboots.
3. **Compile-time defaults** in `include/app_config.h` (`UBERSDR_HOST`/`UBERSDR_PORT`/`UBERSDR_PASSWORD`) — only used as a last-resort fallback.

The admin password is sent only as the `X-Admin-Password` header to the configured host and is never committed to version control.

The overview slideshow polls these endpoints round-robin, one every 2 s (~20 s full cycle):

| Endpoint | Auth | Used for |
|---|---|---|
| `GET /api/description` | none | capacity, user count, callsign, timezone, antenna/rotator |
| `GET /admin/sessions?compact=1` | `X-Admin-Password` | user / bypass counts + network throughput (falls back to parsing the full per-session payload on older servers) |
| `GET /admin/system-load` | `X-Admin-Password` | load averages + CPU temperature |
| `GET /api/noisefloor/latest` | none | per-band FT8 SNR (band conditions) |
| `GET /api/noisefloor/fft?band=…` | none | per-band spectrum charts (one band per cycle) |
| `GET /api/spaceweather` | none | K/A index + solar flux |
| `GET /api/weather` | none | terrestrial weather |
| `GET /admin/psk-rank` | `X-Admin-Password` | PSKReporter spots + DXCC rank |
| `GET /admin/wspr-rank` | `X-Admin-Password` | WSPR Live rank |
| `GET /admin/rbn-data` | `X-Admin-Password` | RBN skimmer rank |

### 2. Build and flash

```bash
pio run --target upload
pio device monitor
```

### 3. Configure WiFi on-screen

1. Power on the device.
2. Tap the lower half of the status screen ("Tap here to configure WiFi").
3. Use the on-screen keyboard to enter your SSID, then tap **OK**.
4. Enter your password, then tap **OK**.
5. The device connects and returns to the status screen.

### 4. Configure WiFi via browser (alternative)

1. Connect your phone/laptop to the `UberSDR-Setup` WiFi network (password `ubersdr1`).
2. Open `http://192.168.4.1/` in a browser.
3. Enter your home WiFi credentials and tap **Save settings**.
4. Once the device joins your network the hotspot turns off automatically (unless you tick "Keep hotspot always on").

## Project structure

```
ubersdr_cyd/
├── include/
│   ├── User_Setup.h          # TFT_eSPI pin/driver config for CYD
│   ├── app_config.example.h  # Template — copy to app_config.h
│   └── app_config.h          # Local credentials (gitignored)
├── src/
│   ├── main.cpp
│   ├── settings.{h,cpp}      # NVS-backed settings store
│   ├── connectivity.{h,cpp}  # WiFi + NTP
│   ├── setup_portal.{h,cpp}  # AP hotspot + web config portal
│   ├── display.{h,cpp}       # TFT + touch + on-screen keyboard + status screen
│   ├── reset_button.{h,cpp}  # BOOT button factory reset
│   ├── ubersdr_api.{h,cpp}   # UberSDR HTTP poller → UberSDRSnapshot
│   ├── slideshow.{h,cpp}     # slide registry, auto-advance, touch dispatch
│   ├── slide_base.{h,cpp}    # Slide interface + shared draw helpers/palette
│   ├── slide_users.{h,cpp}   # Users & capacity slide
│   ├── slide_load.{h,cpp}    # System load & CPU temp slide
│   ├── slide_bands.{h,cpp}   # Band-conditions pill grid slide
│   ├── slide_spaceweather.{h,cpp}  # Space-weather slide
│   ├── slide_weather.{h,cpp}       # Terrestrial weather slide
│   ├── slide_antenna.{h,cpp}       # Antenna-switch + rotator compass slide
│   ├── slide_spectrum.{h,cpp}      # Per-band FFT mini-chart grid (paged)
│   ├── slide_ranking.{h,cpp}       # PSKReporter / WSPR / RBN ranking slide
│   ├── notifications.{h,cpp} # Webhook toast queue + overlay renderer
│   └── debug_log.{h,cpp}     # Rolling debug log served at /debug
└── platformio.ini
```

## Wiring / pin reference

All pins are fixed by the CYD board — no external wiring needed.

| Signal | GPIO |
|---|---|
| TFT MOSI | 13 |
| TFT SCLK | 14 |
| TFT CS | 15 |
| TFT DC | 2 |
| TFT MISO | 12 |
| TFT BL | 21 |
| Touch SCLK | 25 |
| Touch MOSI | 32 |
| Touch MISO | 39 |
| Touch CS | 33 |
| Touch IRQ | 36 |
| BOOT button | 0 |
