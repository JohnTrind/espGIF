#pragma once

enum LedState {
    LED_OFF,
    LED_CONNECTING,   // blue pulse
    LED_CONNECTED,    // solid green
    LED_AP_MODE,      // solid purple
    LED_PLAYING,      // green blink
    LED_STREAMING,    // cyan pulse
    LED_ERROR         // red flash
};

void ledInit();
void ledSet(LedState state);
void ledUpdate();
