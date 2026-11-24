#include "stream_server.h"
#include "gif_player.h"
#include "led_status.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPAsyncWebServer.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

static AsyncWebSocket ws("/stream");
static AsyncWebServer wsServer(81);
static WiFiUDP udp;

static const uint16_t UDP_DISCOVERY_PORT = 4210;
static const char* DISCOVERY_QUERY = "ESPGIF?";

static volatile bool streaming = false;
static uint32_t lastFrameMs = 0;
static const uint32_t STREAM_TIMEOUT_MS = 3000;

// Expected frame size: 172 * 320 * 2 (RGB565) = 110080 bytes
static const size_t FRAME_SIZE = 172 * 320 * 2;

static void pushFrame(uint8_t* data, size_t len) {
    if (len != FRAME_SIZE) {
        Serial.printf("Stream: bad frame size %u (expected %u)\n", len, FRAME_SIZE);
        return;
    }

    // Push RGB565 data directly to display
    tft.startWrite();
    tft.setAddrWindow(0, 0, 172, 320);
    tft.pushPixels((uint16_t*)data, 172 * 320);
    tft.endWrite();

    lastFrameMs = millis();
}

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("Stream client connected: #%u from %s\n",
                          client->id(), client->remoteIP().toString().c_str());
            // Pause GIF playback, take over display
            gifPlayerPause();
            streaming = true;
            lastFrameMs = millis();
            tft.fillScreen(TFT_BLACK);
            ledSet(LED_STREAMING);
            // Only allow one client
            for (auto& c : server->getClients()) {
                if (c.id() != client->id() && c.status() == WS_CONNECTED) {
                    c.close();
                }
            }
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("Stream client disconnected: #%u\n", client->id());
            if (server->count() == 0) {
                streaming = false;
                gifPlayerResume();
                ledSet(LED_PLAYING);
            }
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->opcode == WS_BINARY && info->final && info->index == 0 && info->len == len) {
                // Complete binary frame received in one message
                pushFrame(data, len);
            }
            break;
        }

        case WS_EVT_ERROR:
            Serial.printf("Stream WS error: #%u\n", client->id());
            break;

        case WS_EVT_PONG:
            break;
    }
}

void streamServerInit() {
    // WebSocket server
    ws.onEvent(onWsEvent);
    wsServer.addHandler(&ws);
    wsServer.begin();
    Serial.println("Stream WebSocket server on port 81");

    // UDP discovery listener
    udp.begin(UDP_DISCOVERY_PORT);
    Serial.printf("UDP discovery listening on port %u\n", UDP_DISCOVERY_PORT);
}

void streamServerLoop() {
    // Handle UDP discovery
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        char buf[32];
        int len = udp.read(buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            if (strcmp(buf, DISCOVERY_QUERY) == 0) {
                // Reply with our WebSocket URL
                String reply = "ESPGIF:ws://" + WiFi.localIP().toString() + ":81/stream";
                udp.beginPacket(udp.remoteIP(), udp.remotePort());
                udp.print(reply);
                udp.endPacket();
                Serial.printf("Discovery reply to %s: %s\n",
                              udp.remoteIP().toString().c_str(), reply.c_str());
            }
        }
    }

    // Stream timeout — if no frames for 3s, assume sender disconnected
    if (streaming && (millis() - lastFrameMs > STREAM_TIMEOUT_MS)) {
        Serial.println("Stream timeout, resuming GIF");
        streaming = false;
        ws.closeAll();
        gifPlayerResume();
        ledSet(LED_PLAYING);
    }

    // Cleanup dead WS connections
    ws.cleanupClients();
}

bool streamIsActive() {
    return streaming;
}
