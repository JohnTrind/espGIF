#include "qr_display.h"
#include <qrcode.h>

void drawWifiQR(TFT_eSPI& tft, const char* ssid, int yStart) {
    // Build WiFi QR string: WIFI:T:nopass;S:<ssid>;;
    String qrData = "WIFI:T:nopass;S:";
    qrData += ssid;
    qrData += ";;";

    // QR Code version 3 = 29x29 modules, fits our data
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, qrData.c_str());

    // Calculate module size to fit display width (172px) with padding
    int maxSize = 140;
    int moduleSize = maxSize / qrcode.size;
    if (moduleSize < 2) moduleSize = 2;

    int qrPixelSize = qrcode.size * moduleSize;
    int xOffset = (tft.width() - qrPixelSize) / 2;
    int yOffset = yStart + 5;

    // White background with small border
    int border = moduleSize * 2;
    tft.fillRect(xOffset - border, yOffset - border,
                 qrPixelSize + border * 2, qrPixelSize + border * 2, TFT_WHITE);

    // Draw QR modules
    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                tft.fillRect(xOffset + x * moduleSize,
                             yOffset + y * moduleSize,
                             moduleSize, moduleSize, TFT_BLACK);
            }
        }
    }
}
