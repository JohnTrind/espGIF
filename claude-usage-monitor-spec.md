# Claude Usage Monitor — ESP32 Project Spec

## Goal
Standalone ESP32 device that displays Claude subscription usage limits (5h rolling window, weekly reset, etc.) on a small LCD. Portable — works anywhere with configured WiFi.

## Hardware
Same board as espGIF: **Waveshare ESP32-S3-LCD-1.47**
- ESP32-S3 with PSRAM, 16MB flash
- ST7789 172x320 LCD (portrait, rotation 2)
- SD card (optional, not needed for this project)
- WS2812B RGB LED (status indicator)
- WiFi capable, supports HTTPS via WiFiClientSecure

### Pin Configuration
```
LCD: MOSI=45, SCLK=40, CS=42, DC=41, RST=39, BL=48
RGB LED: GPIO 38 (WS2812B, 1 LED)
```

### PlatformIO Base Config
```ini
[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags =
    -DBOARD_HAS_PSRAM
    -DUSER_SETUP_LOADED
    -DST7789_DRIVER
    -DTFT_WIDTH=172
    -DTFT_HEIGHT=320
    -DTFT_MOSI=45
    -DTFT_SCLK=40
    -DTFT_CS=42
    -DTFT_DC=41
    -DTFT_RST=39
    -DTFT_BL=48
    -DTFT_RGB_ORDER=TFT_BGR
    -DSPI_FREQUENCY=80000000
    -DUSE_HSPI_PORT
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    bodmer/TFT_eSPI @ ^2.5.43
    fastled/FastLED @ ^3.7.0
monitor_speed = 115200
```

## Data Source

### API Endpoint
```
GET https://claude.ai/api/organizations/{ORG_UUID}/usage
```

### Authentication
Requires session cookie from claude.ai browser session.
- Cookie header: `Cookie: sessionKey=sk-ant-...` (exact cookie name TBD — user should check DevTools > Application > Cookies on claude.ai)
- The session cookie expires periodically (estimated 1-2 weeks), user needs to update it via the device's web UI

### Organization UUID
Fixed per account. User gets it from the URL when visiting https://claude.ai/settings/usage — the UUID is in the URL or in the API request visible in DevTools Network tab.

### Expected Response Format
The exact JSON structure needs to be captured from DevTools (Network tab, find the /usage request, check Response). Likely contains:
- Current usage period info
- Token/message counts used
- Limits and remaining quota
- Reset timestamps (5h rolling window, daily, weekly)

**User should capture one real response and include it when setting up the new project.**

## Architecture (Suggested)

```
Modules:
├── main.cpp            — Setup/loop, display orchestration
├── wifi_manager        — Multi-WiFi support (reuse from espGIF pattern)
├── usage_client        — HTTPS client, calls Claude API, parses response
├── display_renderer    — Renders usage dashboard on TFT
├── led_status          — Status LED (connecting, ok, error, refreshing)
├── web_server          — Config UI (WiFi, session cookie, org UUID, refresh interval)
```

### Key Behaviors
1. Boot → connect to WiFi (try saved networks, fallback AP mode)
2. Every N minutes (configurable, default 2min) → fetch usage data via HTTPS
3. Parse JSON response → extract usage %, remaining, reset time
4. Render dashboard on LCD
5. LED indicates status (green=ok, blue=refreshing, red=auth expired)
6. Web UI at device IP for configuration

### Display Layout (172x320 portrait)
```
┌──────────────────┐
│   Claude Usage   │  <- Title
│                  │
│  ████████░░ 73%  │  <- 5h window usage bar
│  Resets in 2h14m │
│                  │
│  Daily           │
│  ██████░░░░ 58%  │
│                  │
│  Weekly          │
│  ███░░░░░░░ 31%  │
│  Resets Mon 00:00│
│                  │
│  Messages: 47/65 │  <- or whatever metric
│  Last refresh:   │
│  12:34:05        │
└──────────────────┘
```

### WiFi Configuration
Same pattern as espGIF:
- Store multiple WiFi networks in NVS (indexed keys)
- Try each on boot (10s timeout per network)
- AP mode fallback ("ClaudeMonitor-Setup") with QR code on LCD
- Web UI for managing networks

### Session Cookie Configuration
- User pastes cookie value in the device's web UI (WiFi tab or dedicated "Auth" section)
- Stored in NVS (Preferences)
- Device shows "Auth Expired" on LCD + red LED if API returns 401/403
- Web UI shows current auth status

### HTTPS on ESP32
```cpp
#include <WiFiClientSecure.h>

WiFiClientSecure client;
client.setInsecure(); // or use root CA cert for claude.ai

client.connect("claude.ai", 443);
client.print("GET /api/organizations/");
client.print(orgUUID);
client.println("/usage HTTP/1.1");
client.println("Host: claude.ai");
client.print("Cookie: sessionKey=");
client.println(sessionCookie);
client.println("Connection: close");
client.println();
// Read response...
```

Note: `setInsecure()` skips certificate validation. For production, embed the root CA cert for claude.ai (Let's Encrypt or similar).

## Configuration Needed from User
1. **Organization UUID** — from claude.ai settings URL
2. **Session cookie** — from browser DevTools (name + value)
3. **WiFi credentials** — via AP mode setup or serial
4. **Actual JSON response sample** — to know exactly what fields to parse

## Differences from espGIF
- No SD card needed
- No GIF player / AnimatedGIF library
- No WebSocket streaming
- Adds WiFiClientSecure for HTTPS
- Adds JSON parsing (manual or ArduinoJson)
- Simpler display (text/bars, no images)
- Periodic polling loop instead of frame rendering
