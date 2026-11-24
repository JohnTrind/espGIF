#include "gif_player.h"
#include "sd_manager.h"
#include <AnimatedGIF.h>
#include <TFT_eSPI.h>
#include <SD_MMC.h>
#include <Preferences.h>

extern TFT_eSPI tft;

static AnimatedGIF gif;
static File gifFile;
static bool playing = false;
static String currentGifPath;
static String currentGifName;
static volatile bool switchRequested = false;
static String pendingGifName;
static volatile bool paused = false;

static int16_t xOffset = 0;
static int16_t yOffset = 0;
static uint16_t dispW = 172;
static uint16_t dispH = 320;
static int gifRotation = 0;

// Line buffer in PSRAM
static uint16_t* lineBuf = nullptr;
static const int MAX_LINE = 480;

// Full file buffer in PSRAM for reliable playback
static uint8_t* fileBuf = nullptr;
static size_t fileBufSize = 0;

// Frame buffer for rotated rendering (PSRAM)
static uint16_t* frameBuf = nullptr;
static int fbWidth = 0;
static int fbHeight = 0;

// ---- Frame buffer management ----

static void allocFrameBuf(int w, int h) {
    size_t needed = (size_t)w * h * sizeof(uint16_t);
    if (frameBuf && fbWidth == w && fbHeight == h) {
        memset(frameBuf, 0, needed);
        return;
    }
    if (frameBuf) { free(frameBuf); frameBuf = nullptr; }
    frameBuf = (uint16_t*)ps_malloc(needed);
    if (frameBuf) {
        fbWidth = w;
        fbHeight = h;
        memset(frameBuf, 0, needed);
        Serial.printf("Frame buffer allocated: %dx%d (%u bytes)\n", w, h, needed);
    } else {
        Serial.println("Frame buffer alloc failed!");
        fbWidth = 0;
        fbHeight = 0;
    }
}

static void freeFrameBuf() {
    if (frameBuf) { free(frameBuf); frameBuf = nullptr; }
    fbWidth = 0;
    fbHeight = 0;
}

// Push the frame buffer to display with rotation applied
static void pushRotatedFrame() {
    if (!frameBuf || !lineBuf) return;

    int outW, outH;
    if (gifRotation == 90 || gifRotation == 270) {
        outW = fbHeight;
        outH = fbWidth;
    } else {
        outW = fbWidth;
        outH = fbHeight;
    }

    // Clip to visible display region (handles negative offsets)
    int srcStartX = (xOffset < 0) ? -xOffset : 0;
    int srcStartY = (yOffset < 0) ? -yOffset : 0;
    int screenX = (xOffset > 0) ? xOffset : 0;
    int screenY = (yOffset > 0) ? yOffset : 0;
    int visW = outW - srcStartX;
    if (screenX + visW > (int)dispW) visW = (int)dispW - screenX;
    int visH = outH - srcStartY;
    if (screenY + visH > (int)dispH) visH = (int)dispH - screenY;
    if (visW <= 0 || visH <= 0 || visW > MAX_LINE) return;

    for (int vy = 0; vy < visH; vy++) {
        int oy = srcStartY + vy;
        for (int vx = 0; vx < visW; vx++) {
            int ox = srcStartX + vx;
            int sx, sy;
            switch (gifRotation) {
                case 90:  sx = oy; sy = fbHeight - 1 - ox; break;
                case 180: sx = fbWidth - 1 - ox; sy = fbHeight - 1 - oy; break;
                case 270: sx = fbWidth - 1 - oy; sy = ox; break;
                default:  sx = ox; sy = oy; break;
            }
            if (sx >= 0 && sx < fbWidth && sy >= 0 && sy < fbHeight) {
                lineBuf[vx] = frameBuf[sy * fbWidth + sx];
            } else {
                lineBuf[vx] = 0;
            }
        }
        tft.pushImage(screenX, screenY + vy, visW, 1, lineBuf);
    }
}

// ---- Draw callbacks ----

