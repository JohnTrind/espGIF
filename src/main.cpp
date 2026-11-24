#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SD_MMC.h>
#include "pin_config.h"
#include "led_status.h"
#include "sd_manager.h"
#include "wifi_manager.h"
#include "gif_player.h"
#include "web_server.h"
#include "stream_server.h"
#include "qr_display.h"

TFT_eSPI tft = TFT_eSPI();

static bool showingInfoScreen = true;

static void showInfoScreen() {
    tft.fillScreen(TFT_BLACK);

    // Title
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(3);
    tft.drawString("espGIF", 86, 30);

    // Divider
    tft.drawLine(20, 70, 152, 70, TFT_DARKGREY);

    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    if (wifiIsAPMode()) {
        tft.drawString("AP Mode", 86, 85);
        tft.setTextSize(1);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString("Scan QR to connect:", 86, 105);

        // Draw QR code for WiFi AP connection (uses custom AP name)
        String apSSID = wifiGetAPSSID();
        drawWifiQR(tft, apSSID.c_str(), 115);

        // IP below QR
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(1);
        String ip = wifiGetIP();
        tft.drawString("http://" + ip, 86, 270);
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString("after connecting", 86, 285);
    } else {
        tft.drawString("Connected", 86, 90);

        // IP Address
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(2);
        String ip = wifiGetIP();
        tft.drawString(ip, 86, 180);

        // Instructions
        tft.setTextSize(1);
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString("Open browser to:", 86, 220);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("http://" + ip, 86, 240);

        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawString("or", 86, 260);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString("http://espgif.local", 86, 280);
    }

    showingInfoScreen = true;
}

// Listen for serial config commands for 3 seconds at boot
// Format: WIFI:ssid:password\n
static void serialConfig() {
    Serial.println("READY");  // signal to installer
    unsigned long start = millis();
    String buf = "";
    while (millis() - start < 3000) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                buf.trim();
                if (buf.startsWith("WIFI:")) {
                    int sep = buf.indexOf(':', 5);
                    if (sep > 5) {
                        String ssid = buf.substring(5, sep);
                        String pass = buf.substring(sep + 1);
                        wifiAddNetwork(ssid, pass);
                        Serial.printf("OK:WIFI:%s\n", ssid.c_str());
                    }
                } else if (buf == "PING") {
                    Serial.println("OK:PONG");
                }
                buf = "";
            } else {
                buf += c;
            }
        }
        delay(10);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== espGIF ===");

    // Listen for installer commands
    serialConfig();

    // Init LED
    ledInit();
    ledSet(LED_CONNECTING);

    // Init display
    tft.init();
    tft.setRotation(2);
    tft.fillScreen(TFT_BLACK);
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);

    // Show boot message
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("espGIF", 86, 140);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Booting...", 86, 170);

    // Init SD card (non-fatal — device works without it, just can't store GIFs)
    if (!sdInit()) {
        Serial.println("WARNING: SD card init failed — insert card and reboot");
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setTextSize(1);
        tft.drawString("No SD card", 86, 190);
        delay(1500);
    }

    // Init WiFi
    wifiInit();

    // Show IP on display
    showInfoScreen();

    // Init web server
    webServerInit();

    // Init GIF player
    gifPlayerInit();

    // Init stream server (WebSocket + UDP discovery)
    streamServerInit();

    // Auto-play last selected GIF if available
    String defaultGif = gifPlayerLoadDefault();
    if (defaultGif.length() > 0 && SD_MMC.exists(gifPath(defaultGif))) {
        Serial.printf("Auto-playing default GIF: %s\n", defaultGif.c_str());
        if (gifPlayerLoad(gifPath(defaultGif))) {
            showingInfoScreen = false;
            ledSet(LED_PLAYING);
        }
    }

    Serial.println("Setup complete!");
}

void loop() {
    // Handle stream server (UDP discovery + timeout)
    streamServerLoop();

    // When streaming, skip GIF logic — stream_server owns the display
    if (streamIsActive()) {
        ledUpdate();
        return;
    }

    // Handle GIF switch requests from web server
    if (gifPlayerHasPendingSwitch()) {
        gifPlayerHandleSwitch();
        showingInfoScreen = false;
        ledSet(LED_PLAYING);
    }

    // Play GIF frames or show info screen
    if (gifPlayerIsPlaying()) {
        gifPlayerPlayFrame();
    } else if (!showingInfoScreen) {
        showInfoScreen();
        ledSet(wifiIsAPMode() ? LED_AP_MODE : LED_CONNECTED);
    }

    ledUpdate();
}
