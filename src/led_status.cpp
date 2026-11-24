#include "led_status.h"
#include "pin_config.h"
#include <FastLED.h>

static CRGB leds[RGB_LED_NUM];
static LedState currentState = LED_OFF;
static unsigned long lastToggle = 0;
static bool toggleOn = false;

void ledInit() {
    FastLED.addLeds<WS2812B, RGB_LED_PIN, GRB>(leds, RGB_LED_NUM);
    FastLED.setBrightness(30);
    leds[0] = CRGB::Black;
    FastLED.show();
}

void ledSet(LedState state) {
    currentState = state;
    lastToggle = millis();
    toggleOn = true;
}

void ledUpdate() {
    unsigned long now = millis();

    switch (currentState) {
        case LED_OFF:
            leds[0] = CRGB::Black;
            break;

        case LED_CONNECTING: {
            // Blue breathing
            uint8_t val = (uint8_t)((sin(now / 300.0) + 1.0) * 127);
            leds[0] = CRGB(0, 0, val);
            break;
        }

        case LED_CONNECTED:
            leds[0] = CRGB(0, 80, 0);
            break;

        case LED_AP_MODE:
            leds[0] = CRGB(80, 0, 80);
            break;

        case LED_PLAYING:
            if (now - lastToggle > 1000) {
                toggleOn = !toggleOn;
                lastToggle = now;
            }
            leds[0] = toggleOn ? CRGB(0, 80, 0) : CRGB(0, 20, 0);
            break;

        case LED_STREAMING: {
            // Cyan breathing
            uint8_t val = (uint8_t)((sin(now / 200.0) + 1.0) * 100);
            leds[0] = CRGB(0, val, val);
            break;
        }

        case LED_ERROR:
            if (now - lastToggle > 300) {
                toggleOn = !toggleOn;
                lastToggle = now;
            }
            leds[0] = toggleOn ? CRGB(80, 0, 0) : CRGB::Black;
            break;
    }

    FastLED.show();
}