static void gifDrawDirect(GIFDRAW* pDraw) {
    uint8_t* s = pDraw->pPixels;
    uint16_t* palette = pDraw->pPalette;

    int16_t srcW = pDraw->iWidth;
    int16_t drawX = xOffset + pDraw->iX;
    int16_t drawY = yOffset + pDraw->iY + pDraw->y;

    if (drawY < 0 || drawY >= (int16_t)dispH) return;

    int16_t srcStart = 0;
    int16_t dstX = drawX;
    int16_t drawW = srcW;

    if (dstX < 0) {
        srcStart = -dstX;
        drawW += dstX;
        dstX = 0;
    }
    if (dstX + drawW > (int16_t)dispW) {
        drawW = (int16_t)dispW - dstX;
    }
    if (drawW <= 0) return;
    if (drawW > MAX_LINE) drawW = MAX_LINE;

    if (pDraw->ucHasTransparency) {
        uint8_t trans = pDraw->ucTransparent;
        int runStart = -1;
        for (int x = 0; x <= drawW; x++) {
            bool isTransparent = (x == drawW) || (s[srcStart + x] == trans);
            if (!isTransparent) {
                if (runStart < 0) runStart = x;
                lineBuf[x - runStart] = palette[s[srcStart + x]];
            } else if (runStart >= 0) {
                tft.pushImage(dstX + runStart, drawY, x - runStart, 1, lineBuf);
                runStart = -1;
            }
        }
    } else {
        for (int x = 0; x < drawW; x++) {
            lineBuf[x] = palette[s[srcStart + x]];
        }
        tft.pushImage(dstX, drawY, drawW, 1, lineBuf);
    }
}

static void gifDrawToBuffer(GIFDRAW* pDraw) {
    if (!frameBuf) return;
    uint8_t* s = pDraw->pPixels;
    uint16_t* palette = pDraw->pPalette;
    int srcW = pDraw->iWidth;
    int srcY = pDraw->iY + pDraw->y;
    uint8_t trans = pDraw->ucTransparent;
    bool hasTrans = pDraw->ucHasTransparency;

    if (srcY < 0 || srcY >= fbHeight) return;

    for (int x = 0; x < srcW; x++) {
        if (hasTrans && s[x] == trans) continue;
        int sx = pDraw->iX + x;
        if (sx >= 0 && sx < fbWidth) {
            frameBuf[srcY * fbWidth + sx] = palette[s[x]];
        }
    }
}

static void gifDraw(GIFDRAW* pDraw) {
    if (!lineBuf) return;
    if (gifRotation == 0) {
        gifDrawDirect(pDraw);
    } else {
        gifDrawToBuffer(pDraw);
    }
}

// ---- Public API ----

void gifPlayerInit() {
    gif.begin(GIF_PALETTE_RGB565_BE);
    lineBuf = (uint16_t*)ps_malloc(MAX_LINE * sizeof(uint16_t));
    if (!lineBuf) lineBuf = (uint16_t*)malloc(MAX_LINE * sizeof(uint16_t));
    dispW = tft.width();
    dispH = tft.height();
    Serial.printf("Display: %dx%d\n", dispW, dispH);
}

static bool loadGifFromSD(const String& path) {
    gifFile = SD_MMC.open(path, FILE_READ);
    if (!gifFile) {
        Serial.printf("Cannot open: %s\n", path.c_str());
        return false;
    }

    size_t fileSize = gifFile.size();
    Serial.printf("Reading GIF: %s (%u bytes)\n", path.c_str(), fileSize);

    if (fileBuf && fileBufSize < fileSize) {
        free(fileBuf);
        fileBuf = nullptr;
    }

    if (!fileBuf) {
        fileBuf = (uint8_t*)ps_malloc(fileSize);
        if (!fileBuf) {
            Serial.println("PSRAM alloc failed, trying heap");
            fileBuf = (uint8_t*)malloc(fileSize);
        }
    }

    if (!fileBuf) {
        Serial.println("Failed to allocate GIF buffer");
        gifFile.close();
        return false;
    }
    fileBufSize = fileSize;

    size_t bytesRead = gifFile.read(fileBuf, fileSize);
    gifFile.close();

    if (bytesRead != fileSize) {
        Serial.printf("Read mismatch: got %u of %u\n", bytesRead, fileSize);
        return false;
    }

    if (gif.open(fileBuf, fileSize, gifDraw)) {
        return true;
    }

    Serial.printf("gif.open from RAM failed, error: %d\n", gif.getLastError());
    return false;
}

