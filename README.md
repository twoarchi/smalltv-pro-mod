# GeekMagic SmallTV-PRO Modded Firmware (v3.12.5)

<p align="center">
  <img src="https://img.shields.io/badge/Platform-ESP32%208MB-red?style=for-the-badge&logo=espressif" alt="ESP32 8MB" />
  <img src="https://img.shields.io/badge/Resolution-240%C3%97240%20IPS-blue?style=for-the-badge" alt="240x240 IPS" />
  <img src="https://img.shields.io/badge/Font-Korean%20Galmuri-brightgreen?style=for-the-badge" alt="Korean Galmuri Font" />
  <img src="https://img.shields.io/badge/Notify%20Engine-Multi--Line%20Card-orange?style=for-the-badge" alt="Multi-Line Card" />
  <img src="https://img.shields.io/badge/Screen%20Protection-Burn--in%20Wiper-purple?style=for-the-badge" alt="Burn-in Wiper" />
  <img src="https://img.shields.io/badge/Version-v3.12.5-blue?style=for-the-badge" alt="v3.12.5" />
</p>

An advanced, feature-rich custom firmware for the **GeekMagic SmallTV-PRO** desktop cube (classic ESP32 with 8 MB Flash and 1.54" 240×240 IPS display).

This fork brings **native Korean bitmap font engine (KS X 1001 2,350 syllables + Galmuri 22px)**, **multi-line card text wrapping mode for SMS notifications**, **scheduled app timers (Timer mode) with 4 time slots**, **LCD burn-in prevention with 7 procedural & animated refresh effects**, **dual stock market tracking (Asian/KRX + US candlesticks with smart pre-market sessions)**, and **high-definition 160×160 animated full-screen notifications** to your desk monitor.

---

## 🌟 Key Features

### 1. Korean Bitmap Font Engine & Multi-Line Card Mode (New in v3.12.0)
* **Native Korean Font Support**: Embedded KS X 1001 2,350 Korean syllables + ASCII + symbols based on GalmuriMono.
* **Optimized 22px Scaling**: 11px base with 2× integer scale (22×22 px Korean, 12×22 px ASCII), with 1px letter spacing and 12px line gap for optimal readability.
* **Multi-Line Card Wrap Mode (`wrap: true`)**: Ideal for SMS/text forwarding (e.g. from Android Tasker). Displays title, divider line, and 5~6 lines of clean wrapped text without scrolling.
* **Zero Marquee Smear**: Marquee scrolling uses a 1ms single-burst background wipe without display bus deadlock.
* **100% Backward Compatible**: All existing notifications without `wrap` retain the classic animated icon + 1-line marquee scroll.

### 2. Scheduled App Timers & Simplified Mode Selection
* **4-Slot 24h App Timer**: Schedule exact apps (Stock 1, Stock 2, Weather) to run based on the time of day with automatic cross-midnight resolution.
* **Streamlined Mode Dropdown**: Clean choice between **Timer** (scheduled by time) and **Carousel** (rotate apps on a dwell timer).

### 2. Screen Refresh & Burn-in Prevention Engine
* **LCD Image Retention Protection**: Periodic pixel exercise routines to prevent and reverse IPS LCD image persistence caused by static stock tickers.
* **Dual Scheduling**: Run periodically (Every N minutes, smartly aligned to the clock e.g. `:00` on the hour) or daily at a specific time (`HH:MM`).
* **Instant WebUI Preview**: Test any effect live on screen with a single click.
* **Touch to Wake**: Tapping the top touch button immediately dismisses the refresh screen and returns to your active dashboard.
* **7 Built-in Refresh Modes**:
  * `matrix`: Cyberpunk digital rain
  * `rainbow`: 8-color SMPTE smooth scrolling bars
  * `starfield`: 3D warp spaceflight stars
  * `black`: Deep Black (liquid crystal discharge, zero glare)
  * `white`: Pure White (industry-standard image retention wiper)
  * `snow`: Retro TV static noise
  * `mascot`: SmallTV animated jumping mascot

### 2. Dual Stock Candlestick Tracking
* **Stock 1 (Domestic / KRX)**: Real-time price quotes, price changes, and full candlestick / line charts powered by Naver Finance API (1-min, 5-min, 30-min, 60-min, Daily, Weekly intervals). Smooth pre-market handling (clean daily candles before 09:00, real-time intraday candles after 09:00).
* **Stock 2 (US / Global)**: Real-time and pre-market quotes via Yahoo Finance API (`SOXX`, `SPY`, `QQQ`, `NVDA`, `AAPL`, `TSM`, etc.). Intelligent multi-session boundary separating yesterday's close from today's live pre-market.
* **Background Pre-fetching**: Data is cached in the background so switching between clock and charts is instantaneous.

### 3. HD 160×160 Animated Notify Engine
* **Clean Integer 2× Scaling**: Source assets are hand-tuned 80×80 pixel matrices rendered at `cell = 2` (160×160 px on the 240×240 panel). Completely eliminates non-integer scaling artifacts, blurriness, and pixel crawling.
* **REST API Webhook**: Simple `/api/notify` JSON endpoint compatible with **Home Assistant**, **n8n**, **Node-RED**, **AI Agents (Claude, AutoGPT)**, and shell scripts.
* **Built-in Animation Presets**:

| Tag (`anim`) | Aliases | Frames & Speed | Description |
| :--- | :--- | :--- | :--- |
| **`phone`** | `ring`, `call` | 10 frames @ 70ms | Retro pink telephone vibrating & ringing |
| **`mailbox`** | `mail` | 16 frames @ 100ms | 3D mailbox raising red flag & opening hatch |
| **`sms`** | `message`, `msg`, `text` | 19 frames @ 100ms | Typing bubble with moving dots and popup |
| **`telegram`** | `tg` | 4 frames @ 100ms | Telegram paper airplane flying & rotating |
| **`beer`** | `beer_chicken` | 1 frame | Beer mug & fried chicken cheer sticker |
| **`busy`** | `busy_coffee` | 1 frame | Working overtime pouring coffee sticker |
| **`cry`** | `crying_bbu` | 1 frame | Crying reaction character sticker |
| **`chicken`** | `cheering_chicken` | 1 frame | Motivational chicken sticker |
| **`yahoo`** | `yahoo_stars` | 1 frame | Celebration stars sticker |

---

## 🚀 REST API Usage

### Screen Refresh Preview (`/api/burnin/preview`)
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"effect":"matrix","durationSec":5}' \
  http://<SMALLTV_IP>/api/burnin/preview
