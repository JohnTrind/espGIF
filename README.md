# espGIF

Animated GIF player and live screen-streaming display for the **Waveshare ESP32-S3-LCD-1.47** (172×320 ST7789). GIFs live on an SD card and are managed from a built-in web UI — or you can stream your PC screen to the display in real time over WebSocket.

## Features

- **GIF playback from SD** — each GIF is loaded fully into PSRAM before playback for smooth, reliable decoding; per-GIF rotation (0/90/180/270°) and last-played GIF remembered across reboots.
- **Web UI + REST API** on port 80 — upload, preview, select, rotate and delete GIFs; manage WiFi networks; settings.
- **Live streaming** — WebSocket on port 81 receives raw RGB565 frames from the PC; UDP auto-discovery on port 4210, so the sender finds the device on its own.
- **Painless WiFi setup** — tries saved networks, falls back to an access point and shows a **WiFi QR code on the LCD** to join from a phone; `espgif.local` via mDNS.
- **Status LED** (WS2812B) — connecting, connected, AP mode, playing, streaming, error.

## Hardware

| Part | Details |
|---|---|
| Board | Waveshare ESP32-S3-LCD-1.47 (ESP32-S3, 16 MB flash, PSRAM) |
| Display | ST7789 172×320 — MOSI 45, SCLK 40, CS 42, DC 41, RST 39, BL 48 |
| SD card | SD_MMC 4-bit — CLK 14, CMD 15, D0 16, D1 18, D2 17, D3 21 |
| LED | WS2812B on GPIO 38 |

## Build & flash

[PlatformIO](https://platformio.org/) project, environment `esp32s3`:

```bash
pio run -t upload        # build + flash
pio device monitor       # serial @ 115200
```

Or use the guided installer, which detects the board, flashes it and sets the WiFi over serial:

```bash
python tools/install.py
```

## Tools (`tools/`)

All scripts install their own pip dependencies on first run.

| Script | What it does |
|---|---|
| `convert_gifs.py` | Resizes/rotates GIFs from `gifs_raw/` to 172×320 into `gifs_ready/` |
| `sync_gifs.py` | Syncs `gifs_ready/` to the device over the HTTP API (uploads new, removes deleted) |
| `stream_sender.py` | Captures the screen (or a region) and streams it at a target FPS |
| `stream_window.py` | Windowed streaming variant |
| `install.py` | First-time flash + WiFi configuration |

```bash
python tools/convert_gifs.py
python tools/sync_gifs.py gifs_ready espgif.local
python tools/stream_sender.py --fps 15
```

## REST API

| Method | Endpoint | |
|---|---|---|
| GET | `/api/gifs` | list GIFs |
| GET | `/api/gifs/file?name=` | download a GIF |
| POST | `/api/upload` | upload a GIF |
| POST | `/api/select` | play a GIF |
| DELETE | `/api/gifs?name=` | delete a GIF |
| POST | `/api/rotate` | set per-GIF rotation |
| GET | `/api/status` | device status |
| GET/POST/DELETE | `/api/wifi/networks` | saved WiFi networks |
| GET/POST | `/api/wifiscan` | scan networks |
| GET/POST | `/api/settings` | settings |

## Design notes

- Single-threaded Arduino loop with cooperative modules (`gif_player`, `web_server`, `stream_server`, `wifi_manager`, `sd_manager`, `qr_display`, `led_status`).
- Async web callbacks only set flags; the main loop does the work — avoids racing the display/SD bus.
- Uploads pause playback to avoid SD contention; a streaming client takes exclusive control of the display.
- JSON is parsed by hand to keep the firmware free of ArduinoJson.

## Libraries

TFT_eSPI · AnimatedGIF (bitbank2) · ESPAsyncWebServer (ESP32Async) · FastLED · QRCode (ricmoo)

## License

MIT