bool gifPlayerLoad(const String& path) {
    gifPlayerStop();

    currentGifPath = path;
    int slash = path.lastIndexOf('/');
    currentGifName = (slash >= 0) ? path.substring(slash + 1) : path;

    if (loadGifFromSD(path)) {
        int gw = gif.getCanvasWidth();
        int gh = gif.getCanvasHeight();

        gifRotation = getGifRotation(currentGifName);

        // For rotated GIFs, allocate frame buffer
        if (gifRotation != 0) {
            allocFrameBuf(gw, gh);
        } else {
            freeFrameBuf();
        }

        // For 90/270, effective display dimensions are swapped
        int effectiveW = (gifRotation == 90 || gifRotation == 270) ? gh : gw;
        int effectiveH = (gifRotation == 90 || gifRotation == 270) ? gw : gh;
        xOffset = ((int)dispW - effectiveW) / 2;
        yOffset = ((int)dispH - effectiveH) / 2;

        playing = true;
        tft.fillScreen(TFT_BLACK);
        Serial.printf("GIF playing: %s (%dx%d) rot=%d offset(%d,%d)\n",
                      currentGifName.c_str(), gw, gh, gifRotation, xOffset, yOffset);
        return true;
    }

    Serial.printf("Failed to load GIF: %s\n", path.c_str());
    return false;
}

bool gifPlayerPlayFrame() {
    if (!playing || paused) return false;

    int frameDelay = 0;
    int result = gif.playFrame(false, &frameDelay);

    if (result > 0) {
        // For rotated GIFs, push the frame buffer to display after decode
        if (gifRotation != 0 && frameBuf) {
            pushRotatedFrame();
        }
        if (frameDelay > 0) delay(frameDelay);
        return true;
    }

    if (result == 0) {
        gif.reset();
        return true;
    }

    Serial.printf("playFrame error: %d, reloading\n", gif.getLastError());
    gif.close();
    if (loadGifFromSD(currentGifPath)) {
        return true;
    }

    playing = false;
    return false;
}

void gifPlayerStop() {
    if (playing) {
        gif.close();
        playing = false;
    }
    freeFrameBuf();
    currentGifPath = "";
    currentGifName = "";
}

bool gifPlayerIsPlaying() {
    return playing;
}

String gifPlayerCurrentName() {
    return currentGifName;
}

void gifPlayerRequestSwitch(const String& name) {
    pendingGifName = name;
    switchRequested = true;
}

bool gifPlayerHasPendingSwitch() {
    return switchRequested;
}

void gifPlayerHandleSwitch() {
    if (!switchRequested) return;
    switchRequested = false;
    String path = gifPath(pendingGifName);
    if (gifPlayerLoad(path)) {
        gifPlayerSaveDefault(pendingGifName);
    }
}

void gifPlayerSaveDefault(const String& name) {
    Preferences prefs;
    prefs.begin("gif", false);
    prefs.putString("default", name);
    prefs.end();
    Serial.printf("Default GIF saved: %s\n", name.c_str());
}

String gifPlayerLoadDefault() {
    Preferences prefs;
    String name = "";
    if (prefs.begin("gif", true)) {
        name = prefs.getString("default", "");
        prefs.end();
    }
    return name;
}

void gifPlayerPause() {
    paused = true;
    Serial.println("GIF playback paused");
}

void gifPlayerResume() {
    paused = false;
    Serial.println("GIF playback resumed");
}

bool gifPlayerIsPaused() {
    return paused;
}