```

### Full-Screen Notification Alert (`/api/notify`)
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"anim":"mailbox","title":"GITHUB","label":"PR #42 merged successfully!","ttl":5}' \
  http://<SMALLTV_IP>/api/notify
```

---

## 📥 Flashing & Installation

### Option A: Web OTA Update (Recommended)
1. Download `firmware_v3.10.0_public_release.bin` from the [Releases](https://github.com/twoarchi/smalltv-pro-mod/releases) page.
2. Open your SmallTV WebUI (`http://<DEVICE_IP>/`) in a browser.
   * *⚠️ Note: Disable browser auto-translation (e.g. Chrome Google Translate) before opening the page.*
3. Go to the **System** or **Update** tab.
4. Select `firmware_v3.10.0_public_release.bin` and upload.
5. The device reboots automatically in ~5 seconds. Existing Wi-Fi credentials and saved stock tickers will be preserved.

### Option B: USB Flashing with esptool (Clean Install / Recovery)
If flashing from scratch or recovering a bricked device:

```bash
esptool.py --chip esp32 --port COM3 --baud 921600 write_flash 0x0 firmware.factory_v3.10.0_public_release.bin
```

### On-device update (Update tab)

The **Update from GitHub** card asks `twoarchi/smalltv-pro-mod` for its newest
release and installs the asset named `smalltv-mod-firmware-esp32-pro.bin`. It
never asks the upstream project, so an upstream release can no longer offer
itself to this device and, installed, replace the modded image.

A release therefore has to carry that asset name. A tag built by CI attaches it
automatically (the `release` job uploads `dist/`, where the 8 MB image is
staged under that name); a release uploaded by hand has to attach it too,
alongside the versioned `firmware_v<tag>_public_release.bin` names. v3.10.0 was
uploaded by hand before this change and carries only the versioned names, so
the card answers `check failed: no matching asset` and installs nothing —
manual upload (Option A) is unaffected. Attaching a Pro image built from this
commit or later under the standard name makes the card work as is, and every
later CI tag release keeps it working.

---

## 🛠️ Building from Source

This project uses [PlatformIO](https://platformio.org/).

```bash
git clone https://github.com/twoarchi/smalltv-pro-mod.git
cd smalltv-pro-mod
pio run -e smalltv_esp32_8mb
```
