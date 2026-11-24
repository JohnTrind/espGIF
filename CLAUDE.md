# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

espGIF is a PlatformIO project for a Waveshare ESP32-S3-LCD-1.47 board that displays animated GIFs on a 172x320 ST7789 LCD. GIFs are stored on an SD card and can be managed via a built-in web UI or streamed in real-time from a PC over WebSocket.

**Target hardware:** ESP32-S3 with PSRAM, 16MB flash, ST7789 172x320 LCD, SD card (4-bit SD_MMC), WS2812B RGB LED.

## Build & Flash

This is a PlatformIO project (environment: `esp32s3`).

```bash
pio run                  # build
pio run -t upload        # flash to device
pio device monitor       # serial monitor (115200 baud)
```

## Architecture

The firmware runs a single-threaded Arduino loop with cooperative modules:

- **main.cpp** — Setup/loop orchestration. Shows info screen (IP address, WiFi status) when no GIF is playing. Listens for serial `WIFI:ssid:pass` config commands during first 3 seconds of boot.
- **gif_player** — Loads entire GIF into PSRAM via AnimatedGIF library, renders frame-by-frame to TFT using scanline callback (`gifDraw`). Supports pause/resume for uploads and streaming. Persists last-played GIF name via `Preferences` NVS storage. Per-GIF rotation (0/90/180/270) via PSRAM frame buffer for non-zero rotations.
- **web_server** — AsyncWebServer on port 80. Serves embedded HTML/JS UI (PROGMEM). REST API: `GET /api/gifs`, `GET /api/gifs/file?name=`, `POST /api/upload`, `POST /api/select`, `DELETE /api/gifs?name=`, `GET /api/status`, `POST /api/wifi`, `GET /api/wifiscan`, `POST /api/rotate`, `GET/POST /api/settings`. JSON is parsed manually (no ArduinoJson dependency).
- **stream_server** — WebSocket server on port 81 (`/stream`). Receives raw RGB565 frames (110080 bytes each) and pushes directly to display. UDP discovery on port 4210 (query `ESPGIF?`, reply `ESPGIF:ws://ip:81/stream`). Auto-timeout after 3s with no frames.
- **wifi_manager** — Tries saved credentials (3 attempts, 15s each), falls back to AP mode (custom SSID, default `espGIF-Setup`). Credentials stored in NVS `Preferences`. mDNS hostname: `espgif.local`. WiFi scanning with AP+STA mode support.
- **qr_display** — Generates WiFi QR code on the TFT LCD using ricmoo/QRCode library. Shown in AP mode for easy phone connection.
- **sd_manager** — SD_MMC 4-bit mode. GIFs stored in `/gifs/` directory on SD card.
- **led_status** — WS2812B status LED via FastLED. States: connecting (blue breathing), connected (green), AP mode (purple), playing (green pulse), streaming (cyan breathing), error (red blink).

**Display coordinate note:** The display is used in rotation 2 (portrait, 172 wide x 320 tall). GIFs are centered on screen with calculated offsets.

## Python Tools (`tools/`)

All tools auto-install their pip dependencies on first run.

- **convert_gifs.py** — Converts GIFs in `gifs_raw/` to 172x320 with 90-degree clockwise rotation, outputs to `gifs_ready/`. Requires Pillow.
- **sync_gifs.py** — Syncs `gifs_ready/` folder to the device via HTTP API (uploads new, deletes removed). Usage: `python tools/sync_gifs.py [folder] [host]`
- **stream_sender.py** — Captures screen and streams RGB565 frames to ESP32 via WebSocket. Supports auto-discovery, region capture, configurable FPS. Requires websocket-client, pillow, mss.
- **stream_window.py** — Windowed streaming variant.
- **install.py** — Initial device setup (serial WiFi configuration).

## Key Libraries

- **TFT_eSPI** — Display driver, configured entirely via `build_flags` in platformio.ini (no User_Setup.h)
- **AnimatedGIF** (bitbank2) — GIF decoding with RGB565 palette mode
- **ESPAsyncWebServer** (ESP32Async fork) — Async HTTP + WebSocket
- **FastLED** — WS2812B LED control
- **QRCode** (ricmoo) — QR code generation for WiFi AP connection display

## Important Patterns

- GIF files are fully loaded into PSRAM before playback (not streamed from SD) for reliable decoding. Rotated GIFs also use a PSRAM frame buffer for transform.
- Per-GIF rotation metadata stored in `/gifs/.meta.json` on SD card. Parsed/written manually.
- The `switchRequested` flag uses a volatile bool for thread-safe communication between the async web server callbacks and the main loop.
- Upload operations pause GIF playback (`gifPlayerPause`/`gifPlayerResume`) to avoid SD bus contention.
- Streaming takes exclusive control of the display — GIF playback is paused when a WebSocket client connects.
- The TFT_eSPI driver config is entirely in platformio.ini build_flags with `-DUSER_SETUP_LOADED` — there is no separate User_Setup.h file.
